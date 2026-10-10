#include "../src/core/ksucalls.hpp"

#include <linux/filter.h>
#include <linux/seccomp.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cassert>
#include <cstdarg>
#include <cstddef>
#include <cstring>
#include <iostream>

namespace {

volatile sig_atomic_t forwarded_sigsys = 0;
bool logged_reboot_trap = false;

void previous_sigsys_handler(int, siginfo_t* info, void*) {
    if (info != nullptr && info->si_code == 1 && info->si_syscall == SYS_getppid) {
        forwarded_sigsys = 1;
    }
}

void install_test_filter() {
    const sock_filter filter[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_reboot, 2, 0),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_getppid, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
    };
    const sock_fprog program = {
        static_cast<unsigned short>(sizeof(filter) / sizeof(filter[0])),
        const_cast<sock_filter*>(filter),
    };

    assert(prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0);
    assert(prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program) == 0);
}

}  // namespace

namespace ksud {

void log_v(const char*, ...) {}
void log_d(const char*, ...) {}
void log_i(const char*, ...) {}
void log_w(const char*, ...) {}

void log_e(const char* format, ...) {
    if (format != nullptr && strstr(format, "blocked by seccomp") != nullptr) {
        logged_reboot_trap = true;
    }
}

}  // namespace ksud

int main() {
    struct sigaction previous{};
    previous.sa_sigaction = previous_sigsys_handler;
    previous.sa_flags = SA_SIGINFO;
    assert(sigemptyset(&previous.sa_mask) == 0);
    assert(sigaction(SIGSYS, &previous, nullptr) == 0);

    ksud::setup_sigsys_handler();
    install_test_filter();

    (void)ksud::get_version();
    assert(logged_reboot_trap);
    assert(forwarded_sigsys == 0);

    (void)syscall(SYS_getppid);
    assert(forwarded_sigsys == 1);

    std::cout << "ksucalls_sigsys_test: all checks passed\n";
    return 0;
}
