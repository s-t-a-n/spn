#pragma once

#include "spn/threading/lockguard.hpp"

#include <etl/utility.h>
#include <zephyr/kernel.h>

#include <type_traits>

namespace spn {

/// Reentrant safe mutually exclusive lock
class Mutex {
public:
    Mutex() noexcept { k_mutex_init(&_mutex); }
    ~Mutex() = default;

    Mutex(const Mutex&)            = delete;
    Mutex& operator=(const Mutex&) = delete;
    Mutex(Mutex&&)                 = delete;
    Mutex& operator=(Mutex&&)      = delete;

    /// Lock the mutex
    /// note: returns non-zero on timeout
    [[nodiscard]] int lock(k_timeout_t timeout = K_NO_WAIT) { return k_mutex_lock(&_mutex, timeout); }

    /// Unlock the mutex
    int unlock() noexcept { return k_mutex_unlock(&_mutex); }

    /// Runs a lambda function in a locked context
    /// note: returns default-constructed value on lock timeout
    template<typename Callable>
    auto with_lock(Callable&& func, k_timeout_t timeout = K_NO_WAIT) noexcept(noexcept(func())) {
        if (lock(timeout) != 0) {
            if constexpr (std::is_void_v<decltype(func())>) {
                return;
            } else {
                return decltype(func()){};
            }
        }

        if constexpr (std::is_void_v<decltype(func())>) {
            etl::forward<Callable>(func)();
            unlock();
        } else {
            auto r = etl::forward<Callable>(func)();
            unlock();
            return r;
        }
    }

    /// Returns a LockGuard (initially locked)
    LockGuard lockguard() { return LockGuard{&_mutex}; }

    /// Returns a LockGuard (initially unlocked - should be locked by user)
    LockGuard deferred_lockguard() { return LockGuard{&_mutex, defer_lock}; }

    /// Returns a LockGuard (assumes to be already locked)
    LockGuard adopted_lockguard() { return LockGuard{&_mutex, adopt_lock}; }

private:
    k_mutex _mutex = {};
};

} // namespace spn
