#pragma once

#include <etl/type_traits.h>
#include <zephyr/kernel.h>

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
    void schedule(k_timeout_t delta_time, ArgT* arg = nullptr) {
        if constexpr (has_arg) _cw.arg = arg;
        k_work_reschedule(&_cw.work, delta_time);
    }

    /// Check if work is scheduled
    bool is_scheduled() const {
        return k_work_delayable_is_pending(&_cw.work) || k_work_delayable_busy_get(&_cw.work) & K_WORK_QUEUED;
    }

    /// Cancel pending work
    /// note: async=false blocks until handler exits, async=true returns immediately
    void cancel(bool async = false) {
        if (_in_handler) {
            k_work_cancel_delayable(&_cw.work);
            return;
        }
        if (async) {
            k_work_cancel_delayable(&_cw.work);
            return;
        }
        k_work_sync sync{};
        k_work_cancel_delayable_sync(&_cw.work, &sync);
    }

protected:
    void assign_arg(ArgT* arg) {
        if constexpr (has_arg) {
            // todo: make arg atomatic or lock the access by mutex
            _cw.arg = arg;
        }
    }
    void assign_owner(OwnerT* owner) { _cw.owner = owner; }
    void assign_handler(handler_f handler) { _cw.handler = handler; }

    void invoke() {
        if constexpr (has_arg) (_cw.owner->*_cw.handler)(_cw.arg);
        else
            (_cw.owner->*_cw.handler)();
    }

private:
    static void work_handler(k_work* work) {
        auto c               = CONTAINER_OF(work, ContainedWork, work);
        c->self->_in_handler = true;
        if constexpr (has_arg) (c->owner->*c->handler)(c->arg);
        else
            (c->owner->*c->handler)();
        c->self->_in_handler = false;
    }

    struct ContainedWork {
        ArgT*               arg{}; // ignored when ArgT == void
        OwnerT*             owner{};
        handler_f           handler{};
        k_work_delayable    work{};
        Work<ArgT, OwnerT>* self{nullptr};
    };

    ContainedWork _cw{};
    bool          _in_handler{false};
};

} // namespace spn
