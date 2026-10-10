// Execute production selection and parent/child mounting against a deterministic
// metadata/syscall adapter. No mount privileges or Android device are required.
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
namespace logging {
enum class Level { Info, Warning, Error };
}
namespace fixture {
constexpr int directory = 0040000;
constexpr int regular = 0100000;
struct Entry {
    int mode = directory;
    bool opaque = false;
};
struct State {
    std::map<std::string, Entry> entries;
    std::map<std::string, int> stat_reads;
    std::map<std::string, int> opaque_reads;
    std::string fail_stat;
    std::string fail_opaque;
    bool classic = false;
    std::string lowerdir;
    std::string classic_options;
    int stock_binds = 0;
    int empty_layers = 0;
} state;
}  // namespace fixture

#define stat FixtureStat
#define lstat fixture_lstat
#define chdir fixture_chdir
#define close fixture_close
#ifdef S_IFMT
#undef S_IFMT
#endif
#define S_IFMT 0170000
#ifdef S_ISDIR
#undef S_ISDIR
#endif
#define S_ISDIR(mode) (((mode) & S_IFMT) == fixture::directory)
#ifdef S_ISREG
#undef S_ISREG
#endif
#define S_ISREG(mode) (((mode) & S_IFMT) == fixture::regular)
struct stat {
    int st_mode = 0;
};
int lstat(const char* path, struct stat* out) {
    auto& state = fixture::state;
    ++state.stat_reads[path];
    if (path == state.fail_stat) {
        errno = EIO;
        return -1;
    }
    const auto it = state.entries.find(path);
    if (it == state.entries.end()) {
        errno = ENOENT;
        return -1;
    }
    out->st_mode = it->second.mode;
    return 0;
}
int chdir(const char*) {
    return 0;
}
int close(int) {
    return 0;
}
namespace fsutil {
void mlog(const std::string&, logging::Level = logging::Level::Info) {}
bool register_umount(const std::string&) {
    return true;
}
bool directory_is_opaque(const std::string& path, bool& opaque) {
    auto& state = fixture::state;
    ++state.opaque_reads[path];
    if (path == state.fail_opaque)
        return false;
    opaque = state.entries.at(path).opaque;
    return true;
}
bool prepare_empty_mountpoint(const std::string&) {
    ++fixture::state.empty_layers;
    return true;
}
#include "mount_decode_under_test.inc"
}  // namespace fsutil
using fsutil::mlog;
fs::path runtime_data_dir() {
    return "runtime";
}
constexpr unsigned kFsopenCloexec = 1;
constexpr unsigned kFsconfigSetString = 1;
constexpr unsigned kFsconfigCmdCreate = 6;
constexpr unsigned kFsmountCloexec = 1;
constexpr unsigned kMoveMountEmptyPath = 4;
#ifndef AT_FDCWD
constexpr int AT_FDCWD = -100;
#endif
int sys_fsopen(const char*, unsigned) {
    return fixture::state.classic ? -1 : 1;
}
int sys_fsconfig(int, unsigned, const char* key, const char* value, int) {
    if (key && std::string(key) == "lowerdir")
        fixture::state.lowerdir = value;
    return 0;
}
int sys_fsmount(int, unsigned, unsigned) {
    return 2;
}
int sys_move_mount(int, const char*, int, const char*, unsigned) {
    return 0;
}
int mount(const char*, const char*, const char*, unsigned long, const void* data) {
    fixture::state.classic_options = static_cast<const char*>(data);
    return 0;
}
#include "overlay_index_under_test.inc"
#include "overlay_record_under_test.inc"
bool rbind_mount(const std::string&, const std::string&, std::vector<AttachedMount>&) {
    ++fixture::state.stock_binds;
    return true;
}
#include "overlay_layers_under_test.inc"

int main() {
    std::istringstream no_children;
    const MountPointIndex mount_points(no_children);
    const std::vector<std::string> roots{"/top", "/bottom", "/last"};
    std::vector<AttachedMount> attached;
    const std::string empty = (runtime_data_dir() / "run" / "overlay_empty").string();

    // Both mount APIs receive the same selected layers. An opaque lower root
    // cuts off stock and later modules; each selected root is inspected once.
    for (const bool classic : {false, true}) {
        fixture::state = {};
        auto& state = fixture::state;
        state.classic = classic;
        state.entries = {{"/top", {}}, {"/bottom", {fixture::directory, true}}, {"/last", {}}};
        attached.clear();
        assert(mount_overlay("/system/etc", roots, "", "", "KSU", attached, mount_points));
        assert(attached.size() == 1);
        assert((state.stat_reads == std::map<std::string, int>{{"/bottom", 1}, {"/top", 1}}));
        assert(state.opaque_reads == state.stat_reads);
        assert(state.empty_layers == 0 && state.stock_binds == 0);
        assert(classic ? state.classic_options == "lowerdir=/top:/bottom"
                       : state.lowerdir == "/top:/bottom");
    }

    // A single opaque layer needs the empty lower, never the stock layer.
    fixture::state = {};
    fixture::state.entries = {{"/top", {fixture::directory, true}}, {"/bottom", {}}};
    assert(mount_overlay("/system/etc", roots, "", "", "KSU", attached, mount_points));
    assert(fixture::state.lowerdir == "/top:" + empty);
    assert(fixture::state.empty_layers == 1);
    assert(fixture::state.stat_reads.size() == 1 && fixture::state.opaque_reads.at("/top") == 1);

    // A transparent stack retains stock and precedence; a missing layer is
    // skipped without attempting to read its opaque metadata.
    fixture::state = {};
    fixture::state.entries = {{"/top", {}}, {"/last", {}}};
    assert(mount_overlay("/system/etc", roots, "", "", "KSU", attached, mount_points));
    assert(fixture::state.lowerdir == "/top:/last:.");
    assert(fixture::state.stat_reads.size() == 3);
    assert(fixture::state.opaque_reads.size() == 2);

    // The ancestor is opaque while the child itself is transparent. Passing
    // the selection into mount_overlayfs must preserve that ancestor cutoff.
    fixture::state = {};
    auto& state = fixture::state;
    state.entries = {{"/top", {}},          {"/top/sub", {fixture::directory, true}},
                     {"/top/sub/deep", {}}, {"/bottom", {}},
                     {"/bottom/sub", {}},   {"/bottom/sub/deep", {}}};
    assert(mount_overlay_child("/system/etc/sub/deep", "/sub/deep", roots, "./sub/deep", "KSU",
                               attached));
    assert(state.lowerdir == "/top/sub/deep:" + empty);
    assert((state.stat_reads ==
            std::map<std::string, int>{{"/top", 1}, {"/top/sub", 1}, {"/top/sub/deep", 1}}));
    assert(state.opaque_reads == state.stat_reads);

    // Missing children below an opaque ancestor, or a file replacing a
    // directory, must not resurrect lower modules or bind stock back.
    for (const bool replace_with_file : {false, true}) {
        state = {};
        state.entries = {{"/top", {fixture::directory, !replace_with_file}}, {"/bottom", {}}};
        if (replace_with_file)
            state.entries["/top/sub"] = {fixture::regular, false};
        attached.clear();
        assert(mount_overlay_child("/system/etc/sub", "/sub", roots, "./sub", "KSU", attached));
        assert(attached.empty() && state.lowerdir.empty() && state.stock_binds == 0);
        assert(state.stat_reads.size() == 2 && state.opaque_reads.size() == 1);
    }

    // No module at the child preserves the original stock submount.
    state = {};
    state.entries = {{"/top", {}}, {"/bottom", {}}, {"/last", {}}};
    assert(mount_overlay_child("/system/etc/sub", "/sub", roots, "./sub", "KSU", attached));
    assert(state.stock_binds == 1 && state.lowerdir.empty());

    for (const bool metadata_error : {false, true}) {
        state = {};
        state.entries = {{"/top", {}}};
        if (metadata_error)
            state.fail_opaque = "/top";
        else
            state.fail_stat = "/top";
        attached.clear();
        assert(!mount_overlay("/system/etc", roots, "", "", "KSU", attached, mount_points));
        assert(attached.empty() && state.lowerdir.empty());
    }
    std::cout << "PASS OverlayFS layer precedence, opaque cutoff and metadata scan counts\n";
}
