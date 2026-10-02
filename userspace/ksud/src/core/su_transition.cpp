#include "su_transition.hpp"

#include "defs.hpp"
#include "log.hpp"
#include "utils.hpp"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace ksud {

namespace {

constexpr const char* kTransitionLockPath = "/data/adb/ksu/sucompat.transition.lock";

}  // namespace

SucompatTransitionLock::SucompatTransitionLock() {
    if (geteuid() != 0 || !ensure_dir_exists(WORKING_DIR)) {
        LOGE("sucompat: transition lock requires root and %s", WORKING_DIR);
        return;
    }

    fd_ = open(kTransitionLockPath, O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd_ < 0) {
        LOGE("sucompat: failed to open transition lock: %s", strerror(errno));
        return;
    }

    struct stat st{};
    if (fstat(fd_, &st) != 0 || !S_ISREG(st.st_mode) || st.st_nlink != 1 ||
        fchown(fd_, 0, 0) != 0 || fchmod(fd_, 0600) != 0) {
        LOGE("sucompat: invalid transition lock: %s", strerror(errno));
        close(fd_);
        fd_ = -1;
        return;
    }

    int ret;
    do {
        ret = flock(fd_, LOCK_EX);
    } while (ret != 0 && errno == EINTR);
    if (ret != 0) {
        LOGE("sucompat: failed to acquire transition lock: %s", strerror(errno));
        close(fd_);
        fd_ = -1;
    }
}

SucompatTransitionLock::~SucompatTransitionLock() {
    if (fd_ >= 0) {
        close(fd_);
    }
}

bool SucompatTransitionLock::locked() const {
    return fd_ >= 0;
}

}  // namespace ksud
