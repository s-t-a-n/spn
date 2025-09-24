#pragma once

#include "spn/debugging/assert.hpp"

#include <etl/algorithm.h>
#include <zephyr/kernel.h>

namespace spn {

struct adopt_lock_t {}; // adopt an owned lock without locking
struct defer_lock_t {}; // create without locking; lock later

inline constexpr adopt_lock_t adopt_lock{};
inline constexpr defer_lock_t defer_lock{};

/// RAII mutex lock guard
class LockGuard {
public:
    explicit LockGuard(k_mutex* mutex) noexcept : _mutex(mutex) {
        auto rc = k_mutex_lock(_mutex, K_FOREVER);
        _owns   = (rc == 0);
    }

    LockGuard(k_mutex* m, adopt_lock_t) noexcept : _mutex(m), _owns(true) {}
    LockGuard(k_mutex* m, defer_lock_t) noexcept : _mutex(m), _owns(false) {}

    LockGuard(const LockGuard&)            = delete;
    LockGuard& operator=(const LockGuard&) = delete;

    LockGuard(LockGuard&& other) noexcept : _mutex(other._mutex), _owns(other._owns) {
        other._mutex = nullptr;
        other._owns  = false;
    }

    LockGuard& operator=(LockGuard&& other) noexcept {
        if (this == &other) return *this;
        if (_owns && _mutex) (void)k_mutex_unlock(_mutex);
        _mutex       = other._mutex;
        _owns        = other._owns;
        other._mutex = nullptr;
        other._owns  = false;
        return *this;
    }

    ~LockGuard() {
        if (_owns) (void)k_mutex_unlock(_mutex);
    }

    int lock(k_timeout_t t = K_FOREVER) noexcept {
        if (_owns) return -EINVAL;
        const int rc = k_mutex_lock(_mutex, t);
        _owns        = (rc == 0);
        return rc;
    }

    /// Unlock if lock is owned
    int unlock() noexcept {
        if (!_owns) return -EPERM;
        _owns = false;
        return k_mutex_unlock(_mutex);
    }

    /// Check if this guard currently owns the lock
    bool owns_lock() const noexcept { return _owns; }

private:
    k_mutex* _mutex;
    bool     _owns;
};

} // namespace spn
