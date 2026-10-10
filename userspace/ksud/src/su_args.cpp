#include "su_args.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ksud::su_args {

namespace {

// Mirrors the value-taking letters of run_su_shell's getopt optstring.
constexpr char kValueShortOpts[] = "csgGzZ";

bool is_dash_prefixed(const std::string& arg) {
    return !arg.empty() && arg.front() == '-';
}

// Index of the first option supplying the shell command, or npos.
std::size_t find_command_option(const std::vector<std::string>& argv) {
    for (std::size_t i = 1; i < argv.size(); ++i) {
        if (argv[i] == "-c" || argv[i] == "--command" || argv[i].rfind("--command=", 0) == 0) {
            return i;
        }
    }
    return std::string::npos;
}

// Index of the target user in the "user command [argument...]" form, or npos. A positional command
// is only recognized when two adjacent operands appear and the token before them is not an option
// claiming the first of them as its value. argv[0] takes part in the first window, so the user can
// be argv[1] - which also means the form requires an explicit user.
std::size_t find_user_before_command(const std::vector<std::string>& argv) {
    for (std::size_t i = 1; i + 1 < argv.size(); ++i) {
        if (!takes_separate_value(argv[i - 1]) && !is_dash_prefixed(argv[i]) &&
            !is_dash_prefixed(argv[i + 1])) {
            return i;
        }
    }
    return std::string::npos;
}

std::string join_with_spaces(const std::vector<std::string>& argv, std::size_t begin) {
    std::string joined;
    for (std::size_t i = begin; i < argv.size(); ++i) {
        if (i != begin) {
            joined += ' ';
        }
        joined += argv[i];
    }
    return joined;
}

}  // namespace

bool takes_separate_value(const std::string& arg) {
    if (arg == "-cn") {  // legacy Magisk alias for --context
        return true;
    }
    if (arg.rfind("--", 0) == 0) {
        // "--opt=value" carries its own value, so only the separated spelling matters here.
        static constexpr std::array<const char*, 5> kValueLongOpts = {
            "--command", "--shell", "--group", "--supp-group", "--context"};
        return std::any_of(kValueLongOpts.begin(), kValueLongOpts.end(),
                           [&arg](const char* opt) { return arg == opt; });
    }
    if (arg.size() < 2 || arg.front() != '-') {
        return false;
    }
    for (std::size_t i = 1; i < arg.size(); ++i) {
        if (std::strchr(kValueShortOpts, arg[i]) != nullptr) {
            // A value-taking letter claims the next argv element only when it ends the cluster;
            // otherwise the rest of the cluster is its value.
            return i + 1 == arg.size();
        }
    }
    return false;
}

ParsedArgv split(const std::vector<std::string>& argv) {
    ParsedArgv parsed;
    if (argv.empty()) {
        return parsed;
    }

    const std::size_t command_option = find_command_option(argv);
    const std::size_t user = find_user_before_command(argv);

    // Whichever comes first wins. A positional command truncates the option region at the user, so
    // a later -c is never parsed; otherwise -c claims everything after it.
    std::size_t option_end = argv.size();
    if (user < command_option) {
        option_end = user + 1;
        parsed.executable = argv[user + 1];
        parsed.exec_args.assign(argv.begin() + static_cast<std::ptrdiff_t>(user) + 2, argv.end());
    }

    // Fold a -c command and its arguments into one token and rewrite legacy aliases. Both are
    // confined to the option region, so a positional command's own arguments stay verbatim.
    std::vector<std::string> region;
    for (std::size_t i = 1; i < option_end; ++i) {
        if (i == command_option && (argv[i] == "-c" || argv[i] == "--command")) {
            region.push_back(argv[i]);
            if (i + 1 < argv.size()) {
                region.push_back(join_with_spaces(argv, i + 1));
            }
            break;
        }
        if (argv[i] == "-mm") {
            region.emplace_back("-M");
        } else if (argv[i] == "-cn") {
            region.emplace_back("-z");
        } else {
            region.push_back(argv[i]);
        }
    }

    // Order options ahead of operands, preserving each group's relative order. getopt then sees
    // every option before it stops at the first operand, so parsing does not depend on whether
    // libc permutes argv.
    std::vector<std::string> operands;
    parsed.option_argv.push_back(argv[0]);
    bool after_ddash = false;
    for (std::size_t i = 0; i < region.size(); ++i) {
        if (after_ddash) {
            operands.push_back(region[i]);
        } else if (region[i] == "--") {
            parsed.option_argv.push_back(region[i]);
            after_ddash = true;
        } else if (region[i].size() > 1 && region[i].front() == '-') {
            parsed.option_argv.push_back(region[i]);
            if (takes_separate_value(region[i]) && i + 1 < region.size()) {
                parsed.option_argv.push_back(region[i + 1]);
                ++i;
            }
        } else {
            operands.push_back(region[i]);
        }
    }
    parsed.option_argv.insert(parsed.option_argv.end(), operands.begin(), operands.end());

    return parsed;
}

std::optional<std::uint32_t> parse_numeric_id(std::string_view value) {
    if (value.empty()) {
        return std::nullopt;
    }

    std::uint32_t result = 0;
    for (const char ch : value) {
        if (ch < '0' || ch > '9') {
            return std::nullopt;
        }
        const std::uint32_t digit = static_cast<std::uint32_t>(ch - '0');
        if (result > (std::numeric_limits<std::uint32_t>::max() - digit) / 10) {
            return std::nullopt;
        }
        result = result * 10 + digit;
    }
    if (result == std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    return result;
}

std::optional<std::uint32_t> resolve_uid(std::string_view user,
                                         std::optional<std::uint32_t> passwd_uid) {
    if (passwd_uid.has_value()) {
        return *passwd_uid == std::numeric_limits<std::uint32_t>::max() ? std::nullopt : passwd_uid;
    }
    return parse_numeric_id(user);
}

IdentityPlan make_identity_plan(std::optional<std::uint32_t> uid, std::optional<std::uint32_t> gid,
                                std::optional<std::uint32_t> first_group,
                                std::uint32_t current_uid) {
    const bool requested = uid.has_value() || gid.has_value() || first_group.has_value();
    const std::uint32_t resolved_uid = uid.value_or(current_uid);
    const std::uint32_t resolved_gid = gid.value_or(first_group.value_or(resolved_uid));
    return {requested, resolved_uid, resolved_gid};
}

}  // namespace ksud::su_args
