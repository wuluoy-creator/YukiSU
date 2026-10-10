/**
 * ksuinit - KernelSU Init
 *
 * This is the first userspace program to run before the real init.
 * It loads the KernelSU LKM module and then transfers control to the real init.
 *
 * Rewritten from Rust to C++ for ZySU.
 */

#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstring>

#include "init.hpp"
#include "log.hpp"

/**
 * Entry point - we use C main because we need to handle missing stdin/stdout/stderr
 * gracefully (Rust's std would abort in that case).
 */
int main(int /*argc*/, char* argv[], char* envp[]) {
    if (getpid() != 1) {
        (void)fprintf(stderr, "ksuinit must run as PID 1\n");
        return 1;
    }

    // Initialize KernelSU (mount filesystems, load LKM, setup init)
    if (ksuinit::init()) {
        // Transfer control through the restored /init on the normal boot path.
        execve("/init", argv, envp);
        KLOGE("Cannot execute /init: %s", strerror(errno));
    }

    // Never re-execute our own /init when restoring its symlink failed.
    const char* real_init = ksuinit::real_init_path();
    execve(real_init, argv, envp);
    KLOGE("Cannot execute %s: %s", real_init, strerror(errno));

    // If execve fails, we're in trouble
    return 1;
}
