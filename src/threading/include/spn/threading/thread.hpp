#pragma once

#include "spn/core/enum_flags.hpp"
#include "spn/core/types.hpp"
#include "spn/debugging/assert.hpp"
#include "spn/logging/logging.hpp"
#include "spn/threading/mutex.hpp"
#include "spn/threading/pacing/pacing_strategy.hpp"
#include "spn/threading/pacing/spin_pacing.hpp"

#include <etl/delegate.h>
#include <zephyr/kernel.h>
#if defined(CONFIG_OBJ_CORE_THREAD) || defined(CONFIG_OBJ_CORE_EVENT)
#    include <zephyr/kernel/obj_core.h>
#endif

#include <cerrno>

namespace spn {

enum class ThreadState : uint32_t {
    IDLE        = 1 << 0,
    STARTING    = 1 << 1,
    RUNNING     = 1 << 2,
    PAUSING     = 1 << 3,
    PAUSED      = 1 << 4,
    RESUMING    = 1 << 5,
    STOPPING    = 1 << 6,
    STOPPED     = 1 << 7,
    ABORTED     = 1 << 8,
    MASK_STATE  = IDLE | STARTING | RUNNING | PAUSING | PAUSED | RESUMING | STOPPING | STOPPED | ABORTED,
    MASK_HALTED = STOPPED | ABORTED,
};

/// Convert thread state to string
constexpr const char* to_string(ThreadState state) noexcept {
    switch (state) {
    case ThreadState::IDLE: return "IDLE";
    case ThreadState::STARTING: return "STARTING";
    case ThreadState::RUNNING: return "RUNNING";
    case ThreadState::PAUSING: return "PAUSING";
    case ThreadState::PAUSED: return "PAUSED";
    case ThreadState::RESUMING: return "RESUMING";
    case ThreadState::STOPPING: return "STOPPING";
    case ThreadState::STOPPED: return "STOPPED";
    case ThreadState::ABORTED: return "ABORTED";
    default: return "UNKNOWN";
    }
}

/// Managed thread with state control
template<size_t STACK_SIZE, typename ArgT>
class Thread {
public:
    using Delegate = etl::delegate<void(ArgT* arg, ThreadState state)>;

    Thread(
        Delegate                    delegate,
        ArgT*                       arg,
        int                         priority = 10,
        const char*                 name     = nullptr,
        threading::IPacingStrategy& pacing   = threading::SpinPacing::instance()
    )
        : _thread{}, _thread_stack{}, _thread_priority(priority), _delegate(delegate), _arg(arg), _pacing(&pacing),
          _pacing_active(false), _ev{} {
        k_event_init(&_ev);
        k_event_set(&_ev, U32(ThreadState::IDLE));

        auto id = k_thread_create(
            &_thread,
            _thread_stack,
            K_THREAD_STACK_SIZEOF(_thread_stack),
            thread_entry,
            this,
            arg,
            &_ev,
            priority,
            0,
            K_FOREVER
        );
        if (name != nullptr) k_thread_name_set(id, name);
    }

    Thread(const Thread&)             = delete;
    Thread(const Thread&&)            = delete;
    Thread& operator=(const Thread&)  = delete;
    Thread& operator=(const Thread&&) = delete;

    ~Thread() {
        stop();
#ifdef CONFIG_OBJ_CORE_THREAD
        k_obj_core_unlink(&_thread.obj_core);
#endif
#ifdef CONFIG_OBJ_CORE_EVENT
        k_obj_core_unlink(&_ev.obj_core);
#endif
    }

    /// Start the thread
    /// note: returns -EINVAL if not in IDLE state
    [[nodiscard]] int start() {
        auto lockguard = _mutex.lockguard();

        if (k_event_test(&_ev, U32(ThreadState::IDLE)) == U32(ThreadState::IDLE)) {
            k_event_set_masked(&_ev, U32(ThreadState::STARTING), U32(ThreadState::MASK_STATE));
            k_thread_start(&_thread);
            return 0;
        }
        return -EINVAL;
    }

    /// Resume paused thread
    /// note: returns -EINVAL if not in PAUSED state
    [[nodiscard]] int resume() {
        auto lockguard = _mutex.lockguard();

        if (k_event_test(&_ev, U32(ThreadState::PAUSED)) == U32(ThreadState::PAUSED)) {
            k_event_set_masked(&_ev, U32(ThreadState::RESUMING), U32(ThreadState::MASK_STATE));
            return 0;
        }
        return -EINVAL;
    }

    /// Set pacing strategy when the thread is idle, paused, or halted
    /// note: returns -EBUSY if the thread is running or transitioning
    [[nodiscard]] int set_pacing_strategy(threading::IPacingStrategy& pacing) {
        auto lockguard = _mutex.lockguard();

        auto flags = k_event_test(&_ev, U32(ThreadState::MASK_STATE));
        if (flags
            & U32(
                ThreadState::RUNNING | ThreadState::STARTING | ThreadState::RESUMING | ThreadState::PAUSING
                | ThreadState::STOPPING
            )) {
            return -EBUSY;
        }

        _pacing = &pacing;
        return 0;
    }

    /// Returns the pacing strategy currently associated with the thread
    threading::IPacingStrategy& pacing_strategy() {
        auto lockguard = _mutex.lockguard();
        return *_pacing;
    }

    /// Returns the pacing strategy currently associated with the thread
    const threading::IPacingStrategy& pacing_strategy() const {
        auto lockguard = _mutex.lockguard();
        return *_pacing;
    }

    /// Start idle thread or resume paused thread
    [[nodiscard]] int start_or_resume() {
        auto lockguard = _mutex.lockguard();

        if (k_event_test(&_ev, U32(ThreadState::PAUSED)) == U32(ThreadState::PAUSED)) {
            return resume();
        }
        return start();
    }

    /// Pause running thread
    /// note: returns -ETIMEDOUT on timeout, -EINVAL if not RUNNING
    int pause(k_timeout_t timeout = K_FOREVER) {
        auto lockguard = _mutex.lockguard();

        auto flags = k_event_test(&_ev, U32(ThreadState::MASK_STATE));

        if (flags == U32(ThreadState::PAUSED)) return 0;
        if (flags != U32(ThreadState::RUNNING)) return -EINVAL;

        k_event_set_masked(&_ev, U32(ThreadState::PAUSING), U32(ThreadState::MASK_STATE));

        interrupt_pacing();

        // avoid deadlock if delegate calls pause() on itself
        if (k_current_get() == &_thread) {
            return 0;
        }

        flags = k_event_wait(&_ev, U32(ThreadState::PAUSED), false, timeout);

        if (flags == 0) {
            if (timeout.ticks != 0) MLOG_WRN(spn_threading, "Thread: timed out waiting for thread to pause");
            return -ETIMEDOUT;
        }

        if (flags == U32(ThreadState::PAUSED)) return 0;

        MLOG_WRN(spn_threading, "Thread: failed to pause thread. Thread is in state: %u", flags);
        return -EINVAL;
    }

    /// Stop thread and wait for completion
    /// note: aborts thread and returns -EAGAIN on timeout
    int stop(k_timeout_t timeout = K_FOREVER) {
        auto lockguard = _mutex.lockguard();

        auto flags = k_event_test(&_ev, U32(ThreadState::MASK_STATE));

        if (flags == U32(ThreadState::STOPPED)) return 0;
        if (flags == U32(ThreadState::ABORTED)) return -EINVAL;

        if (flags == U32(ThreadState::IDLE)) {
            k_event_set_masked(&_ev, U32(ThreadState::STOPPED), U32(ThreadState::MASK_STATE));
            return 0;
        }

        k_event_set_masked(&_ev, U32(ThreadState::STOPPING), U32(ThreadState::MASK_STATE));
        interrupt_pacing();

        // avoid deadlock if delegate calls stop() on itself
        if (k_current_get() == &_thread) {
            return 0;
        }

        flags = k_event_wait(&_ev, U32(ThreadState::STOPPED), false, timeout);

        if (flags == 0) {
            MLOG_WRN(spn_threading, "Thread: timed out waiting for thread to stop");
            k_thread_abort(&_thread);
            k_event_set(&_ev, U32(ThreadState::ABORTED));
            return -EAGAIN;
        }

        if (flags == U32(ThreadState::STOPPED)) {
            return k_thread_join(&_thread, timeout);
        }

        MLOG_WRN(spn_threading, "Thread: failed to stop thread. Thread is in state: %u", flags);
        return -EINVAL;
    }

    /// Change thread priority
    void adjust_priority(int new_priority) {
        auto lockguard = _mutex.lockguard();

        k_thread_priority_set(&_thread, new_priority);
        _thread_priority = new_priority;
    }

    /// Get current thread state
    ThreadState state() {
        auto lockguard = _mutex.lockguard();
        return static_cast<ThreadState>(k_event_test(&_ev, U32(ThreadState::MASK_STATE)));
    }

    /// Get current thread priority
    int priority() {
        auto lockguard = _mutex.lockguard();
        return _thread_priority;
    }

private:
    void activate_pacing() {
        if (_pacing_active) return;
        _pacing->on_enter_running();
        _pacing_active = true;
    }

    void deactivate_pacing() {
        if (!_pacing_active) return;
        _pacing->on_exit_running();
        _pacing_active = false;
    }

    void interrupt_pacing() {
        if (_pacing == nullptr) return;
        _pacing->interrupt();
        k_wakeup(&_thread);
    }

    static void thread_entry(void* thread_v, void* argument_v, void* event_v) {
        auto* thread_obj = static_cast<Thread*>(thread_v);
        auto* arg        = static_cast<ArgT*>(argument_v);
        auto* event      = static_cast<k_event*>(event_v);

        spn_assert(thread_obj != nullptr);
        spn_assert(arg != nullptr);
        spn_assert(event_v != nullptr);

        auto& delegate = thread_obj->_delegate;

        MLOG_DBG(spn_threading, "Thread: thread with name {%s} started.", k_thread_name_get(k_current_get()));

        delegate(arg, ThreadState::STARTING);
        k_event_set_masked(event, U32(ThreadState::RUNNING), U32(ThreadState::MASK_STATE));
        thread_obj->activate_pacing();

        while (k_event_test(event, U32(ThreadState::MASK_STATE)) != U32(ThreadState::STOPPING)) {
            auto flags = k_event_wait(
                event,
                U32(ThreadState::RUNNING | ThreadState::PAUSING | ThreadState::RESUMING | ThreadState::STOPPING),
                false,
                K_FOREVER
            );

            if (flags == U32(ThreadState::STOPPING)) {
                thread_obj->deactivate_pacing();
                break;
            }

            if (flags == U32(ThreadState::RESUMING)) {
                delegate(arg, ThreadState::RESUMING);
                k_event_set_masked(event, U32(ThreadState::RUNNING), U32(ThreadState::MASK_STATE));
                thread_obj->activate_pacing();
            };

            while (k_event_test(event, U32(ThreadState::MASK_STATE)) == U32(ThreadState::RUNNING)) {
                thread_obj->_pacing->wait();
                delegate(arg, ThreadState::RUNNING);
                thread_obj->_pacing->after_iteration();
            }

            auto current_state = k_event_test(event, U32(ThreadState::MASK_STATE));
            if (current_state == U32(ThreadState::PAUSING)) {
                thread_obj->deactivate_pacing();
                delegate(arg, ThreadState::PAUSING);
                k_event_set_masked(event, U32(ThreadState::PAUSED), U32(ThreadState::MASK_STATE));
            } else if (current_state == U32(ThreadState::STOPPING)) {
                thread_obj->deactivate_pacing();
            }
        }

        thread_obj->deactivate_pacing();
        delegate(arg, ThreadState::STOPPING);

        k_event_set_masked(event, U32(ThreadState::STOPPED), U32(ThreadState::MASK_STATE));
        k_thread_abort(k_current_get());

        MLOG_DBG(spn_threading, "Thread: thread with name {%s} halted.", k_thread_name_get(k_current_get()));
    }

private:
    k_thread _thread;
    K_THREAD_STACK_MEMBER(_thread_stack, STACK_SIZE);

    int _thread_priority;

    Delegate _delegate;
    ArgT*    _arg;

    threading::IPacingStrategy* _pacing;
    bool                        _pacing_active;

    k_event       _ev;
    mutable Mutex _mutex;
};

} // namespace spn
