#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <string>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

namespace {

int writes = 0;
bool interrupted = false;
bool fail_write = false;
std::string output;

int fake_open(const char*, int) {
    return 123;
}
int fake_close(int) {
    return 0;
}

ssize_t fake_write(int fd, const void* data, size_t length) {
    if (fd != 123) {
        return -1;
    }
    ++writes;
    if (interrupted && writes == 1) {
        errno = EINTR;
        return -1;
    }
    if (fail_write) {
        errno = EIO;
        return -1;
    }
    output.assign(static_cast<const char*>(data), length);
    errno = EAGAIN;
    return static_cast<ssize_t>(length);
}

}  // namespace

#define open fake_open
#define close fake_close
#define write fake_write
#include "../src/log.cpp"
#undef write
#undef close
#undef open

int main() {
    ksuinit::log_init("test");
    errno = ENOENT;
    ksuinit::klog(6, "value %d", 42);
    if (output != "<6>value 42" || errno != ENOENT || writes != 1) {
        return 1;
    }
    writes = 0;
    interrupted = true;
    errno = ERANGE;
    ksuinit::klog(3, "%s", "retry");
    if (output != "<3>retry" || errno != ERANGE || writes != 2) {
        return 1;
    }
    const std::string long_message(1024, 'x');
    ksuinit::klog(4, "%s", long_message.c_str());
    if (output.size() != 511 || output.substr(0, 3) != "<4>" || output.back() != 'x') {
        return 1;
    }
    fail_write = true;
    errno = ENOSPC;
    ksuinit::klog(3, "write failure");
    if (errno != ENOSPC) {
        return 1;
    }
    std::puts("ksuinit logging regressions passed");
}
