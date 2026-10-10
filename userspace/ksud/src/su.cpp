#include "su.hpp"
#include "core/ksucalls.hpp"
#include "defs.hpp"
#include "log.hpp"
#include "su_args.hpp"
#include "terminal.hpp"
#include "utils.hpp"

#include <fcntl.h>
#include <getopt.h>
#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace ksud {

namespace {

int su_usage_error(std::string_view message, std::string_view usage) {
    (void)terminal::usage_error(message, usage);
    return 1;
}

void setup_shell_rc(const std::string& shell, bool preserve_env) {
    if (isatty(STDIN_FILENO) != 1 || isatty(STDERR_FILENO) != 1)
        return;
    if (shell != "/system/bin/sh" && shell != "/system/bin/mksh" &&
        shell != "/data/adb/ksu/bin/sh" && shell != "/data/adb/ksu/bin/ash")
        return;
    const char* env = getenv("ENV");
    if (preserve_env && (!env || strcmp(env, SHELL_RC_PATH) != 0))
        return;
    if (env && strcmp(env, FEATURE_CONFIG_PATH) != 0 && strcmp(env, SHELL_RC_PATH) != 0)
        return;
    if (env && !preserve_env)
        unsetenv("ENV");
    const int rc = open(SHELL_RC_PATH, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (rc < 0)
        return;
    struct stat status{};
    const bool readable = fstat(rc, &status) == 0 && S_ISREG(status.st_mode);
    close(rc);
    if (!readable)
        return;
    const char* prompt = getenv("PS1");
    if (prompt)
        setenv("KSU_SHELL_PS1", prompt, 1);
    else
        unsetenv("KSU_SHELL_PS1");
    if (!preserve_env)
        setenv("ENV", SHELL_RC_PATH, 1);
}

void print_su_usage() {
    printf("ZySU\n\n");
    terminal::heading(stdout, "Usage: ");
    printf("su [options] [-] [user [command [argument...]]]\n\n");
    printf("Options may be given before or after user. A positional command is exec'd\n");
    printf("directly, not through the shell; use -c to run something through the shell.\n\n");
    terminal::heading(stdout, "Options:\n");
    printf("  -c, --command COMMAND    pass COMMAND to the invoked shell\n");
    printf("  -h, --help               display this help message and exit\n");
    printf("  -l, --login              pretend the shell to be a login shell\n");
    printf("  -p, --preserve-environment  preserve HOME, USER, LOGNAME and SHELL\n");
    printf("  -s, --shell SHELL        use SHELL instead of the default\n");
    printf("  -v, --version            display version number and exit\n");
    printf("  -V                       display version code and exit\n");
    printf("  -M, -mm, --mount-master  force run in the global mount namespace\n");
    printf("  -g, --group GROUP        specify the primary group\n");
    printf("  -G, --supp-group GROUP   specify a supplementary group\n");
    printf("  -W, --no-wrapper         don't use ksu fd wrapper\n");
    printf("  -Z, -z, --context CONTEXT  run in the given SELinux context\n");
    printf("      --ksu-no-new-privs   block this process and its children from re-escalating\n");
}

bool set_identity(uid_t uid, gid_t gid, const std::vector<gid_t>& groups) {
    if (setgroups(groups.size(), groups.empty() ? nullptr : groups.data()) != 0) {
        LOGE("Failed to set supplementary groups: %s", strerror(errno));
        return false;
    }
    // Only change creds when not already the target (when already root, no setres* — avoids
    // seccomp).
    if (getegid() != gid && setresgid(gid, gid, gid) != 0) {
        LOGE("Failed to setresgid %u: %s", gid, strerror(errno));
        return false;
    }
    if (geteuid() != uid && setresuid(uid, uid, uid) != 0) {
        LOGE("Failed to setresuid %u: %s", uid, strerror(errno));
        return false;
    }
    return true;
}

// Dyntransition this thread; the program exec'd afterwards inherits the context.
bool set_selinux_context(const std::string& context) {
    if (context.empty()) {
        LOGE("Empty SELinux context");
        return false;
    }
    const int fd = open("/proc/thread-self/attr/current", O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        LOGE("Failed to open attr/current: %s", strerror(errno));
        return false;
    }
    const ssize_t written = write(fd, context.c_str(), context.size());
    const int write_errno = errno;
    close(fd);
    if (written != static_cast<ssize_t>(context.size())) {
        LOGE("Failed to set SELinux context %s: %s", context.c_str(),
             written < 0 ? strerror(write_errno) : "short write");
        return false;
    }
    return true;
}

void wrap_tty(int fd) {
    errno = 0;
    if (isatty(fd) != 1 && errno != EACCES) {
        return;
    }
    const int new_fd = get_wrapped_fd(fd);
    if (new_fd < 0) {
        LOGW("Failed to get wrapped fd for %d", fd);
        return;
    }
    if (isatty(new_fd) != 1) {
        close(new_fd);
        return;
    }
    const int dup_result = dup2(new_fd, fd);
    const int dup_errno = errno;
    close(new_fd);
    if (dup_result == -1) {
        LOGW("Failed to dup %d -> %d: %s", new_fd, fd, strerror(dup_errno));
    }
}

}  // namespace

int su_main(int argc, char** argv) {
    // sucompat applies the selected profile before exec and installs a scoped
    // driver fd once that exec succeeds. Older kernels may not provide the fd,
    // but have already applied the profile as well.
    const int claim_result = claim_inherited_su_driver_fd();
    if (claim_result < 0) {
        LOGE("Failed to scan inherited driver fds: %s", strerror(-claim_result));
        return 1;
    }
    return run_su_shell(argc, argv);
}

int run_su_shell(int argc, char** argv) {
    // Parse options
    std::string command;
    bool has_command = false;
    std::string shell = "/system/bin/sh";  // Use system shell by default (like Rust version)
    bool is_login = false;
    bool preserve_env = false;
    bool mount_master = false;
    bool use_fd_wrapper = true;
    bool ksu_no_new_privs = false;
    std::optional<std::string> selinux_context;
    std::optional<std::uint32_t> requested_gid;
    std::vector<gid_t> groups;

    // Split off any positional command and order the remaining options ahead of the operands, so
    // options work on either side of the username.
    const su_args::ParsedArgv parsed = su_args::split(std::vector<std::string>(argv, argv + argc));

    std::vector<char*> new_argv;
    new_argv.reserve(parsed.option_argv.size() + 1);
    for (const auto& s : parsed.option_argv) {
        new_argv.push_back(const_cast<char*>(s.c_str()));
    }
    new_argv.push_back(nullptr);
    argc = static_cast<int>(new_argv.size() - 1);
    argv = new_argv.data();

    constexpr int OPT_KSU_NO_NEW_PRIVS = 0x100;  // long-only option id
    static const std::array<struct option, 13> long_options = {{
        {"command", required_argument, nullptr, 'c'},
        {"help", no_argument, nullptr, 'h'},
        {"login", no_argument, nullptr, 'l'},
        {"preserve-environment", no_argument, nullptr, 'p'},
        {"shell", required_argument, nullptr, 's'},
        {"version", no_argument, nullptr, 'v'},
        {"mount-master", no_argument, nullptr, 'M'},
        {"group", required_argument, nullptr, 'g'},
        {"supp-group", required_argument, nullptr, 'G'},
        {"no-wrapper", no_argument, nullptr, 'W'},
        {"context", required_argument, nullptr, 'z'},
        {"ksu-no-new-privs", no_argument, nullptr, OPT_KSU_NO_NEW_PRIVS},
        {nullptr, 0, nullptr, 0},
    }};

    optind = 1;  // Reset getopt
    opterr = 0;
    int opt;
    // Keep the leading "+": su_args::split has already moved every option ahead of the operands,
    // so stopping at the first operand is correct and does not depend on libc permuting argv.
    while ((opt = getopt_long(argc, argv, "+:c:hlps:vVMg:G:Wz:Z:", long_options.data(), nullptr)) !=
           -1) {
        switch (opt) {
        case 'c':
            command = optarg;
            has_command = true;
            break;
        case 'h':
            print_su_usage();
            return 0;
        case 'l':
            is_login = true;
            break;
        case 'p':
            preserve_env = true;
            break;
        case 's':
            shell = optarg;
            break;
        case 'v':
            printf("%s:KernelSU\n", VERSION_NAME);
            return 0;
        case 'V':
            printf("%s\n", VERSION_CODE);
            return 0;
        case 'M':
            mount_master = true;
            break;
        case 'g': {
            requested_gid = su_args::parse_numeric_id(optarg);
            if (!requested_gid.has_value()) {
                return su_usage_error("invalid GID '" + std::string(optarg) + "'",
                                      "su --group <GID>");
            }
            break;
        }
        case 'G': {
            const auto group = su_args::parse_numeric_id(optarg);
            if (!group.has_value()) {
                return su_usage_error("invalid supplementary GID '" + std::string(optarg) + "'",
                                      "su --supp-group <GID>");
            }
            groups.push_back(static_cast<gid_t>(*group));
            break;
        }
        case 'W':
            use_fd_wrapper = false;
            break;
        // -Z matches upstream ksud, -z (and the legacy -cn alias) matches Magisk su.
        case 'z':
        case 'Z':
            selinux_context = optarg;
            break;
        case OPT_KSU_NO_NEW_PRIVS:
            ksu_no_new_privs = true;
            break;
        case ':':
            return su_usage_error("option '" + std::string(argv[optind - 1]) + "' requires a value",
                                  "su [OPTIONS] [-] [USER [COMMAND...]]");
        default:
            return su_usage_error("unknown option '" + std::string(argv[optind - 1]) + "'",
                                  "su [OPTIONS] [-] [USER [COMMAND...]]");
        }
    }

    // Check for "-" meaning login shell
    if (optind < argc && strcmp(argv[optind], "-") == 0) {
        is_login = true;
        optind++;
    }

    std::optional<std::uint32_t> requested_uid;
    if (optind < argc) {
        const char* user = argv[optind];
        const struct passwd* pw = getpwnam(user);
        const std::optional<std::uint32_t> passwd_uid =
            pw == nullptr ? std::nullopt
                          : std::optional<std::uint32_t>(static_cast<std::uint32_t>(pw->pw_uid));
        requested_uid = su_args::resolve_uid(user, passwd_uid);
        if (!requested_uid.has_value()) {
            return terminal::error("unknown user '" + std::string(user) + "'",
                                   "Use a username or a decimal UID.");
        }
    }

    const std::optional<std::uint32_t> first_group =
        groups.empty() ? std::nullopt
                       : std::optional<std::uint32_t>(static_cast<std::uint32_t>(groups[0]));
    const su_args::IdentityPlan identity = su_args::make_identity_plan(
        requested_uid, requested_gid, first_group, static_cast<std::uint32_t>(getuid()));
    const uid_t target_uid = static_cast<uid_t>(identity.uid);
    const gid_t target_gid = static_cast<gid_t>(identity.gid);

    // Switch to global mount namespace if requested
    if (mount_master) {
        if (!switch_mnt_ns(1)) {
            LOGW("Failed to switch to global mount namespace");
        }
    }

    // Wrap tty fds if requested
    if (use_fd_wrapper) {
        wrap_tty(0);
        wrap_tty(1);
        wrap_tty(2);
    }

    // Lock this process and its children out of any further escalation.
    if (ksu_no_new_privs && set_ksu_no_new_privs() != 0) {
        LOGE("Failed to set KSU_NO_NEW_PRIVS");
        return 1;
    }

    // Switch cgroups
    switch_cgroups();

    // Set environment
    setenv("ASH_STANDALONE", "1", 1);

    // Prepend /data/adb/ksu/bin to PATH
    const char* old_path = getenv("PATH");
    std::string new_path = "/data/adb/ksu/bin";
    if (old_path && old_path[0] != '\0') {
        new_path = new_path + ":" + old_path;
    }
    setenv("PATH", new_path.c_str(), 1);

    if (!preserve_env) {
        const struct passwd* pw = getpwuid(target_uid);
        if (pw) {
            setenv("HOME", pw->pw_dir, 1);
            setenv("USER", pw->pw_name, 1);
            setenv("LOGNAME", pw->pw_name, 1);
            setenv("SHELL", shell.c_str(), 1);
        } else {
            const std::string uid_name = std::to_string(target_uid);
            setenv("HOME", "/data", 1);
            setenv("USER", uid_name.c_str(), 1);
            setenv("LOGNAME", uid_name.c_str(), 1);
            setenv("SHELL", shell.c_str(), 1);
        }
    }

    umask(022);
    if (identity.requested && !set_identity(target_uid, target_gid, groups)) {
        return 1;
    }

    // Change credentials while still in the unrestricted su domain.
    if (selinux_context.has_value() && !set_selinux_context(*selinux_context)) {
        return 1;
    }

    if (!has_command && !parsed.executable.has_value())
        setup_shell_rc(shell, preserve_env);

    // A positional command wins over -c; su_args::split already resolved which one came first.
    const std::string executable = parsed.executable.value_or(shell);
    // The login arg0 convention only applies to the shell: a positional command must keep its own
    // name, since busybox applets and ksud itself dispatch on the argv[0] basename.
    const std::string arg0 = (is_login && !parsed.executable.has_value()) ? "-" : executable;

    std::vector<const char*> exec_argv;
    exec_argv.push_back(arg0.c_str());

    if (parsed.executable.has_value()) {
        for (const auto& arg : parsed.exec_args) {
            exec_argv.push_back(arg.c_str());
        }
    } else if (has_command) {
        exec_argv.push_back("-c");
        exec_argv.push_back(command.c_str());
    }

    exec_argv.push_back(nullptr);

    execvp(executable.c_str(), const_cast<char* const*>(exec_argv.data()));

    (void)terminal::file_error("cannot execute", executable, errno);
    return 127;
}

// Legacy functions for backward compatibility
int root_shell() {
    std::array<char*, 2> argv = {const_cast<char*>("su"), nullptr};
    return su_main(1, argv.data());
}

// Keep the debug shell's credential path compatible with seccomp-constrained callers.
int grant_root_shell(bool global_mnt) {
    // Grant root first via kernel
    if (grant_root() < 0) {
        LOGE("Failed to grant root");
        return 1;
    }

    // Only set uid/gid when not already root (su binary + ZySU kernel path). When already root
    // (e.g. IcePatch), skip to avoid seccomp SIGSYS.
    if (geteuid() != 0 || getegid() != 0) {
        setgid(0);
        setuid(0);
    }

    // Switch to global mount namespace if requested
    if (global_mnt && !switch_mnt_ns(1)) {
        LOGW("Failed to switch to global mount namespace");
    }

    // Add /data/adb/ksu/bin to PATH
    const char* old_path = getenv("PATH");
    std::string new_path = "/data/adb/ksu/bin";
    if (old_path && old_path[0]) {
        new_path += ":";
        new_path += old_path;
    }
    setenv("PATH", new_path.c_str(), 1);

    setup_shell_rc("/system/bin/sh", false);
    std::array<char*, 2> shell_argv = {const_cast<char*>("sh"), nullptr};
    execv("/system/bin/sh", shell_argv.data());

    LOGE("Failed to exec shell: %s", strerror(errno));
    return 127;
}

}  // namespace ksud
