// Fault-injection tests compile the production exec_dd body without accessing
// real partitions. The runner supplies boot_copy_under_test.inc unchanged.
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr int O_RDONLY = 0;
constexpr int O_WRONLY = 1;
constexpr int O_CLOEXEC = 2;
constexpr int O_CREAT = 4;
constexpr int O_TRUNC = 8;
constexpr int regular_mode = 1;
constexpr int block_mode = 2;
#define S_ISREG(mode) ((mode) == regular_mode)
#define S_ISBLK(mode) ((mode) == block_mode)

struct stat {
    int st_dev = 1;
    int st_ino = 1;
    int st_mode = regular_mode;
    int st_rdev = 0;
};

struct State {
    std::string input = std::string(2 * 1024 * 1024 + 17, 'x');
    std::string output = "previous output";
    size_t read_offset = 0;
    size_t write_offset = 0;
    struct stat source;
    struct stat destination{1, 2, regular_mode, 0};
    int closed = 0;
    int truncations = 0;
    int syncs = 0;
    bool interrupt_read = false;
    bool interrupt_write = false;
    bool interrupt_sync = false;
    bool zero_write = false;
    bool fail_stat = false;
    bool fail_sync = false;
    bool fail_close = false;
};

State state;

void log_error(const char*, ...) {}
#define LOGE(...) log_error(__VA_ARGS__)

int open(const char*, int flags, int = 0) {
    if ((flags & O_TRUNC) != 0) {
        state.output.clear();
        if (state.source.st_ino == state.destination.st_ino)
            state.input.clear();
    }
    return (flags & O_WRONLY) != 0 ? 11 : 10;
}

int fstat(int fd, struct stat* result) {
    if (state.fail_stat) {
        errno = EIO;
        return -1;
    }
    *result = fd == 10 ? state.source : state.destination;
    return 0;
}

int ftruncate(int fd, long size) {
    assert(fd == 11 && size == 0);
    assert(state.destination.st_mode == regular_mode);
    ++state.truncations;
    state.output.clear();
    return 0;
}

ptrdiff_t read(int fd, void* buffer, size_t size) {
    assert(fd == 10);
    if (state.interrupt_read) {
        state.interrupt_read = false;
        errno = EINTR;
        return -1;
    }
    const size_t count = std::min({size, state.input.size() - state.read_offset, size_t{8191}});
    std::memcpy(buffer, state.input.data() + state.read_offset, count);
    state.read_offset += count;
    return static_cast<ptrdiff_t>(count);
}

ptrdiff_t write(int fd, const void* buffer, size_t size) {
    assert(fd == 11);
    if (state.interrupt_write) {
        state.interrupt_write = false;
        errno = EINTR;
        return -1;
    }
    if (state.zero_write)
        return 0;
    const size_t count = std::min(size, size_t{4093});
    state.output.resize(std::max(state.output.size(), state.write_offset + count));
    std::memcpy(state.output.data() + state.write_offset, buffer, count);
    state.write_offset += count;
    return static_cast<ptrdiff_t>(count);
}

int fsync(int fd) {
    assert(fd == 11);
    ++state.syncs;
    if (state.interrupt_sync) {
        state.interrupt_sync = false;
        errno = EINTR;
        return -1;
    }
    errno = EIO;
    return state.fail_sync ? -1 : 0;
}

int close(int fd) {
    ++state.closed;
    errno = EIO;
    return fd == 11 && state.fail_close ? -1 : 0;
}

using ssize_t = ptrdiff_t;
#include "boot_copy_under_test.inc"

bool copy() {
    const bool result = exec_dd("input", "output");
    assert(state.closed == 2);
    return result;
}

}  // namespace

int main() {
    state.interrupt_read = state.interrupt_write = state.interrupt_sync = true;
    assert(copy());
    assert(state.output == state.input && state.truncations == 1 && state.syncs == 2);
    state = State{};
    state.input = "short";
    assert(copy() && state.output == "short");
    state = State{};
    state.destination.st_ino = state.source.st_ino;
    const auto original = state.input;
    assert(!copy() && state.truncations == 0 && state.input == original);
    state = State{};
    state.source.st_mode = state.destination.st_mode = block_mode;
    state.source.st_rdev = state.destination.st_rdev = 42;
    assert(!copy() && state.write_offset == 0);
    state = State{};
    state.destination.st_mode = block_mode;
    assert(copy() && state.truncations == 0 && state.output == state.input);
    state = State{};
    state.fail_stat = true;
    assert(!copy() && state.truncations == 0);
    state = State{};
    state.zero_write = true;
    assert(!copy() && state.syncs == 0);
    state = State{};
    state.fail_sync = true;
    assert(!copy());
    state = State{};
    state.fail_close = true;
    assert(!copy());
    puts("Boot image copy fault-injection tests passed");
}
