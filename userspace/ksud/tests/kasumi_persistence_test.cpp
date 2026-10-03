#include "core/json.hpp"
#include "uapi/kasumi.h"

#include <cassert>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {
std::optional<std::string> contents;
int read_error = 0;
}  // namespace

namespace ksud {
std::optional<std::string> read_file(const std::string&) {
    errno = read_error;
    return contents;
}
}  // namespace ksud

namespace kagami {
std::filesystem::path runtime_data_dir() {
    return "/mock/runtime";
}
#include "kasumi_persistence_under_test.inc"
}  // namespace kagami

int main() {
    std::vector<std::string> rules = {"/retained"};
    std::string error;
    for (const int code : {EACCES, EIO, 0}) {
        contents.reset();
        read_error = code;
        assert(!kagami::load_user_hide_rules(rules, error));
        assert(!error.empty() && rules == std::vector<std::string>{"/retained"});
    }

    read_error = 0;
    for (const auto* invalid :
         {"", "[", "null", "{}", "[\"/good\", null]", "[\"/good\", \"relative\"]", "[\"\"]",
          "[\"/path\\u0000suffix\"]"}) {
        contents = invalid;
        assert(!kagami::load_user_hide_rules(rules, error));
        assert(rules == std::vector<std::string>{"/retained"});
    }
    contents = "[\"/" + std::string(KSM_USER_HIDE_PATH_MAX - 1, 'x') + "\"]";
    assert(!kagami::load_user_hide_rules(rules, error));
    assert(rules == std::vector<std::string>{"/retained"});

    contents = "[\"/first\", \"/second\", \"/first\"]";
    assert(kagami::load_user_hide_rules(rules, error));
    assert((rules == std::vector<std::string>{"/first", "/second", "/first"}));

    contents = "[]";
    assert(kagami::load_user_hide_rules(rules, error));
    assert(rules.empty());

    rules = {"/old"};
    contents.reset();
    read_error = ENOENT;
    assert(kagami::load_user_hide_rules(rules, error));
    assert(rules.empty());
    std::cout << "kasumi_persistence_test: all checks passed\n";
}
