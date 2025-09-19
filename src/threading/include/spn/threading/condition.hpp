#pragma once

#include "spn/threading/lockguard.hpp"

#include <zephyr/kernel.h>

namespace spn {

/// Threading condition. Wraps k_condvar with C++ semantics
class Condition {
public:
    Condition() {
        k_mutex_init(&_mutex);
        k_condvar_init(&_cond);
    }

    /// Lock the mutex
    auto lock(k_timeout_t timeout = K_FOREVER) { return k_mutex_lock(&_mutex, timeout); }

    /// Unlock the mutex
    auto unlock() { return k_mutex_unlock(&_mutex); }

    /// Wait for a condition
    auto wait(k_timeout_t timeout = K_FOREVER) { return k_condvar_wait(&_cond, &_mutex, timeout); }

    /// Wait for a predicate to become true within a timeout
    template<typename Predicate>
    bool wait_for(k_timeout_t timeout, Predicate pred) {
        uint32_t start_ms   = k_uptime_get_32();
        uint32_t timeout_ms = k_ticks_to_ms_floor32(timeout.ticks);

        while (!pred()) {
            uint32_t now_ms     = k_uptime_get_32();
            uint32_t elapsed_ms = now_ms - start_ms;

            if (elapsed_ms >= timeout_ms) {
                return false; // timeout expired
            }

            uint32_t    remaining_ms      = timeout_ms - elapsed_ms;
            k_timeout_t remaining_timeout = K_MSEC(remaining_ms);

            bool signaled = wait(remaining_timeout);
            if (!signaled) return false; // timeout expired during wait
        }
        return true;
    }

    /// Signal single waiting thread
    void signal() { k_condvar_signal(&_cond); }

    /// Signal all waiting threads
    auto broadcast() { return k_condvar_broadcast(&_cond); }

    /// Returns RAII-lockguard of underlying mutex
    LockGuard lockguard() { return LockGuard(&_mutex); }

private:
    k_mutex   _mutex{};
    k_condvar _cond{};
};

} // namespace spn
