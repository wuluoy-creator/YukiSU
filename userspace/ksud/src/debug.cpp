#include "debug.hpp"
#include <climits>
#include "core/ksucalls.hpp"
#include "kernelsu_loader.hpp"
#include "log.hpp"
#include "terminal.hpp"
#include "utils.hpp"

#include <sys/stat.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace ksud {

static constexpr const char* KERNEL_PARAM_PATH = "/sys/module/kernelsu/parameters";

namespace {

bool read_u32(const std::string& path, uint32_t& value) {
    const auto content = read_file(path);
    return content && parse_uint32(trim_view(*content), &value);
}

bool write_u32(const std::string& path, uint32_t value) {
    return write_file(path, std::to_string(value));
}

bool get_pkg_uid(const std::string& pkg, uint32_t& uid) {
    const std::string data_path = "/data/data/" + pkg;
    struct stat st{};
    if (stat(data_path.c_str(), &st) != 0) {
        terminal::errorf("Failed to stat %s: %s\n", data_path.c_str(), strerror(errno));
        return false;
    }
    uid = st.st_uid;
    return true;
}

}  // namespace

int debug_set_manager(const std::string& pkg) {
    // Check if CONFIG_KSU_DEBUG is enabled
    std::error_code path_error;
    if (!std::filesystem::exists(KERNEL_PARAM_PATH, path_error) || path_error) {
        printf("CONFIG_KSU_DEBUG is not enabled in kernel\n");
        return 1;
    }

    // Get package UID
    uint32_t uid;
    if (!get_pkg_uid(pkg, uid)) {
        terminal::errorf("Failed to get UID for package: %s\n", pkg.c_str());
        return 1;
    }

    printf("Package %s has UID: %u\n", pkg.c_str(), uid);

    // Set manager UID via kernel parameter
    const std::string param_path = std::string(KERNEL_PARAM_PATH) + "/ksu_debug_manager_uid";

    uint32_t before_uid = 0;
    read_u32(param_path, before_uid);

    if (!write_u32(param_path, uid)) {
        terminal::errorf("Failed to write manager UID to kernel parameter\n");
        return 1;
    }

    uint32_t after_uid = 0;
    read_u32(param_path, after_uid);

    printf("Set manager UID: %u -> %u\n", before_uid, after_uid);

    // Force-stop the package to apply changes
    printf("Force-stopping package...\n");
    const auto stop_result = exec_command({"am", "force-stop", pkg});
    if (stop_result.exit_code != 0) {
        LOGW("Failed to force-stop %s: %s", pkg.c_str(), stop_result.stderr_str.c_str());
    }

    printf("Manager set successfully!\n");
    return 0;
}

int debug_insmod(const std::string& module, const std::vector<std::string>& params) {
    std::error_code ec;
    const auto resolved_path = std::filesystem::canonical(module, ec);
    if (ec) {
        terminal::errorf("Failed to resolve module path %s: %s\n", module.c_str(),
                         ec.message().c_str());
        return 1;
    }

    std::string param_values;
    for (size_t i = 0; i < params.size(); ++i) {
        if (i != 0) {
            param_values.push_back(' ');
        }
        param_values += params[i];
    }

    if (!kernelsu_loader::load_module(resolved_path.c_str(), param_values)) {
        terminal::errorf("Failed to load kernel module: %s\n", resolved_path.c_str());
        return 1;
    }

    printf("Loaded kernel module: %s\n", resolved_path.c_str());
    return 0;
}

int debug_mark(const std::vector<std::string>& args) {
    if (args.empty()) {
        printf("Usage: ksud debug mark <get|mark|unmark|refresh> [PID]\n");
        return 1;
    }

    const std::string& cmd = args[0];
    uint32_t parsed_pid = 0;
    if (args.size() > 1 && (!parse_uint32(args[1], &parsed_pid) || parsed_pid > INT_MAX))
        return terminal::usage_error("invalid PID: expected an integer between 0 and 2147483647",
                                     "ksud debug mark <get|mark|unmark> [PID]");
    const int32_t pid = static_cast<int32_t>(parsed_pid);

    if (cmd == "get") {
        const uint32_t result = mark_get(pid);
        if (pid == 0) {
            printf("Total marked processes: %u\n", result);
        } else {
            printf("Process %d is %s\n", pid, result ? "marked" : "not marked");
        }
        return 0;
    } else if (cmd == "mark") {
        if (mark_set(pid) < 0) {
            terminal::errorf("Failed to mark process %d\n", pid);
            return 1;
        }
        printf("Marked process %d\n", pid);
        return 0;
    } else if (cmd == "unmark") {
        if (mark_unset(pid) < 0) {
            terminal::errorf("Failed to unmark process %d\n", pid);
            return 1;
        }
        printf("Unmarked process %d\n", pid);
        return 0;
    } else if (cmd == "refresh") {
        if (mark_refresh() < 0) {
            terminal::errorf("Failed to refresh marks\n");
            return 1;
        }
        printf("Refreshed all process marks\n");
        return 0;
    }

    terminal::errorf("Unknown mark command: %s\n", cmd.c_str());
    return 1;
}

}  // namespace ksud
