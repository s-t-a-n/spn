#pragma once

#include "spn/threading/lockguard.hpp"

#include <etl/utility.h>
#include <zephyr/kernel.h>

#include <type_traits>

namespace spn {

/// Reentrant safe mutually exclusive lock
class Mutex {
public:
    Mutex() { k_mutex_init(&_mutex); }

    Mutex(const Mutex&)            = delete;
    Mutex& operator=(const Mutex&) = delete;
    Mutex(Mutex&&)                 = delete;
    Mutex& operator=(Mutex&&)      = delete;

    /// Lock the mutex
    /// note: returns non-zero on timeout
    auto lock(k_timeout_t timeout = K_FOREVER) { return k_mutex_lock(&_mutex, timeout); }

    /// Unlock the mutex
    auto unlock() { return k_mutex_unlock(&_mutex); }

    /// Runs a lambda function in a locked context
    /// note: returns default-constructed value on lock timeout
    template<typename Callable>
    auto with_lock(Callable&& func, k_timeout_t timeout = K_FOREVER) {
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

    /// Returns a LockGuard
    LockGuard lockguard() { return {&_mutex}; }

private:
    k_mutex _mutex = {};
};

} // namespace spn
