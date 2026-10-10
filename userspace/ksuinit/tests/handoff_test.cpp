#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "init.hpp"
#include "log.hpp"

namespace {

bool is_pid_one = true;
bool init_result = true;
bool has_original_init = true;
size_t init_calls = 0;
char** expected_argv = nullptr;
char** expected_envp = nullptr;
std::vector<std::string> exec_paths;

int fake_getpid() {
    return is_pid_one ? 1 : 42;
}

int fake_execve(const char* path, char* const argv[], char* const envp[]) {
    if (argv != expected_argv || envp != expected_envp) {
        std::abort();
    }
    exec_paths.emplace_back(path);
    errno = ENOENT;
    return -1;
}

}  // namespace

// Run the real entry point with only the operating-system boundary replaced.
#define getpid fake_getpid
#define execve fake_execve
#define main ksuinit_main
#include "../src/main.cpp"
#undef main
#undef execve
#undef getpid

namespace ksuinit {

bool init() {
    ++init_calls;
    return init_result;
}

const char* real_init_path() {
    return has_original_init ? "/init.real" : "/system/bin/init";
}

void klog(int, const char*, ...) {}

}  // namespace ksuinit

int main() {
    char program[] = "/init";
    char argument[] = "second_stage";
    char environment[] = "TEST=kept";
    char* argv[] = {program, argument, nullptr};
    char* envp[] = {environment, nullptr};
    expected_argv = argv;
    expected_envp = envp;

    for (bool original : {true, false}) {
        has_original_init = original;
        const std::string fallback = original ? "/init.real" : "/system/bin/init";
        for (bool restored : {true, false}) {
            init_result = restored;
            init_calls = 0;
            exec_paths.clear();
            if (ksuinit_main(2, argv, envp) != 1 || init_calls != 1 ||
                exec_paths.size() != (restored ? 2u : 1u) || exec_paths.back() != fallback ||
                (restored && exec_paths.front() != "/init")) {
                return 1;
            }
        }
    }
    is_pid_one = false;
    init_calls = 0;
    exec_paths.clear();
    if (ksuinit_main(2, argv, envp) != 1 || init_calls != 0 || !exec_paths.empty()) {
        return 1;
    }
    std::puts("ksuinit PID 1 handoff regressions passed");
}
