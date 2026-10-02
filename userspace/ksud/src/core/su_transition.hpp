#pragma once

namespace ksud {

class SucompatTransitionLock {
public:
    SucompatTransitionLock();
    ~SucompatTransitionLock();

    SucompatTransitionLock(const SucompatTransitionLock&) = delete;
    SucompatTransitionLock& operator=(const SucompatTransitionLock&) = delete;

    [[nodiscard]] bool locked() const;

private:
    int fd_{-1};
};

}  // namespace ksud
