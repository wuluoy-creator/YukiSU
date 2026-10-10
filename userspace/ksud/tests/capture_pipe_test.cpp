#include <fcntl.h>
#include <unistd.h>
#include <array>
#include <cassert>
#include <cerrno>
#include <cstdio>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0x80000
#endif
#ifndef F_DUPFD_CLOEXEC
#define F_DUPFD_CLOEXEC 1030
#endif

namespace {
std::array<bool, 32> descriptors{};
int limit = 32;
int fail_duplicate = 0;
int duplicate_calls = 0;

int unused_descriptor(int minimum) {
    for (int fd = minimum; fd < limit; ++fd) {
        if (!descriptors[fd])
            return fd;
    }
    errno = EMFILE;
    return -1;
}

int fake_pipe2(int* fds, int flags) {
    assert(flags == O_CLOEXEC);
    const int read_end = unused_descriptor(0);
    if (read_end < 0)
        return -1;
    descriptors[read_end] = true;
    const int write_end = unused_descriptor(0);
    if (write_end < 0) {
        descriptors[read_end] = false;
        return -1;
    }
    descriptors[write_end] = true;
    fds[0] = read_end;
    fds[1] = write_end;
    return 0;
}

int fake_fcntl(int fd, int operation, int minimum) {
    assert(descriptors[fd] && operation == F_DUPFD_CLOEXEC && minimum == 3);
    if (++duplicate_calls == fail_duplicate) {
        errno = EMFILE;
        return -1;
    }
    const int copy = unused_descriptor(minimum);
    if (copy >= 0)
        descriptors[copy] = true;
    return copy;
}

int fake_close(int fd) {
    assert(fd >= 0 && fd < limit && descriptors[fd]);
    descriptors[fd] = false;
    return 0;
}
}  // namespace

#define pipe2 fake_pipe2
#define fcntl fake_fcntl
#define close fake_close
#include "pipe.hpp"
#undef pipe2
#undef fcntl
#undef close

int main() {
    for (unsigned closed = 0; closed < 8; ++closed) {
        for (int failure = 0; failure <= 2; ++failure) {
            descriptors.fill(false);
            for (int fd = 0; fd < 3; ++fd)
                descriptors[fd] = (closed & (1U << fd)) == 0;
            const auto initial = descriptors;
            duplicate_calls = 0;
            fail_duplicate = failure;
            {
                ksud::CapturePipe pipe;
                const bool opened = pipe.open_pipe();
                if (opened) {
                    assert(pipe.fd(0) > 2 && pipe.fd(1) > 2 && pipe.fd(0) != pipe.fd(1));
                    pipe.close_end(0);
                    pipe.close_end(0);
                    assert(pipe.fd(0) == -1);
                } else {
                    assert(failure != 0 && duplicate_calls == failure);
                }
                for (int fd = 0; fd < 3; ++fd) {
                    if (initial[fd])
                        assert(descriptors[fd]);
                }
            }
            assert(descriptors == initial);
        }
    }

    descriptors.fill(false);
    descriptors[0] = descriptors[1] = descriptors[2] = true;
    const auto initial = descriptors;
    limit = 6;
    fail_duplicate = 0;
    {
        ksud::CapturePipe first;
        ksud::CapturePipe second;
        assert(first.open_pipe());
        assert(!second.open_pipe() && errno == EMFILE);
    }
    assert(descriptors == initial);
    puts("capture pipe fault-injection regressions passed");
}
