// run_kasumi_host_tests.py extracts the production restore function into this
// fixture so kernel transport, storage readiness and persisted state can fail
// independently without mounting or modifying a real device.
#include "kagami/kasumi_client.hpp"

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {
bool boot_completed = true;
bool available = true;
bool ownership_query_ok = true;
bool managed = true;
bool rules_query_ok = true;
bool mutation_ok = true;
bool namespace_ok = true;
bool parent_ready = true;
bool persistence_ok = true;
int enabled = 1;
std::vector<std::string> persisted;
std::vector<kagami::kasumi::UserHideRule> registered;
std::vector<std::string> mutations;

bool user_hide_restore_pending = false;

bool load_user_hide_rules(std::vector<std::string>& rules, std::string& error) {
    if (!persistence_ok) {
        error = "invalid persisted rules";
        return false;
    }
    rules = persisted;
    return true;
}

namespace logging {
enum class Level { Debug, Error };
}

void mlog(const std::string&, logging::Level = logging::Level::Debug) {}

namespace fsutil {
template <typename Callback>
bool run_in_init_mount_ns(Callback callback) {
    return namespace_ok && callback();
}
}  // namespace fsutil

namespace fs {
struct path {
    std::string value;
    explicit path(const std::string& value) : value(value) {}
    path parent_path() const { return path(value.substr(0, value.rfind('/'))); }
    const char* c_str() const { return value.c_str(); }
};
}  // namespace fs

struct stat {
    unsigned int st_mode;
};
int stat(const char*, struct stat* value) {
    value->st_mode = parent_ready ? 1 : 0;
    return parent_ready ? 0 : -1;
}
bool S_ISDIR(unsigned int mode) {
    return mode == 1;
}

void reset() {
    boot_completed = available = ownership_query_ok = managed = rules_query_ok = true;
    mutation_ok = namespace_ok = parent_ready = persistence_ok = true;
    enabled = 1;
    persisted.clear();
    registered.clear();
    mutations.clear();
    user_hide_restore_pending = false;
}
}  // namespace

namespace ksud {
std::string getprop(const char*) {
    return boot_completed ? "1" : "0";
}
}  // namespace ksud

namespace kagami::kasumi {
bool is_available() {
    return available;
}
bool managed_hide_mode(bool& result) {
    result = managed;
    return ownership_query_ok;
}
bool user_hide_rules(std::vector<UserHideRule>& output) {
    output = registered;
    return rules_query_ok;
}
bool delete_user_hide(const std::string& path, std::uint64_t) {
    mutations.push_back("delete:" + path);
    return mutation_ok;
}
bool upsert_user_hide(const std::string& path, UserHideRule*) {
    mutations.push_back("upsert:" + path);
    return mutation_ok;
}
int enabled_state() {
    return enabled;
}
bool hide_path(const std::string& path) {
    mutations.push_back("hide:" + path);
    return mutation_ok;
}
}  // namespace kagami::kasumi

namespace {
#include "kasumi_restore_under_test.inc"
}

int main() {
    std::string error;
    reset();
    boot_completed = false;
    user_hide_restore_pending = true;
    assert(restore_user_hide_rules(error, false));
    assert(!user_hide_restore_pending && mutations.empty());

    reset();
    persistence_ok = false;
    registered = {{1, 0, 0, 0, 0, "/keep"}};
    assert(!restore_user_hide_rules(error, false));
    assert(user_hide_restore_pending && mutations.empty());

    reset();
    ownership_query_ok = false;
    assert(!restore_user_hide_rules(error, false));
    assert(user_hide_restore_pending && mutations.empty());

    reset();
    rules_query_ok = false;
    assert(!restore_user_hide_rules(error, false));
    assert(user_hide_restore_pending && mutations.empty());

    reset();
    registered = {{1, 0, 0, 0, 0, "/stale"}, {2, 0, 0, 0, 0, "/keep"}};
    persisted = {"/keep", "/new", "/new"};
    assert(restore_user_hide_rules(error, false));
    assert(!user_hide_restore_pending);
    assert((mutations == std::vector<std::string>{"delete:/stale", "upsert:/new"}));

    mutations.clear();
    mutation_ok = false;
    assert(!restore_user_hide_rules(error, true));
    assert(user_hide_restore_pending);
    assert((mutations == std::vector<std::string>{"delete:/stale", "upsert:/new"}));

    mutations.clear();
    mutation_ok = true;
    assert(restore_user_hide_rules(error, true));
    assert(!user_hide_restore_pending);

    reset();
    managed = false;
    enabled = -1;
    assert(!restore_user_hide_rules(error, false));
    assert(user_hide_restore_pending && mutations.empty());

    enabled = 1;
    persisted = {"/parent/file"};
    parent_ready = false;
    assert(!restore_user_hide_rules(error, true));
    assert(user_hide_restore_pending && mutations.empty());

    parent_ready = true;
    assert(restore_user_hide_rules(error, true));
    assert(!user_hide_restore_pending);
    assert((mutations == std::vector<std::string>{"hide:/parent/file"}));
    std::cout << "kasumi_restore_test: all checks passed\n";
}
