#include "../src/su.hpp"

#include <getopt.h>
#include <sys/types.h>

#include <cassert>
#include <cstdarg>
#include <initializer_list>
#include <iostream>
#include <string>
#include <vector>

namespace {

int unexpected_privileged_calls = 0;

int run(std::initializer_list<const char*> args) {
    std::vector<std::string> storage(args.begin(), args.end());
    std::vector<char*> argv;
    argv.reserve(storage.size() + 1);
    for (auto& arg : storage) {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);
    return ksud::run_su_shell(static_cast<int>(storage.size()), argv.data());
}

void expect_rejected(std::initializer_list<const char*> args) {
    const int calls_before = unexpected_privileged_calls;
    const int result = run(args);
    if (result != 1 || unexpected_privileged_calls != calls_before) {
        std::cerr << "unexpected result for";
        for (const char* arg : args) {
            std::cerr << ' ' << arg;
        }
        std::cerr << ": result=" << result
                  << ", privileged calls=" << unexpected_privileged_calls - calls_before << '\n';
    }
    assert(result == 1);
    assert(unexpected_privileged_calls == calls_before);
}

}  // namespace

namespace ksud {

extern const char* const VERSION_CODE = "1";
extern const char* const VERSION_NAME = "test";

void log_v(const char*, ...) {}
void log_d(const char*, ...) {}
void log_i(const char*, ...) {}
void log_w(const char*, ...) {}
void log_e(const char*, ...) {}

int claim_inherited_su_driver_fd() {
    ++unexpected_privileged_calls;
    return 0;
}

int get_wrapped_fd(int) {
    ++unexpected_privileged_calls;
    return -1;
}

int set_ksu_no_new_privs() {
    ++unexpected_privileged_calls;
    return -1;
}

int grant_root() {
    ++unexpected_privileged_calls;
    return -1;
}

bool switch_mnt_ns(pid_t) {
    ++unexpected_privileged_calls;
    return false;
}

void switch_cgroups() {
    ++unexpected_privileged_calls;
}

void umask(mode_t) {
    ++unexpected_privileged_calls;
}

}  // namespace ksud

int main() {
    opterr = 0;

    expect_rejected({"su", "--unknown"});
    expect_rejected({"su", "-q"});
    expect_rejected({"su", "-Mq"});
    expect_rejected({"su", "-c"});
    expect_rejected({"su", "--command"});
    expect_rejected({"su", "-s"});
    expect_rejected({"su", "-g"});
    expect_rejected({"su", "-G"});
    expect_rejected({"su", "-Z"});
    expect_rejected({"su", "--context"});
    expect_rejected({"su", "--ksu-no-new-prix"});

    assert(run({"su", "--help"}) == 0);
    assert(run({"su", "--version"}) == 0);
    assert(unexpected_privileged_calls == 0);

    std::cout << "su_getopt_test: all checks passed\n";
    return 0;
}
