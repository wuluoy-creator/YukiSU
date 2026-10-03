#include "kagami/kasumi_client.hpp"
#include "uapi/kasumi.h"

#include <cassert>
#include <cerrno>
#include <cstring>
#include <deque>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {
struct ExpectedCall {
    unsigned long command;
    std::function<int(void*)> reply;
};
std::deque<ExpectedCall> calls;

void expect(unsigned long command, std::function<int(void*)> reply) {
    calls.push_back({command, std::move(reply)});
}

void expect_features(int bitmask) {
    expect(KSM_IOC_GET_FEATURES, [bitmask](void* arg) {
        *static_cast<int*>(arg) = bitmask;
        return 0;
    });
}

void expect_error(unsigned long command, int error) {
    expect(command, [error](void*) {
        errno = error;
        return -1;
    });
}

void expect_hide_rule(std::uint64_t cursor, std::uint64_t id, const char* path) {
    expect(KSM_IOC_USER_HIDE_QUERY, [=](void* value) {
        auto& arg = *static_cast<kasumi_user_hide_arg*>(value);
        assert(arg.rule_id == cursor);
        arg.rule_id = id;
        arg.path_len = std::strlen(path);
        std::memcpy(arg.path, path, arg.path_len + 1);
        return 0;
    });
}

void test_text_replies() {
    namespace ksm = kagami::kasumi;
    // The ABI reports its length separately and need not copy a terminator.
    expect(KSM_IOC_LIST_RULES, [](void* value) {
        auto& arg = *static_cast<kasumi_syscall_list_arg*>(value);
        std::memset(arg.buf, 'x', arg.size);
        return 0;
    });
    assert(ksm::active_rules() == std::string(64UL * 1024, 'x'));

    expect(KSM_IOC_GET_HOOKS, [](void* value) {
        auto& arg = *static_cast<kasumi_syscall_list_arg*>(value);
        std::memcpy(arg.buf, "hooks trailing bytes", 20);
        arg.size = 5;
        return 0;
    });
    assert(ksm::hooks() == "hooks");

    expect(KSM_IOC_GET_HOOKS, [](void* value) {
        ++static_cast<kasumi_syscall_list_arg*>(value)->size;
        return 0;
    });
    assert(ksm::hooks().empty());
    assert(errno == EPROTO);

    expect_error(KSM_IOC_LIST_RULES, EACCES);
    assert(ksm::active_rules().empty());
    assert(errno == EACCES);
    assert(calls.empty());
}

void test_feature_errors() {
    namespace ksm = kagami::kasumi;
    expect_error(KSM_IOC_GET_FEATURES, EIO);
    assert(!ksm::clear_overlay_xattr_hiding());
    assert(errno == EIO);

    expect_features(0);
    assert(ksm::clear_overlay_xattr_hiding());
    expect_features(KSM_FEATURE_OVERLAY_XATTR_HIDE);
    expect(KSM_IOC_HIDE_OVERLAY_XATTRS, [](void* value) {
        const auto& arg = *static_cast<kasumi_syscall_arg*>(value);
        assert(arg.src != nullptr && arg.src[0] == '\0');
        return 0;
    });
    assert(ksm::clear_overlay_xattr_hiding());

    expect_error(KSM_IOC_GET_FEATURES, EACCES);
    assert(!ksm::set_mount_hide(true));
    assert(errno == EACCES);
    expect_features(KSM_FEATURE_MOUNT_HIDE);
    assert(!ksm::set_mount_hide(true, ksm::MountHideMode::Aggressive));
    assert(errno == EOPNOTSUPP);

    expect_features(KSM_FEATURE_MOUNT_HIDE);
    expect(KSM_IOC_SET_MOUNT_HIDE, [](void* value) {
        const auto& arg = *static_cast<kasumi_mount_hide_arg*>(value);
        assert(arg.enable == 1);
        return 0;
    });
    assert(ksm::set_mount_hide(true));

    expect_features(KSM_FEATURE_MOUNT_HIDE | KSM_FEATURE_MOUNT_HIDE_AGGRESSIVE);
    expect(KSM_IOC_SET_MOUNT_HIDE_MODE, [](void* value) {
        assert(*static_cast<int*>(value) == KSM_MOUNT_HIDE_MODE_AGGRESSIVE);
        return 0;
    });
    expect(KSM_IOC_SET_MOUNT_HIDE, [](void* value) {
        auto& arg = *static_cast<kasumi_mount_hide_arg*>(value);
        assert(arg.enable == 1);
        arg.err = -EBUSY;
        return 0;
    });
    assert(!ksm::set_mount_hide(true, ksm::MountHideMode::Aggressive));
    assert(errno == EBUSY);
    assert(calls.empty());
}

void test_maps_paths() {
    namespace ksm = kagami::kasumi;
    assert(!ksm::add_maps_rule(1, 2, 3, 4, std::string(KSM_MAX_LEN_PATHNAME, 'x')));
    assert(errno == ENAMETOOLONG);
    assert(!ksm::add_maps_rule(1, 2, 3, 4, std::string("/path\0suffix", 12)));
    assert(errno == EINVAL);

    const std::string maximum_path(KSM_MAX_LEN_PATHNAME - 1, 'x');
    expect(KSM_IOC_ADD_MAPS_RULE, [&](void* value) {
        const auto& arg = *static_cast<kasumi_maps_rule*>(value);
        assert(arg.target_ino == 1 && arg.target_dev == 2);
        assert(arg.spoofed_ino == 3 && arg.spoofed_dev == 4);
        assert(std::string(arg.spoofed_pathname) == maximum_path);
        return 0;
    });
    assert(ksm::add_maps_rule(1, 2, 3, 4, maximum_path));
    expect(KSM_IOC_ADD_MAPS_RULE, [](void* value) {
        const auto& arg = *static_cast<kasumi_maps_rule*>(value);
        assert(arg.spoofed_pathname[0] == '\0');
        return 0;
    });
    // An empty pathname preserves the original path while spoofing ino/dev.
    assert(ksm::add_maps_rule(1, 2, 3, 4, ""));
    assert(calls.empty());
}

void test_hide_snapshot() {
    namespace ksm = kagami::kasumi;
    std::vector<ksm::UserHideRule> rules;
    expect_hide_rule(0, 7, "/first");
    expect_hide_rule(7, 15, "/second");
    expect_hide_rule(15, 0, "");
    assert(ksm::user_hide_rules(rules));
    assert(rules.size() == 2 && rules[0].path == "/first" && rules[1].id == 15);

    expect_hide_rule(0, 8, "/replacement");
    expect_error(KSM_IOC_USER_HIDE_QUERY, EIO);
    assert(!ksm::user_hide_rules(rules));
    assert(errno == EIO);
    assert(rules.size() == 2 && rules[0].path == "/first" && rules[1].id == 15);

    expect(KSM_IOC_USER_HIDE_QUERY, [](void* value) {
        auto& arg = *static_cast<kasumi_user_hide_arg*>(value);
        arg.path_len = sizeof(arg.path);
        return 0;
    });
    assert(!ksm::user_hide_rules(rules));
    assert(errno == EPROTO);
    expect_hide_rule(0, 1, "/first");
    expect_hide_rule(1, 1, "/first");
    assert(!ksm::user_hide_rules(rules));
    assert(errno == EOVERFLOW);
    assert(calls.empty());
}
}  // namespace

extern "C" int ksu_kasumi_ioctl(unsigned long command, void* arg) {
    assert(!calls.empty());
    auto call = std::move(calls.front());
    calls.pop_front();
    assert(call.command == command);
    return call.reply(arg);
}

int main() {
    test_text_replies();
    test_feature_errors();
    test_maps_paths();
    test_hide_snapshot();
    const auto modules =
        kagami::kasumi::active_modules_from_rules("add /system/a /data/adb/modules/z/system/a 0\n"
                                                  "merge /system/b /data/adb/modules/a/system/b\n"
                                                  "add /system/c /data/adb/modules/z/system/c 0\n");
    assert((modules == std::vector<std::string>{"a", "z"}));
    std::cout << "kasumi_client_test: all checks passed\n";
}
