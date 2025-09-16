#pragma once

#include <zephyr/kernel.h>

namespace spn {

/// RAII mutex lock guard
class LockGuard {
public:
    /// Acquire mutex lock
    LockGuard(k_mutex* mutex) : mtx(mutex) { k_mutex_lock(mtx, K_FOREVER); }

    /// Release mutex lock
    ~LockGuard() { k_mutex_unlock(mtx); }

    LockGuard(const LockGuard&)            = delete;
    LockGuard& operator=(const LockGuard&) = delete;
    LockGuard(LockGuard&&)                 = delete;
    LockGuard& operator=(LockGuard&&)      = delete;

private:
    k_mutex* mtx;
};

} // namespace spn
