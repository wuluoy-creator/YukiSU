// Production code is extracted by run_script_wait_host_tests.py. Process and
// signal calls are controlled here so lost wakeups, unrelated children, and
// failures can be exercised deterministically without Android or live modules.
#include "module/module.hpp"

#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#ifdef WIFEXITED
#undef WIFEXITED
#undef WEXITSTATUS
#endif
#ifdef WNOHANG
#undef WNOHANG
#endif

namespace ksud {
void apply_common_script_env(const CommonScriptEnv&, const char*, bool) {}
}  // namespace ksud

namespace fixture {
using ksud::CommonScriptEnv;
using ksud::ScriptWait;
using namespace std::chrono_literals;
using pid_t = int;
using ssize_t = long long;
using time_t = long long;
using sigset_t = uint64_t;
struct timespec {
    time_t tv_sec;
    long tv_nsec;
};
constexpr int SIGCHLD = 17, SIG_BLOCK = 0, SIG_SETMASK = 2;
constexpr int WNOHANG = 1, O_CLOEXEC = 0x80000;
constexpr const char* BUSYBOX_PATH = "/test/busybox";
constexpr const char* SCRIPT = "/test/module/post-fs-data.sh";
constexpr pid_t CHILD = 42;
constexpr sigset_t CHILD_SIGNAL = sigset_t{1} << SIGCHLD;
constexpr sigset_t ORIGINAL_MASK = sigset_t{1} << 10;

sigset_t signal_mask;
int forks, waits, signal_waits, restorations, warnings, errors, reads;
int block_error, pipe_error, fork_error, read_error, child_error, restore_error, written_error;
int child_exit_status;
bool child_branch, script_exists;
std::vector<int> wait_results, signal_results;
std::vector<int> wait_options, closed;
std::vector<std::chrono::nanoseconds> timeouts;
sigset_t exec_mask;
ScriptWait::Clock::time_point expire_during_spawn, expire_during_wait;

void reset() {
    signal_mask = ORIGINAL_MASK;
    forks = waits = signal_waits = restorations = warnings = errors = reads = 0;
    block_error = pipe_error = fork_error = read_error = child_error = 0;
    restore_error = written_error = 0;
    expire_during_spawn = expire_during_wait = {};
    child_exit_status = 0;
    child_branch = false;
    script_exists = true;
    exec_mask = 0;
    wait_results.clear();
    signal_results.clear();
    wait_options.clear();
    closed.clear();
    timeouts.clear();
}

template <typename... Args>
void log_info(const char*, Args...) {}
template <typename... Args>
void log_warning(const char*, Args...) {
    ++warnings;
}
template <typename... Args>
void log_error(const char*, Args...) {
    ++errors;
}
#define LOGI(...) log_info(__VA_ARGS__)
#define LOGW(...) log_warning(__VA_ARGS__)
#define LOGE(...) log_error(__VA_ARGS__)

int sigemptyset(sigset_t* set) {
    *set = 0;
    return 0;
}
int sigaddset(sigset_t* set, int signal) {
    *set |= sigset_t{1} << signal;
    return 0;
}
int sigprocmask(int how, const sigset_t* set, sigset_t* previous) {
    if (how == SIG_SETMASK && restore_error) {
        errno = restore_error;
        restore_error = 0;
        return -1;
    }
    if (how == SIG_BLOCK && block_error) {
        errno = block_error;
        return -1;
    }
    if (previous)
        *previous = signal_mask;
    if (how == SIG_BLOCK)
        signal_mask |= *set;
    else {
        assert(how == SIG_SETMASK);
        ++restorations;
        signal_mask = *set;
    }
    return 0;
}

pid_t waitpid(pid_t pid, int* status, int options) {
    assert(pid == CHILD);  // Never reap a logger or another module's child.
    assert((signal_mask & CHILD_SIGNAL) != 0);
    assert(static_cast<size_t>(waits) < wait_results.size());
    wait_options.push_back(options);
    const int result = wait_results[waits++];
    if (result < 0) {
        errno = -result;
        return -1;
    }
    if (result == CHILD && status)
        *status = child_exit_status;
    return result;
}
int sigtimedwait(const sigset_t* set, void*, const timespec* timeout) {
    assert(*set == CHILD_SIGNAL);
    assert((signal_mask & CHILD_SIGNAL) != 0);  // No unblocked lost-wakeup window.
    assert(static_cast<size_t>(signal_waits) < signal_results.size());
    assert(timeout->tv_sec >= 0 && timeout->tv_nsec >= 0 && timeout->tv_nsec < 1000000000L);
    timeouts.push_back(std::chrono::seconds(timeout->tv_sec) +
                       std::chrono::nanoseconds(timeout->tv_nsec));
    if (expire_during_wait != ScriptWait::Clock::time_point{})
        std::this_thread::sleep_until(expire_during_wait);
    const int result = signal_results[signal_waits++];
    if (result < 0) {
        errno = -result;
        return -1;
    }
    return result;
}
bool WIFEXITED(int status) {
    return (status & 0x7f) == 0;
}
int WEXITSTATUS(int status) {
    return (status >> 8) & 0xff;
}
bool file_exists(const std::string& path) {
    return path == BUSYBOX_PATH || (path == SCRIPT && script_exists);
}
CommonScriptEnv build_common_script_env() {
    return {};
}
void detach_process_group(bool use_init_pgrp) {
    assert(use_init_pgrp);
}
void switch_cgroups() {}
int pipe2(int* fds, int flags) {
    assert(flags == O_CLOEXEC);
    if (pipe_error) {
        errno = pipe_error;
        return -1;
    }
    fds[0] = 3;
    fds[1] = 4;
    return 0;
}
pid_t fork() {
    ++forks;
    assert((signal_mask & CHILD_SIGNAL) != 0);
    if (expire_during_spawn != ScriptWait::Clock::time_point{})
        std::this_thread::sleep_until(expire_during_spawn);
    if (fork_error) {
        errno = fork_error;
        return -1;
    }
    return child_branch ? 0 : CHILD;
}
int close(int fd) {
    closed.push_back(fd);
    return 0;
}
ssize_t read(int fd, void* data, size_t size) {
    assert(fd == 3 && size == sizeof(int));
    ++reads;
    if (read_error) {
        errno = read_error;
        read_error = 0;
        return -1;
    }
    if (child_error) {
        std::memcpy(data, &child_error, sizeof(child_error));
        return sizeof(child_error);
    }
    return 0;
}
ssize_t write(int fd, const void* data, size_t size) {
    assert(fd == 4 && size == sizeof(written_error));
    std::memcpy(&written_error, data, size);
    return size;
}
int chdir(const char*) {
    return 0;
}
int setenv(const char*, const char*, int) {
    return 0;
}
int execl(const char*, const char*, const char*, std::nullptr_t) {
    exec_mask = signal_mask;
    errno = ENOENT;
    return -1;
}
struct ChildExit {};
[[noreturn]] void _exit(int status) {
    assert(status == 127);
    throw ChildExit{};
}

#include "script_wait_under_test.inc"

int run(ScriptWait wait) {
    return fixture::run_script(SCRIPT, wait, "module", nullptr, nullptr);
}
void restored() {
    assert(signal_mask == ORIGINAL_MASK);
    assert(restorations == 1);
}

void test_no_wait_and_expired_deadline() {
    for (const auto wait :
         {ScriptWait::no_wait(), ScriptWait::until(ScriptWait::Clock::now() - 1s)}) {
        reset();
        assert(run(wait) == 0);
        assert(forks == 1 && waits == 0 && signal_waits == 0 && warnings == 0);
        assert(reads == 1);  // Even NoWait completes the pre-exec handshake.
        restored();
    }
}

void test_exit_status_and_interruptions() {
    for (const bool forever : {false, true}) {
        reset();
        wait_results = {-EINTR, CHILD};
        child_exit_status = 7 << 8;
        const auto wait =
            forever ? ScriptWait::forever() : ScriptWait::until(ScriptWait::Clock::now() + 5s);
        assert(run(wait) == 7);
        assert(waits == 2 && warnings == 0);
        assert(wait_options == std::vector<int>(2, forever ? 0 : WNOHANG));
        restored();
    }
}

void test_timeout_does_not_reap_or_terminate() {
    reset();
    wait_results = {0};
    signal_results = {-EAGAIN};
    assert(run(ScriptWait::until(ScriptWait::Clock::now() + 5s)) == 0);
    assert(forks == 1 && waits == 1 && signal_waits == 1 && warnings == 1);
    assert(wait_options == std::vector<int>{WNOHANG});
    assert(timeouts[0] > 0ns && timeouts[0] <= 5s);
    // No kill API is supplied; timeout must return while the child is running.
    restored();
}

void test_shared_deadline_is_exhausted() {
    reset();
    const auto wait = ScriptWait::until(ScriptWait::Clock::now() + 500ms);
    expire_during_wait = wait.deadline;
    wait_results = {0};
    signal_results = {-EAGAIN};
    assert(run(wait) == 0);
    assert(waits == 1 && signal_waits == 1 && warnings == 1);
    assert(signal_mask == ORIGINAL_MASK);

    // Reusing this budget for another script or post-mount must not restart it.
    assert(run(wait) == 0);
    assert(forks == 2 && waits == 1 && signal_waits == 1 && warnings == 1);
    assert(signal_mask == ORIGINAL_MASK && restorations == 2);
}

void test_deadline_expires_during_spawn() {
    reset();
    const auto wait = ScriptWait::until(ScriptWait::Clock::now() + 50ms);
    expire_during_spawn = wait.deadline;
    assert(run(wait) == 0);
    assert(forks == 1 && reads == 1 && waits == 0 && signal_waits == 0 && warnings == 0);
    restored();
}

void test_unrelated_children_and_interrupted_signal_wait() {
    reset();
    wait_results = {0, 0, 0, CHILD};
    signal_results = {SIGCHLD, -EINTR, SIGCHLD};
    assert(run(ScriptWait::until(ScriptWait::Clock::now() + 5s)) == 0);
    assert(waits == 4 && signal_waits == 3 && warnings == 0);
    assert(timeouts[0] >= timeouts[1] && timeouts[1] >= timeouts[2]);
    restored();
}

void test_error_paths_restore_mask() {
    for (int failure = 0; failure < 6; ++failure) {
        reset();
        if (failure == 0)
            pipe_error = EMFILE;
        else if (failure == 1)
            fork_error = EAGAIN;
        else if (failure == 2)
            read_error = EIO;
        else if (failure == 3) {
            child_error = ENOENT;
            wait_results = {-EINTR, CHILD};
        } else if (failure == 4)
            wait_results = {-ECHILD};
        else {
            wait_results = {0};
            signal_results = {-EINVAL};
        }
        assert(run(ScriptWait::until(ScriptWait::Clock::now() + 5s)) == -1);
        assert(errors == 1);
        restored();
    }
    reset();
    block_error = EINVAL;
    assert(run(ScriptWait::forever()) == -1 && forks == 0);
    assert(signal_mask == ORIGINAL_MASK && restorations == 0);
}

void test_read_interruption_and_original_mask() {
    reset();
    read_error = EINTR;
    signal_mask |= CHILD_SIGNAL;
    assert(run(ScriptWait::no_wait()) == 0 && reads == 2);
    assert(signal_mask == (ORIGINAL_MASK | CHILD_SIGNAL) && restorations == 1);
}

void test_child_inherits_original_mask() {
    for (const auto original : {ORIGINAL_MASK, ORIGINAL_MASK | CHILD_SIGNAL}) {
        reset();
        signal_mask = original;
        child_branch = true;
        try {
            (void)run(ScriptWait::no_wait());
            assert(false);
        } catch (const ChildExit&) {
            assert(exec_mask == original);
        }
    }
}

void test_child_mask_restore_failure() {
    reset();
    child_branch = true;
    restore_error = EPERM;
    try {
        (void)run(ScriptWait::no_wait());
        assert(false);
    } catch (const ChildExit&) {
        assert(exec_mask == 0 && written_error == EPERM);
    }
}
}  // namespace fixture

int main() {
    fixture::test_no_wait_and_expired_deadline();
    fixture::test_exit_status_and_interruptions();
    fixture::test_timeout_does_not_reap_or_terminate();
    fixture::test_shared_deadline_is_exhausted();
    fixture::test_deadline_expires_during_spawn();
    fixture::test_unrelated_children_and_interrupted_signal_wait();
    fixture::test_error_paths_restore_mask();
    fixture::test_read_interruption_and_original_mask();
    fixture::test_child_inherits_original_mask();
    fixture::test_child_mask_restore_failure();
    puts("ksud script deadline and signal-mask regressions passed");
}
