#include "../src/su_args.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace args = ksud::su_args;

namespace {

using Argv = std::vector<std::string>;

std::string show(const Argv& argv) {
    std::string out;
    for (const auto& a : argv) {
        out += out.empty() ? "" : " ";
        out += '[' + a + ']';
    }
    return out;
}

void expect(const Argv& input, const Argv& option_argv, const std::optional<std::string>& exe,
            const Argv& exec_args) {
    const args::ParsedArgv parsed = args::split(input);
    bool ok = parsed.option_argv == option_argv && parsed.executable == exe &&
              parsed.exec_args == exec_args;
    if (!ok) {
        std::cerr << "FAIL: " << show(input) << "\n"
                  << "  option_argv want " << show(option_argv) << "\n"
                  << "              got  " << show(parsed.option_argv) << "\n"
                  << "  executable  want " << exe.value_or("<none>") << "\n"
                  << "              got  " << parsed.executable.value_or("<none>") << "\n"
                  << "  exec_args   want " << show(exec_args) << "\n"
                  << "              got  " << show(parsed.exec_args) << "\n";
    }
    assert(ok);
}

// Shorthand for the common case of no positional command.
void expect_options_only(const Argv& input, const Argv& option_argv) {
    expect(input, option_argv, std::nullopt, {});
}

void test_takes_separate_value() {
    assert(args::takes_separate_value("-Z"));
    assert(args::takes_separate_value("-c"));
    assert(args::takes_separate_value("-s"));
    assert(args::takes_separate_value("-g"));
    assert(args::takes_separate_value("-G"));
    assert(args::takes_separate_value("-z"));
    assert(args::takes_separate_value("-cn"));  // legacy --context alias
    assert(args::takes_separate_value("-Ms"));  // value letter ends the cluster
    assert(args::takes_separate_value("--shell"));
    assert(args::takes_separate_value("--context"));
    assert(args::takes_separate_value("--supp-group"));
    assert(args::takes_separate_value("--group"));
    assert(args::takes_separate_value("--command"));

    assert(!args::takes_separate_value("-"));
    assert(!args::takes_separate_value("--"));
    assert(!args::takes_separate_value("-M"));
    assert(!args::takes_separate_value("-mm"));
    assert(!args::takes_separate_value("-lp"));
    assert(!args::takes_separate_value("-Zu:r:shell:s0"));  // value is inside the cluster
    assert(!args::takes_separate_value("--context=u:r:shell:s0"));
    assert(!args::takes_separate_value("--login"));
    assert(!args::takes_separate_value("2000"));
    assert(!args::takes_separate_value(""));
}

// The three cases reported broken on device.
void test_reported_failures() {
    expect_options_only({"su", "2000", "-c", "id"}, {"su", "-c", "id", "2000"});
    expect_options_only({"su", "-Z", "ctx", "2000", "-c", "id -Z"},
                        {"su", "-Z", "ctx", "-c", "id -Z", "2000"});
    expect_options_only({"su", "2000", "-Z", "ctx", "-c", "id -Z"},
                        {"su", "-Z", "ctx", "-c", "id -Z", "2000"});
}

void test_no_regression() {
    expect_options_only({"su"}, {"su"});
    expect_options_only({"su", "2000"}, {"su", "2000"});
    expect_options_only({"su", "-c", "id"}, {"su", "-c", "id"});
    expect_options_only({"su", "-Z", "ctx", "-c", "id -Z"}, {"su", "-Z", "ctx", "-c", "id -Z"});
    expect_options_only({"su", "-"}, {"su", "-"});
    expect_options_only({"su", "-", "2000"}, {"su", "-", "2000"});
    // -c still merges every trailing argument into one shell command string.
    expect_options_only({"su", "-c", "id", "-Z"}, {"su", "-c", "id -Z"});
    expect_options_only({"su", "-M", "-c", "echo", "a", "b"}, {"su", "-M", "-c", "echo a b"});
}

void test_positional_command() {
    expect({"su", "2000", "ls"}, {"su", "2000"}, "ls", {});
    expect({"su", "2000", "ls", "-l"}, {"su", "2000"}, "ls", {"-l"});
    expect({"su", "-M", "2000", "ls"}, {"su", "-M", "2000"}, "ls", {});
    expect({"su", "-Z", "ctx", "2000", "ls", "-l"}, {"su", "-Z", "ctx", "2000"}, "ls", {"-l"});
    expect({"su", "-", "2000", "ls", "-l"}, {"su", "-", "2000"}, "ls", {"-l"});
    expect({"su", "-l", "2000", "ls", "-la"}, {"su", "-l", "2000"}, "ls", {"-la"});
    // A positional command needs an explicit user: here "ls" is the user and -l is --login.
    expect_options_only({"su", "ls", "-l"}, {"su", "-l", "ls"});
    // Arguments reach the command verbatim, with no re-quoting.
    expect({"su", "2000", "echo", "a  b"}, {"su", "2000"}, "echo", {"a  b"});
}

// Options taking a separate value must not have that value mistaken for the user. The long-option
// cases are the ones upstream's "--shell=" / "--context=" equality checks get wrong.
void test_value_option_not_mistaken_for_user() {
    expect({"su", "--shell", "/system/bin/sh", "2000", "ls"},
           {"su", "--shell", "/system/bin/sh", "2000"}, "ls", {});
    expect({"su", "--context", "ctx", "2000", "ls"}, {"su", "--context", "ctx", "2000"}, "ls", {});
    expect({"su", "-Ms", "/system/bin/sh", "2000", "ls"}, {"su", "-Ms", "/system/bin/sh", "2000"},
           "ls", {});
    expect({"su", "-g", "1000", "2000", "ls"}, {"su", "-g", "1000", "2000"}, "ls", {});
    expect({"su", "-G", "1000", "2000", "ls"}, {"su", "-G", "1000", "2000"}, "ls", {});
    // "--opt=value" carries its own value, so the token after it really is the user.
    expect({"su", "--context=ctx", "2000", "ls"}, {"su", "--context=ctx", "2000"}, "ls", {});
    // The -Z entry PR #3653 adds upstream: without it "2000" would be exec'd as a program.
    expect_options_only({"su", "-Z", "ctx", "2000"}, {"su", "-Z", "ctx", "2000"});
}

void test_legacy_aliases() {
    expect({"su", "-mm", "2000", "ls"}, {"su", "-M", "2000"}, "ls", {});
    expect({"su", "-cn", "ctx", "2000", "ls"}, {"su", "-z", "ctx", "2000"}, "ls", {});
    expect_options_only({"su", "-mm", "-c", "id"}, {"su", "-M", "-c", "id"});
    // A positional command's own arguments are never rewritten.
    expect({"su", "2000", "mycmd", "-mm"}, {"su", "2000"}, "mycmd", {"-mm"});
    expect({"su", "2000", "mycmd", "-cn"}, {"su", "2000"}, "mycmd", {"-cn"});
    // Nor is the -c command string.
    expect_options_only({"su", "-c", "mycmd", "-mm"}, {"su", "-c", "mycmd -mm"});
}

// Whichever of a positional command and -c comes first wins.
void test_command_precedence() {
    expect_options_only({"su", "-c", "ls", "2000", "foo"}, {"su", "-c", "ls 2000 foo"});
    expect({"su", "2000", "ls", "-c", "id"}, {"su", "2000"}, "ls", {"-c", "id"});
    // "--command=" is self-contained, so it is not merged, but it still wins the ordering.
    expect_options_only({"su", "--command=foo", "2000", "ls"},
                        {"su", "--command=foo", "2000", "ls"});
}

void test_odd_shapes() {
    expect_options_only({"su", "--", "-l"}, {"su", "--", "-l"});
    expect_options_only({"su", "2000", "-"}, {"su", "2000", "-"});
    expect_options_only({"su", "-c"}, {"su", "-c"});
    expect({"su", "a", "b"}, {"su", "a"}, "b", {});
}

void test_uid_resolution() {
    assert(args::resolve_uid("shell", 2000) == 2000);
    assert(args::resolve_uid("123", 456) == 456);
    assert(args::resolve_uid("0", std::nullopt) == 0);
    assert(args::resolve_uid("4294967294", std::nullopt) == UINT32_MAX - 1);

    assert(!args::resolve_uid("", std::nullopt).has_value());
    assert(!args::resolve_uid("missing", std::nullopt).has_value());
    assert(!args::resolve_uid("-1", std::nullopt).has_value());
    assert(!args::resolve_uid("12x", std::nullopt).has_value());
    assert(!args::resolve_uid("4294967295", std::nullopt).has_value());
    assert(!args::resolve_uid("4294967296", std::nullopt).has_value());
    assert(!args::resolve_uid("invalid", UINT32_MAX).has_value());
}

void test_numeric_id_parsing() {
    assert(args::parse_numeric_id("0") == 0);
    assert(args::parse_numeric_id("4294967294") == UINT32_MAX - 1);
    assert(!args::parse_numeric_id("").has_value());
    assert(!args::parse_numeric_id("-1").has_value());
    assert(!args::parse_numeric_id("12x").has_value());
    assert(!args::parse_numeric_id("4294967295").has_value());
    assert(!args::parse_numeric_id("4294967296").has_value());
}

void expect_identity(std::optional<std::uint32_t> uid, std::optional<std::uint32_t> gid,
                     std::optional<std::uint32_t> first_group, std::uint32_t current_uid,
                     bool requested, std::uint32_t expected_uid, std::uint32_t expected_gid) {
    const args::IdentityPlan plan = args::make_identity_plan(uid, gid, first_group, current_uid);
    assert(plan.requested == requested);
    assert(plan.uid == expected_uid);
    assert(plan.gid == expected_gid);
}

void test_identity_plan() {
    expect_identity(std::nullopt, std::nullopt, std::nullopt, 2000, false, 2000, 2000);
    expect_identity(0, std::nullopt, std::nullopt, 2000, true, 0, 0);
    expect_identity(1000, std::nullopt, std::nullopt, 2000, true, 1000, 1000);
    expect_identity(std::nullopt, 0, std::nullopt, 2000, true, 2000, 0);
    expect_identity(std::nullopt, 3000, std::nullopt, 2000, true, 2000, 3000);
    expect_identity(std::nullopt, std::nullopt, 0, 2000, true, 2000, 0);
    expect_identity(std::nullopt, std::nullopt, 4000, 2000, true, 2000, 4000);
    expect_identity(1000, 3000, 4000, 2000, true, 1000, 3000);
}

}  // namespace

int main() {
    test_takes_separate_value();
    test_reported_failures();
    test_no_regression();
    test_positional_command();
    test_value_option_not_mistaken_for_user();
    test_legacy_aliases();
    test_command_precedence();
    test_odd_shapes();
    test_uid_resolution();
    test_numeric_id_parsing();
    test_identity_plan();
    std::cout << "su_args_test: all checks passed\n";
    return 0;
}
