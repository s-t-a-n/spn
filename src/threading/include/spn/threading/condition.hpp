#pragma once

#include "spn/threading/lockguard.hpp"

#include <zephyr/kernel.h>
#if defined(CONFIG_OBJ_CORE_MUTEX) || defined(CONFIG_OBJ_CORE_CONDVAR)
#    include <zephyr/kernel/obj_core.h>
#endif

namespace spn {

/// Threading condition. Wraps k_condvar with C++ semantics
class Condition {
public:
    Condition() {
        k_mutex_init(&_mutex);
        k_condvar_init(&_cond);
    }

    ~Condition() {
#ifdef CONFIG_OBJ_CORE_MUTEX
        k_obj_core_unlink(&_mutex.obj_core);
#endif
#ifdef CONFIG_OBJ_CORE_CONDVAR
        k_obj_core_unlink(&_cond.obj_core);
#endif
    }

    /// Lock the mutex
    int lock(k_timeout_t timeout = K_FOREVER) { return k_mutex_lock(&_mutex, timeout); }

    /// Unlock the mutex
    int unlock() { return k_mutex_unlock(&_mutex); }

    /// Wait for a condition
    int wait(k_timeout_t timeout = K_FOREVER) { return k_condvar_wait(&_cond, &_mutex, timeout); }

    /// Wait for a predicate to become true within a timeout
    template<typename Predicate>
    bool wait_for(k_timeout_t timeout, Predicate pred) {
        if (pred()) return true;

        auto endpoint = sys_timepoint_calc(timeout);

        do {
            auto remaining_timeout = sys_timepoint_timeout(endpoint);

            if (K_TIMEOUT_EQ(remaining_timeout, K_NO_WAIT)) {
                return pred();
            }

            if (wait(remaining_timeout) == -EAGAIN) {
                return pred();
            }
        } while (!pred());

        return true;
    }

    /// Signal single waiting thread
    void signal() { k_condvar_signal(&_cond); }

    /// Signal all waiting threads. Returns number of waiters or error value.
    int broadcast() { return k_condvar_broadcast(&_cond); }

    /// Returns RAII-lockguard of underlying mutex
    LockGuard lockguard() { return LockGuard(&_mutex); }

private:
    k_mutex   _mutex{};
    k_condvar _cond{};
};

} // namespace spn
