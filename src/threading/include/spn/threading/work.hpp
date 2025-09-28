#pragma once

#include "spn/threading/semaphore.hpp"

#include <etl/type_traits.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

namespace spn {

/// Delayed work scheduler
template<typename ArgT, typename OwnerT>
class Work {
public:
    static constexpr bool has_arg = !std::is_void_v<ArgT>;
    using handler_f               = std::conditional_t<has_arg, void (OwnerT::*)(ArgT*), void (OwnerT::*)()>;

    explicit Work(OwnerT* owner, handler_f handler) {
        k_work_init_delayable(&_cw.work, Work::work_handler);
        assign_owner(owner);
        assign_handler(handler);
        _cw.self = this;
    }
    Work()                       = delete;
    Work(const Work&)            = delete;
    Work& operator=(const Work&) = delete;
    Work(Work&&)                 = delete;
    Work& operator=(Work&&)      = delete;

    ~Work() { cancel(); }

    /// Schedule work to run after delay
    [[nodiscard]] int schedule(k_timeout_t delta_time, ArgT* arg = nullptr) {
        if constexpr (has_arg) atomic_ptr_set(&_cw.arg, arg);
        return k_work_reschedule(&_cw.work, delta_time);
    }

    /// Check if work is scheduled
    bool is_scheduled() const {
        return k_work_delayable_is_pending(&_cw.work) || k_work_delayable_busy_get(&_cw.work) & K_WORK_QUEUED;
    }

    /// Cancel pending work
    /// note: async=false blocks until handler exits, async=true returns immediately
    void cancel(bool async = false) {
        if (k_is_in_isr()) {
            k_work_cancel_delayable(&_cw.work);
            return;
        }
        if (async) {
            k_work_cancel_delayable(&_cw.work);
            return;
        }
        if (running_in_current_thread()) {
            k_work_cancel_delayable(&_cw.work);
            return;
        }

        _sync_sem.take(K_FOREVER);
        k_work_cancel_delayable_sync(&_cw.work, &_sync);
        _sync_sem.give();
    }

    /// Flush work; returns 1 if the call waited, 0 if it did not, or a negative errno
    [[nodiscard]] int flush(k_timeout_t timeout = K_FOREVER) {
        if (k_is_in_isr()) {
            return 0;
        }
        if (running_in_current_thread()) {
            return 0;
        }

        int rc = _sync_sem.take(timeout);
        if (rc != 0) {
            return rc;
        }
        bool waited = k_work_flush_delayable(&_cw.work, &_sync);
        _sync_sem.give();

        return waited ? 1 : 0;
    }

protected:
    void assign_arg(ArgT* arg) {
        if constexpr (has_arg) {
            atomic_ptr_set(&_cw.arg, arg);
        }
    }
    void assign_owner(OwnerT* owner) { _cw.owner = owner; }
    void assign_handler(handler_f handler) { _cw.handler = handler; }

    void invoke() {
        if constexpr (has_arg) {
            auto* arg = static_cast<ArgT*>(atomic_ptr_get(&_cw.arg));
            (_cw.owner->*_cw.handler)(arg);
        } else
            (_cw.owner->*_cw.handler)();
    }

private:
    static void work_handler(k_work* work) {
        auto c = CONTAINER_OF(work, ContainedWork, work);
        atomic_ptr_set(&c->runner, k_current_get());
        c->self->invoke();
        atomic_ptr_clear(&c->runner);
    }

    struct ContainedWork {
        atomic_ptr_t        arg    = ATOMIC_PTR_INIT(nullptr); // ignored when ArgT == void
        atomic_ptr_t        runner = ATOMIC_PTR_INIT(nullptr);
        OwnerT*             owner{};
        handler_f           handler{};
        k_work_delayable    work{};
        Work<ArgT, OwnerT>* self{nullptr};
    };

    ContainedWork   _cw{};
    k_work_sync     _sync{};
    Semaphore<1, 1> _sync_sem;

    bool running_in_current_thread() const { return atomic_ptr_get(&_cw.runner) == k_current_get(); }
};

} // namespace spn
