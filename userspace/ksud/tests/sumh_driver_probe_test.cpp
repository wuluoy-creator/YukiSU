// Exercise the actual driver acquisition/cache code so failed KMI probes cannot
// mistake stale errno, denied access or allocation failures for absent SUMH.
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <iostream>
#include <limits>

namespace ksud {
struct DriverFd {
    int fd = -1;
    bool su_session = false;
    int error = 0;
};
struct PrctlGetFdCmd {
    int32_t result;
    int32_t fd;
};
constexpr int KSU_PRCTL_GET_FD = 1;
constexpr int SYS_reboot = 2;
constexpr int KSU_INSTALL_MAGIC1 = 3;
constexpr int KSU_INSTALL_MAGIC2 = 4;
constexpr int unanswered = std::numeric_limits<int>::min();
int g_driver_fd = -1;
bool g_driver_fd_init = false;
int g_driver_fd_error = ENODEV;
int g_sigsys_handler_installed = 1;
int g_reboot_fallback_trapped = 0;
int g_reboot_fallback_in_flight = 0;
DriverFd inherited;
int prctl_result = unanswered;
int prctl_fd = -1;
int prctl_errno = EINVAL;
int reboot_fd = unanswered;
int reboot_errno = EINVAL;
bool reboot_trap = false;
int probe_calls = 0;
DriverFd scan_driver_fd() {
    return inherited;
}
int prctl(int, PrctlGetFdCmd* command, int, int, int) {
    ++probe_calls;
    command->result = prctl_result;
    command->fd = prctl_fd;
    errno = prctl_errno;
    return prctl_errno ? -1 : 0;
}
long syscall(int, int, int, int, int* fd) {
    ++probe_calls;
    *fd = reboot_fd;
    g_reboot_fallback_trapped = reboot_trap;
    errno = reboot_errno;
    return reboot_errno ? -1 : 0;
}
void setup_sigsys_handler() {}
#define LOGD(...) ((void)0)
#define LOGE(...) ((void)0)
#include "sumh_driver_probe_under_test.inc"
#undef LOGD
#undef LOGE

void reset() {
    g_driver_fd = -1;
    g_driver_fd_init = false;
    g_driver_fd_error = ENODEV;
    g_sigsys_handler_installed = 1;
    g_reboot_fallback_trapped = g_reboot_fallback_in_flight = 0;
    inherited = {};
    prctl_result = reboot_fd = unanswered;
    prctl_fd = -1;
    prctl_errno = reboot_errno = EINVAL;
    reboot_trap = false;
    probe_calls = 0;
}

void expect_cached_error(int expected) {
    errno = 0;
    assert(get_driver_fd() == -1 && errno == expected);
    const int calls = probe_calls;
    errno = ENOTTY;  // An unrelated failed ioctl cannot poison the cache.
    assert(get_driver_fd() == -1 && errno == expected && probe_calls == calls);
}
}  // namespace ksud

int main() {
    using namespace ksud;
    reset();
    expect_cached_error(ENODEV);
    reset();
    inherited.fd = 7;
    assert(get_driver_fd() == 7 && probe_calls == 0);
    reset();
    prctl_result = 0;
    prctl_fd = 8;
    assert(get_driver_fd() == 8 && probe_calls == 1);
    reset();
    reboot_fd = 9;
    assert(get_driver_fd() == 9 && probe_calls == 2);
    reset();
    prctl_result = -ENOMEM;
    expect_cached_error(ENOMEM);
    reset();
    reboot_fd = -EMFILE;
    expect_cached_error(EMFILE);
    reset();
    inherited.error = EACCES;
    expect_cached_error(EACCES);
    reset();
    reboot_errno = EPERM;
    expect_cached_error(EPERM);
    reset();
    reboot_trap = true;
    expect_cached_error(EPERM);
    reset();
    reboot_errno = EINTR;
    expect_cached_error(EINTR);
    reset();
    prctl_errno = reboot_errno = 0;
    expect_cached_error(EIO);
    std::cout << "sumh_driver_probe_test: all checks passed\n";
}
