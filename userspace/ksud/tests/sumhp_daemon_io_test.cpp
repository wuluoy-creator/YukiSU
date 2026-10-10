#include <algorithm>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <thread>
#include <vector>

// Deterministic syscall failures complement the real POSIX socket tests and
// let Windows hosts exercise the same production I/O loops.
using ssize_t = std::ptrdiff_t;
constexpr short POLLIN = 1, POLLOUT = 4, POLLERR = 8, POLLHUP = 16, POLLNVAL = 32;
constexpr int MSG_NOSIGNAL = 0x4000;
struct pollfd {
    int fd;
    short events;
    short revents;
};

namespace {
struct Step {
    ssize_t result;
    int error = 0;
};
std::deque<Step> reads, writes, polls;
std::vector<int> waits;
std::string input, sent;
std::size_t consumed;
short poll_events;
int delay_ms;

Step next(std::deque<Step>& steps, ssize_t fallback) {
    if (steps.empty())
        return {fallback};
    const auto step = steps.front();
    steps.pop_front();
    return step;
}

int poll(pollfd* fd, unsigned count, int timeout) {
    assert(count == 1 && fd->fd == 3 && timeout > 0);
    waits.push_back(timeout);
    if (delay_ms)
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    const auto step = next(polls, 1);
    errno = step.error;
    fd->revents = poll_events ? poll_events : fd->events;
    return static_cast<int>(step.result);
}

ssize_t send(int fd, const void* buffer, std::size_t length, int flags) {
    assert(fd == 3 && flags == MSG_NOSIGNAL);
    const auto step = next(writes, static_cast<ssize_t>(length));
    errno = step.error;
    if (step.result > 0) {
        assert(static_cast<std::size_t>(step.result) <= length);
        sent.append(static_cast<const char*>(buffer), static_cast<std::size_t>(step.result));
    }
    return step.result;
}

ssize_t recv(int fd, void* buffer, std::size_t length, int flags) {
    assert(fd == 3 && flags == 0);
    const auto step = next(reads, static_cast<ssize_t>(std::min(length, input.size() - consumed)));
    errno = step.error;
    if (step.result > 0) {
        const auto size = static_cast<std::size_t>(step.result);
        assert(size <= length && consumed + size <= input.size());
        std::memcpy(buffer, input.data() + consumed, size);
        consumed += size;
    }
    return step.result;
}

void reset(const std::string& bytes = {}) {
    reads.clear();
    writes.clear();
    polls.clear();
    waits.clear();
    input = bytes;
    sent.clear();
    consumed = 0;
    poll_events = 0;
    delay_ms = 0;
}
}  // namespace

namespace sumhp {
#include "sumhp_daemon_io_under_test.inc"
}  // namespace sumhp

int main() {
    using namespace std::chrono_literals;
    const auto deadline = [] { return std::chrono::steady_clock::now() + 2s; };
    std::string output;

    reset("hello\n");
    reads = {{2}, {-1, EINTR}, {-1, EAGAIN}, {4}};
    polls = {{-1, EINTR}, {1}};
    assert(sumhp::read_message(3, 6, deadline(), output) && output == input);

    reset();
    writes = {{2}, {-1, EINTR}, {-1, EAGAIN}, {4}};
    assert(sumhp::write_all(3, "hello\n", deadline()) && sent == "hello\n");

    reset("done\n");
    poll_events = POLLIN | POLLHUP;
    assert(sumhp::read_message(3, 5, deadline(), output) && output == input);

    for (const auto* bytes : {"", "truncated", "one\ntwo\n"}) {
        reset(bytes);
        output = "stale";
        assert(!sumhp::read_message(3, 64, deadline(), output));
        assert(errno == EPROTO && output.empty());
    }
    reset("123456\n");
    assert(!sumhp::read_message(3, 6, deadline(), output));
    assert(errno == EMSGSIZE && output.empty() && consumed == 6);

    reset("part");
    reads = {{2}, {-1, ECONNRESET}};
    assert(!sumhp::read_message(3, 64, deadline(), output));
    assert(errno == ECONNRESET && output.empty());

    reset();
    writes = {{2}, {-1, EPIPE}};
    assert(!sumhp::write_all(3, "hello\n", deadline()));
    assert(errno == EPIPE && sent == "he");
    reset();
    writes = {{0}};
    assert(!sumhp::write_all(3, "hello\n", deadline()) && errno == EIO);

    reset();
    poll_events = POLLNVAL;
    assert(!sumhp::read_message(3, 64, deadline(), output) && errno == EBADF);
    reset();
    polls = {{0}};
    assert(!sumhp::write_all(3, "hello\n", deadline()) && errno == ETIMEDOUT);
    reset();
    assert(!sumhp::read_message(3, 64, std::chrono::steady_clock::now() - 1s, output));
    assert(errno == ETIMEDOUT && waits.empty());

    // Progress and interruptions consume one fixed deadline. Slow peers must
    // not renew their allowance after each byte or interrupted poll.
    reset("abc\n");
    reads = {{1}, {1}, {2}};
    polls = {{1}, {-1, EINTR}, {0}};
    delay_ms = 10;
    assert(!sumhp::read_message(3, 64, deadline(), output));
    assert(errno == ETIMEDOUT && output.empty() && consumed == 1);
    assert(waits.size() == 3 && waits[0] > waits[1] && waits[1] > waits[2]);

    puts("SUMHP portable socket fault regressions passed");
}
