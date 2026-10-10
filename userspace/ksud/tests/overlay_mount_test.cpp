// Run production registration and orchestration; simulate mount syscalls and
// module planning so failures can be tested without root or an Android device.
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

constexpr int MNT_DETACH = 2;

struct Config {
    bool enable_overlay_xattr_hide = true;
    std::string mount_source = "KSU";
    std::vector<std::string> partitions;
};
struct ModuleEntry {
    std::string id;
};
namespace logging {
enum class Level { Info, Error };
}

namespace fixture {
const std::vector<std::string> overlays = {"/system/etc", "/system/etc/child", "/vendor/lib"};
const std::vector<std::string> mounted = {"/system/etc", "/system/etc/child", "/system/etc/stock",
                                          "/vendor/lib"};
struct State {
    bool available = true;
    bool fail_journal = false;
    std::string fail_registration;
    std::string fail_xattrs;
    std::set<std::string> live;
    std::set<std::string> registered;
    std::vector<std::string> xattr_attempts;
    std::vector<std::string> journal;
    std::vector<std::string> detached;
    int journal_writes = 0;
} state;
}  // namespace fixture

namespace sumhp::sumh {
bool is_available() {
    return fixture::state.available;
}
bool hide_overlay_xattrs(const std::string& path) {
    auto& state = fixture::state;
    assert(state.live.count(path));
    assert(state.registered.count(path));
    state.xattr_attempts.push_back(path);
    return path != state.fail_xattrs;
}
}  // namespace sumhp::sumh

namespace fsutil {
#include "mount_decode_under_test.inc"

void mlog(const std::string&, logging::Level = logging::Level::Info) {}
const std::vector<std::string>& managed_partitions() {
    static const std::vector<std::string> partitions = {"system", "vendor"};
    return partitions;
}
bool register_umount(const std::string& path) {
    auto& state = fixture::state;
    assert(state.live.count(path));
    if (path == state.fail_registration)
        return false;
    state.registered.insert(path);
    return true;
}
bool unregister_umount(const std::string& path) {
    assert(!fixture::state.live.count(path));
    return fixture::state.registered.erase(path) == 1;
}
bool write_mount_journal(const std::string&, const std::vector<std::string>& paths) {
    auto& state = fixture::state;
    ++state.journal_writes;
    if (state.fail_journal)
        return false;
    state.journal = paths;
    return true;
}
}  // namespace fsutil
using fsutil::mlog;

namespace storage {
enum class Mode { Tmpfs };
struct Handle {
    bool ok = true;
    Mode mode = Mode::Tmpfs;
    std::string content_dir = "/mnt/module-mirror";
};
const char* mode_name(Mode) {
    return "tmpfs";
}
Handle setup(const Config&) {
    return {};
}
}  // namespace storage

#include "overlay_index_under_test.inc"
#include "overlay_record_under_test.inc"

bool sync_content(const std::vector<ModuleEntry>&, const storage::Handle&,
                  const std::vector<std::string>&) {
    return true;
}
std::map<std::string, std::vector<std::string>> plan_overlays(const std::string&,
                                                              const std::vector<std::string>&,
                                                              const std::vector<std::string>&) {
    return {{"/system/etc", {"system-layer"}}, {"/vendor/lib", {"vendor-layer"}}};
}
bool mount_overlay(const std::string& root, const std::vector<std::string>&, const std::string&,
                   const std::string&, const std::string&, std::vector<AttachedMount>& attached,
                   const MountPointIndex&) {
    auto& state = fixture::state;
    state.live.insert(root);
    if (!record_mount(root, attached))
        return false;
    if (root == "/system/etc") {
        state.live.insert(root + "/child");
        if (!record_mount(root + "/child", attached))
            return false;
        state.live.insert(root + "/stock");
        if (!record_mount(root + "/stock", attached, false))
            return false;
    }
    return true;
}
std::string overlay_journal() {
    return "/data/adb/sumhp/run/overlay_mounts.list";
}
int umount2(const char* path, int flags) {
    assert(flags == MNT_DETACH);
    fixture::state.detached.emplace_back(path);
    return fixture::state.live.erase(path) ? 0 : -1;
}

#include "overlay_mount_under_test.inc"

int main() {
    // A single snapshot preserves escaped paths and nested stock submounts.
    // Duplicates and neighboring path prefixes must not become child mounts.
    std::istringstream mountinfo(
        "12 1 0:1 / /system/etc/deep/child rw - tmpfs tmpfs rw\n"
        "13 1 0:1 / /system/etc rw - tmpfs tmpfs rw\n"
        "14 1 0:1 / /system/etc/space\\040name rw - tmpfs tmpfs rw\n"
        "15 1 0:1 / /system/etc/deep rw - tmpfs tmpfs rw\n"
        "16 1 0:1 / /system/etc/deep rw - tmpfs tmpfs rw\n"
        "17 1 0:1 / /system/etc-neighbor/child rw - tmpfs tmpfs rw\n"
        "18 1 0:1 / /vendor/lib/child rw - tmpfs tmpfs rw\n"
        "malformed\n"
        "19 1 0:1 /\n"
        "\t20\t1\t0:1\t/\t/system/etc/tab\\011name\trw - tmpfs tmpfs rw\n");
    const MountPointIndex index(mountinfo);
    assert((index.children("/system/etc") ==
            std::vector<std::string>{"/system/etc/deep", "/system/etc/deep/child",
                                     "/system/etc/space name", "/system/etc/tab\tname"}));
    assert((index.children("/vendor/lib") == std::vector<std::string>{"/vendor/lib/child"}));
    assert(index.children("/system/etc/deep/child").empty());
    assert(index.children("/system/et").empty());
    assert(index.children("/missing").empty());
    // Queries continue to use the captured stock view after the input changes.
    mountinfo.clear();
    mountinfo.str("21 1 0:1 / /system/etc/new-overlay rw - overlay KSU rw\n");
    assert(index.children("/system/etc").size() == 4);
    assert(index.children("/system/etc/deep").size() == 1);

    const std::vector<ModuleEntry> modules = {{"module"}};
    Config config;

    // Every independent overlay superblock is registered on initial mount,
    // including a child reconstructed under a planned leaf. Stock binds are not.
    fixture::state = {};
    assert(mount_modules(modules, config));
    assert(fixture::state.xattr_attempts == fixture::overlays);
    assert(fixture::state.journal == fixture::overlays);
    assert(fixture::state.live.size() == fixture::mounted.size());
    assert(fixture::state.detached.empty());

    // An operator-disabled feature and an unavailable SUMH kernel preserve
    // ordinary module mounting, without trying to register attributes.
    for (int disabled = 0; disabled < 2; ++disabled) {
        fixture::state = {};
        config.enable_overlay_xattr_hide = disabled != 0;
        fixture::state.available = disabled == 0;
        assert(mount_modules(modules, config));
        assert(fixture::state.xattr_attempts.empty());
        assert(fixture::state.journal == fixture::overlays);
    }
    config.enable_overlay_xattr_hide = true;

    // A child registration error must fail the plan and roll back all mounts,
    // including the stock bind, without committing a success journal.
    fixture::state = {};
    fixture::state.fail_xattrs = "/system/etc/child";
    assert(!mount_modules(modules, config));
    assert((fixture::state.xattr_attempts ==
            std::vector<std::string>{"/system/etc", "/system/etc/child"}));
    assert(fixture::state.journal_writes == 0);
    assert(fixture::state.live.empty() && fixture::state.registered.empty());
    assert(fixture::state.detached ==
           std::vector<std::string>(fixture::mounted.rbegin(), fixture::mounted.rend()));

    fixture::state = {};
    fixture::state.fail_registration = "/system/etc/child";
    assert(!mount_modules(modules, config));
    assert(fixture::state.xattr_attempts.empty());
    assert(fixture::state.journal_writes == 0);
    assert(fixture::state.live.empty() && fixture::state.registered.empty());

    fixture::state = {};
    fixture::state.fail_journal = true;
    assert(!mount_modules(modules, config));
    assert(fixture::state.xattr_attempts == fixture::overlays);
    assert(fixture::state.live.empty() && fixture::state.registered.empty());
    std::cout << "PASS OverlayFS stock mount index, parent/child registration and rollback\n";
}
