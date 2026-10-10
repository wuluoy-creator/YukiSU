/**
 * ksuinit - Init module
 *
 * Handles the initialization sequence:
 * - Mount filesystems
 * - Setup logging
 * - Detect GKI KernelSU
 * - Load LKM
 * - Setup real init
 */

#include "init.hpp"
#include "loader.hpp"
#include "log.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/vfs.h>
#include <unistd.h>

namespace ksuinit {

namespace {

/**
 * RAII class for auto-unmounting filesystems
 */
class AutoUmount {
public:
    explicit AutoUmount(const char* mountpoint) : mountpoint_(mountpoint) {}
    AutoUmount(const AutoUmount&) = delete;
    AutoUmount& operator=(const AutoUmount&) = delete;
    AutoUmount(AutoUmount&&) = delete;
    AutoUmount& operator=(AutoUmount&&) = delete;

    ~AutoUmount() {
        if (mountpoint_ != nullptr && umount2(mountpoint_, MNT_DETACH) != 0) {
            KLOGE("Cannot umount %s: %s", mountpoint_, strerror(errno));
        }
    }

private:
    const char* mountpoint_;
};

/**
 * Mount a filesystem
 */
bool mount_filesystem(const char* fstype, const char* mountpoint) {
    // Create mountpoint if it doesn't exist
    if (mkdir(mountpoint, 0755) != 0 && errno != EEXIST) {
        KLOGE("Cannot create mountpoint %s: %s", mountpoint, strerror(errno));
        return false;
    }

    // Mount the filesystem
    if (mount(fstype, mountpoint, fstype, 0, nullptr) != 0) {
        KLOGE("Cannot mount %s on %s: %s", fstype, mountpoint, strerror(errno));
        return false;
    }

    return true;
}

/**
 * Prepare the temporary /proc mount used for printk configuration.
 */
AutoUmount prepare_mount() {
    // Reuse an existing procfs mount without taking ownership of it.
    constexpr decltype(statfs::f_type) proc_magic = 0x9fa0;
    struct statfs status{};
    if (statfs("/proc", &status) == 0 && status.f_type == proc_magic) {
        return AutoUmount(nullptr);
    }
    return AutoUmount(mount_filesystem("proc", "/proc") ? "/proc" : nullptr);
}

/**
 * Setup kernel logging via /dev/kmsg
 */
void setup_kmsg() {
    const char* device = "/dev/kmsg";

    // Check if /dev/kmsg exists
    if (access(device, F_OK) != 0) {
        // Try to create it
        if (mknod("/kmsg", S_IFCHR | 0666, makedev(1, 11)) == 0 || errno == EEXIST) {
            device = "/kmsg";
        }
    }

    // Initialize kernel log
    log_init(device);
}

/**
 * Disable kmsg rate limiting
 */
void unlimit_kmsg() {
    const int fd = open("/proc/sys/kernel/printk_devkmsg", O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        return;
    }
    constexpr char kOn[] = "on\n";
    ssize_t result;
    do {
        result = write(fd, kOn, sizeof(kOn) - 1);
    } while (result < 0 && errno == EINTR);
    close(fd);
}

}  // anonymous namespace

const char* real_init_path() {
    return access("/init.real", F_OK) == 0 ? "/init.real" : "/system/bin/init";
}

bool init() {
    // Setup kernel log first
    setup_kmsg();

    KLOGI("Hello, KernelSU!");

    // Mount /proc temporarily for printk configuration and symbol resolution.
    // Only a mount created here is unmounted when this scope exits.
    {
        auto auto_umount = prepare_mount();

        // Disable kmsg rate limiting (requires /proc)
        unlimit_kmsg();

        // Load the KernelSU LKM module
        KLOGI("Loading kernelsu.ko..");
        if (!load_module("/kernelsu.ko")) {
            KLOGE("Cannot load kernelsu.ko");
        }
    }
    // The temporary /proc mount is unmounted here.

    // Determine the real init path
    const char* real_init = real_init_path();
    // Keep the original relative ramdisk link when init.real is present.
    const char* link_target = strcmp(real_init, "/init.real") == 0 ? "init.real" : real_init;

    KLOGI("init is %s", real_init);

    // Stage the replacement before atomically renaming it over /init. A failed
    // symlink must not leave the ramdisk without an init executable.
    constexpr const char* staged_init = "/.ksu_init_link";
    if (symlink(link_target, staged_init) != 0) {
        KLOGE("Cannot stage init symlink to %s: %s", link_target, strerror(errno));
        return false;
    }
    if (rename(staged_init, "/init") != 0) {
        KLOGE("Cannot replace /init: %s", strerror(errno));
        unlink(staged_init);
        return false;
    }

    return true;
}

}  // namespace ksuinit
