#include "core/json.hpp"
#include "uapi/sumh.h"

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

namespace sumhp {
std::filesystem::path runtime_data_dir() {
    return "/mock/runtime";
}
#include "sumh_persistence_under_test.inc"
}  // namespace sumhp

int main() {
    std::vector<std::string> rules = {"/retained"};
    std::string error;
    for (const int code : {EACCES, EIO, 0}) {
        contents.reset();
        read_error = code;
        assert(!sumhp::load_user_hide_rules(rules, error));
        assert(!error.empty() && rules == std::vector<std::string>{"/retained"});
    }

    read_error = 0;
    for (const auto* invalid :
         {"", "[", "null", "{}", "[\"/good\", null]", "[\"/good\", \"relative\"]", "[\"\"]",
          "[\"/path\\u0000suffix\"]"}) {
        contents = invalid;
        assert(!sumhp::load_user_hide_rules(rules, error));
        assert(rules == std::vector<std::string>{"/retained"});
    }
    contents = "[\"/" + std::string(SUMH_USER_HIDE_PATH_MAX - 1, 'x') + "\"]";
    assert(!sumhp::load_user_hide_rules(rules, error));
    assert(rules == std::vector<std::string>{"/retained"});

    contents = "[\"/first\", \"/second\", \"/first\"]";
    assert(sumhp::load_user_hide_rules(rules, error));
    assert((rules == std::vector<std::string>{"/first", "/second", "/first"}));

    contents = "[]";
    assert(sumhp::load_user_hide_rules(rules, error));
    assert(rules.empty());

    rules = {"/old"};
    contents.reset();
    read_error = ENOENT;
    assert(sumhp::load_user_hide_rules(rules, error));
    assert(rules.empty());
    std::cout << "sumh_persistence_test: all checks passed\n";
}
