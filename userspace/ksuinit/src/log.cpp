/**
 * ksuinit - Kernel Log
 *
 * Simple logging to /dev/kmsg for early init stage.
 */

#include "log.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstdarg>
#include <cstdio>

namespace ksuinit {

namespace {

int g_kmsg_fd = -1;

class ErrnoGuard {
public:
    ErrnoGuard() : saved_(errno) {}
    ~ErrnoGuard() { errno = saved_; }
    ErrnoGuard(const ErrnoGuard&) = delete;
    ErrnoGuard& operator=(const ErrnoGuard&) = delete;
    ErrnoGuard(ErrnoGuard&&) = delete;
    ErrnoGuard& operator=(ErrnoGuard&&) = delete;

private:
    int saved_;
};

}  // anonymous namespace

void log_init(const char* device) {
    if (g_kmsg_fd >= 0) {
        close(g_kmsg_fd);
    }
    g_kmsg_fd = open(device, O_WRONLY | O_CLOEXEC);
}

void klog(int level, const char* fmt, ...) {
    const ErrnoGuard errno_guard;
    char buf[512];

    // Format: "<level>message"
    const int prefix_len = snprintf(buf, sizeof(buf), "<%d>", level);
    if (prefix_len < 0 || prefix_len >= static_cast<int>(sizeof(buf))) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    const int msg_len = vsnprintf(buf + prefix_len, sizeof(buf) - prefix_len, fmt, args);
    va_end(args);
    if (msg_len < 0) {
        return;
    }

    // vsnprintf returns the full untruncated length; clamp before adding the
    // prefix so a very large formatted message cannot overflow an int.
    const size_t available = sizeof(buf) - static_cast<size_t>(prefix_len) - 1;
    const size_t message_size = static_cast<size_t>(msg_len);
    const size_t total_len =
        static_cast<size_t>(prefix_len) + (message_size < available ? message_size : available);

    if (g_kmsg_fd >= 0) {
        ssize_t result;
        do {
            result = write(g_kmsg_fd, buf, total_len);
        } while (result < 0 && errno == EINTR);
    } else {
        // Fallback to stderr if kmsg is not available
        (void)fprintf(stderr, "%s", buf + prefix_len);
    }
}

}  // namespace ksuinit
