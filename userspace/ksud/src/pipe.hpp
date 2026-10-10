#pragma once

#include <fcntl.h>
#include <unistd.h>
#include <array>

namespace ksud {

// Keep pipe ends outside stdio: callers may run with any of descriptors 0..2
// closed, and dup2 followed by close must not discard a redirected stream.
class CapturePipe {
public:
    CapturePipe() = default;
    ~CapturePipe() {
        close_end(0);
        close_end(1);
    }
    CapturePipe(const CapturePipe&) = delete;
    CapturePipe& operator=(const CapturePipe&) = delete;
    CapturePipe(CapturePipe&&) = delete;
    CapturePipe& operator=(CapturePipe&&) = delete;

    [[nodiscard]] int fd(int end) const { return fds_[end]; }
    void close_end(int end) {
        if (fds_[end] >= 0) {
            close(fds_[end]);
            fds_[end] = -1;
        }
    }
    bool open_pipe() {
        if (pipe2(fds_.data(), O_CLOEXEC) != 0)
            return false;
        for (auto& fd : fds_) {
            if (fd > STDERR_FILENO)
                continue;
            const int copy = fcntl(fd, F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
            if (copy < 0)
                return false;
            close(fd);
            fd = copy;
        }
        return true;
    }

private:
    std::array<int, 2> fds_{-1, -1};
};

}  // namespace ksud
