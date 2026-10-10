// Execute production directory/file traversal with simulated filesystem and
// mount boundaries so stock-directory enumeration can be counted without root.
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>

enum class NType { Regular, Directory, Symlink, Whiteout };
struct Node {
    std::string name;
    NType type = NType::Directory;
    std::map<std::string, Node> children;
    std::string module_path;
    bool replace = false;
};
struct Walk {
    std::vector<std::string> committed;
    int files = 0;
    int tmpfs_dirs = 0;
    int symlinks = 0;
};
constexpr unsigned long MS_RDONLY = 1;
constexpr unsigned long MS_REMOUNT = 32;
constexpr unsigned long MS_BIND = 4096;
constexpr unsigned long MS_MOVE = 8192;
constexpr unsigned long MS_REC = 16384;
constexpr unsigned long MS_PRIVATE = 1UL << 18;
constexpr int O_CREAT = 1, O_EXCL = 2, O_WRONLY = 4, O_CLOEXEC = 8;

namespace fixture {
struct State {
    std::map<std::string, NType> stock;
    std::vector<std::string> opened_dirs;
    std::vector<std::string> skeletons;
    std::vector<std::string> mirrors;
    std::vector<std::string> symlinks;
    std::vector<std::pair<std::string, std::string>> binds;
    std::vector<std::pair<std::string, unsigned long>> mounts;
    int stat_calls = 0;
    std::map<std::string, int> path_stats;
    int read_calls = 0;
    std::string change_after_stat;
    std::string fail_stat;
    std::string fail_open;
    std::string fail_read;
    std::string fail_bind;
};
State state;
}  // namespace fixture

#define stat FixtureStat
struct stat {
    NType st_mode = NType::Regular;
};
#ifdef S_ISDIR
#undef S_ISDIR
#endif
#ifdef S_ISREG
#undef S_ISREG
#endif
#define S_ISDIR(mode) ((mode) == NType::Directory)
#define S_ISREG(mode) ((mode) == NType::Regular)
int lstat(const char* path, struct stat* out) {
    auto& s = fixture::state;
    ++s.stat_calls;
    if (++s.path_stats[path] == 2 && s.change_after_stat == path)
        s.stock[path] = NType::Symlink;
    if (s.fail_stat == path) {
        errno = EACCES;
        return -1;
    }
    const auto found = s.stock.find(path);
    if (found == s.stock.end()) {
        errno = ENOENT;
        return -1;
    }
    out->st_mode = found->second;
    return 0;
}
struct dirent {
    char d_name[256]{};
};
struct DIR {
    std::string path;
    std::vector<std::string> names;
    std::size_t index = 0;
    dirent current;
};
DIR* opendir(const char* path) {
    auto& s = fixture::state;
    s.opened_dirs.emplace_back(path);
    if (s.fail_open == path) {
        errno = EACCES;
        return nullptr;
    }
    auto* dir = new DIR;
    dir->path = path;
    dir->names = {".", ".."};
    const std::string prefix = std::string(path) + "/";
    for (const auto& [entry, type] : s.stock) {
        (void)type;
        if (entry.rfind(prefix, 0) == 0 && entry.find('/', prefix.size()) == std::string::npos)
            dir->names.push_back(entry.substr(prefix.size()));
    }
    return dir;
}
dirent* readdir(DIR* dir) {
    ++fixture::state.read_calls;
    if (fixture::state.fail_read == dir->path) {
        errno = EIO;
        return nullptr;
    }
    if (dir->index == dir->names.size())
        return nullptr;
    const auto& name = dir->names[dir->index++];
    assert(name.size() < sizeof(dir->current.d_name));
    std::strcpy(dir->current.d_name, name.c_str());
    return &dir->current;
}
int closedir(DIR* dir) {
    delete dir;
    return 0;
}
int open(const char*, int flags, int) {
    assert(flags == (O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC));
    return 10;
}
int close(int fd) {
    assert(fd == 10);
    return 0;
}
int mount(const char*, const char* target, const char*, unsigned long flags, const void*) {
    fixture::state.mounts.emplace_back(target, flags);
    return 0;
}
namespace logging {
enum class Level { Debug, Error };
bool enabled(Level) {
    return false;
}
}  // namespace logging
void mlog(const std::string&, logging::Level = logging::Level::Debug) {}
namespace fsutil {
bool bind_mount(const std::string& source, const std::string& target) {
    fixture::state.binds.emplace_back(source, target);
    return fixture::state.fail_bind != target;
}
}  // namespace fsutil
std::string join(const std::string& parent, const std::string& name) {
    return parent == "/" ? parent + name : parent + "/" + name;
}
bool lexists(const std::string& path) {
    struct stat st{};
    return lstat(path.c_str(), &st) == 0;
}
bool tmpfs_skeleton(const std::string&, const std::string& work, const Node&) {
    fixture::state.skeletons.push_back(work);
    return true;
}
bool mount_mirror(const std::string& real, const std::string&, const std::string& name) {
    fixture::state.mirrors.push_back(join(real, name));
    return true;
}
bool clone_symlink(const std::string&, const std::string& work) {
    fixture::state.symlinks.push_back(work);
    return true;
}
bool do_mount(Node&, const std::string&, const std::string&, bool, Walk&);
#include "magic_traversal_under_test.inc"

Node node(const std::string& name, NType type = NType::Directory) {
    Node result;
    result.name = name;
    result.type = type;
    result.module_path = "/module/" + name;
    return result;
}
void reset() {
    fixture::state = {};
    fixture::state.stock = {{"/system", NType::Directory},
                            {"/system/etc", NType::Directory},
                            {"/system/etc/hosts", NType::Regular},
                            {"/system/etc/stock", NType::Regular},
                            {"/system/etc/subdir", NType::Directory}};
}
bool walk(Node& tree, Walk& result, bool has_tmpfs = false) {
    return do_mount(tree, "/system/etc", "/work/etc", has_tmpfs, result);
}
int main() {
    auto& s = fixture::state;
    // The amount of stock content must not affect an existing-file override.
    for (const int stock_count : {0, 4096}) {
        reset();
        for (int i = 0; i < stock_count; ++i)
            s.stock["/system/etc/unrelated-" + std::to_string(i)] = NType::Regular;
        // An unreadable directory does not prevent binding a known file in it.
        s.fail_open = "/system/etc";
        Node root = node("system");
        root.module_path.clear();
        root.children["etc"] = node("etc");
        root.children["etc"].children["hosts"] = node("hosts", NType::Regular);
        Walk result;
        assert(do_mount(root, "/system", "/work", false, result));
        assert(s.opened_dirs.empty() && s.read_calls == 0);
        assert(s.stat_calls < 10);
        assert(s.skeletons.empty() && s.mirrors.empty());
        assert(s.binds.size() == 1 && s.binds.front().second == "/system/etc/hosts");
        assert(result.files == 1 && result.tmpfs_dirs == 0);
        assert(result.committed == std::vector<std::string>{"/system/etc/hosts"});
        assert((s.mounts == std::vector<std::pair<std::string, unsigned long>>{
                                {"/system/etc/hosts", MS_REMOUNT | MS_BIND | MS_RDONLY},
                                {"/system/etc/hosts", MS_PRIVATE}}));
    }
    // New files still create a skeleton, mirror unrelated entries, and replace
    // existing module targets only once inside that skeleton.
    reset();
    Node tree = node("etc");
    tree.children["hosts"] = node("hosts", NType::Regular);
    tree.children["added"] = node("added", NType::Regular);
    Walk result;
    assert(walk(tree, result));
    assert(s.opened_dirs == std::vector<std::string>{"/system/etc"});
    assert((s.mirrors == std::vector<std::string>{"/system/etc/stock", "/system/etc/subdir"}));
    assert(s.binds.size() == 3);  // movable skeleton plus two module files
    assert(result.files == 2 && result.tmpfs_dirs == 1);
    assert(result.committed == std::vector<std::string>{"/system/etc"});
    assert(std::count(s.mounts.begin(), s.mounts.end(),
                      std::make_pair(std::string("/system/etc"), MS_MOVE)) == 1);

    // Whiteouts of existing files and symlinks also require preserving stock
    // entries, while deleted entries must never be mirrored into the skeleton.
    for (const auto type : {NType::Whiteout, NType::Symlink, NType::Directory}) {
        reset();
        tree = node("etc");
        tree.children["hosts"] = node("hosts", type);
        result = {};
        assert(walk(tree, result));
        assert(s.opened_dirs == std::vector<std::string>{"/system/etc"});
        assert((s.mirrors == std::vector<std::string>{"/system/etc/stock", "/system/etc/subdir"}));
        assert(result.tmpfs_dirs == 1);
        if (type == NType::Symlink)
            assert(s.symlinks == std::vector<std::string>{"/work/etc/hosts"});
    }
    reset();
    tree = node("etc");
    tree.children["absent"] = node("absent", NType::Whiteout);
    result = {};
    assert(walk(tree, result));
    assert(s.opened_dirs.empty() && s.skeletons.empty() && result.committed.empty());

    // Opaque replacement excludes all stock entries; an inherited skeleton
    // still mirrors stock entries even when no new mount must be published.
    for (const bool opaque : {false, true}) {
        reset();
        tree = node("etc");
        tree.replace = opaque;
        tree.children["hosts"] = node("hosts", NType::Regular);
        result = {};
        assert(walk(tree, result, !opaque));
        assert(s.opened_dirs.size() == (opaque ? 0U : 1U));
        assert(s.mirrors.size() == (opaque ? 0U : 2U));
        assert(result.files == 1 && result.tmpfs_dirs == (opaque ? 1 : 0));
        assert(result.committed.size() == (opaque ? 1U : 0U));
    }
    // Partition roots cannot gain new entries, and filesystem/mount failures
    // remain visible to the surrounding transaction's rollback handler.
    for (int failure = 0; failure < 5; ++failure) {
        reset();
        tree = node("etc");
        tree.children["added"] = node("added", NType::Regular);
        switch (failure) {
        case 0:
            tree.module_path.clear();
            break;
        case 1:
            s.fail_stat = "/system/etc";
            break;
        case 2:
            s.fail_open = "/system/etc";
            break;
        case 3:
            s.fail_read = "/system/etc";
            break;
        case 4:
            s.fail_bind = "/work/etc/added";
            break;
        }
        result = {};
        assert(!walk(tree, result));
        assert(result.committed.empty());
    }
    reset();
    tree = node("etc");
    tree.children["hosts"] = node("hosts", NType::Regular);
    s.fail_bind = "/system/etc/hosts";
    result = {};
    assert(!walk(tree, result));
    assert(s.opened_dirs.empty() && result.committed.empty());
    // Keep the final type check before a direct bind even though the parent
    // already inspected the target while deciding whether a skeleton is needed.
    reset();
    tree = node("etc");
    tree.children["hosts"] = node("hosts", NType::Regular);
    s.change_after_stat = "/system/etc/hosts";
    result = {};
    assert(!walk(tree, result));
    assert(s.binds.empty() && result.committed.empty());
    std::cout
        << "Magic Mount traversal regressions passed (0 stock reads for 4096 unrelated files)\n";
}
