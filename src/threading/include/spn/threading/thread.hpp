#pragma once

#include "spn/core/enum_flags.hpp"
#include "spn/core/types.hpp"
#include "spn/debugging/assert.hpp"
#include "spn/logging/logging.hpp"
#include "spn/threading/mutex.hpp"

#include <etl/delegate.h>
#include <zephyr/kernel.h>

namespace spn {

using namespace spn::core;

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
constexpr const char* as_string(ThreadState state) noexcept {
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

    Thread(Delegate delegate, ArgT* arg, int priority = 10, const char* name = nullptr)
        : _thread_priority(priority), _delegate(delegate), _arg(arg) {
        k_event_init(&_ev);
        k_event_set(&_ev, U32(ThreadState::IDLE));

        auto id = k_thread_create(
            &_thread,
            _thread_stack,
            K_THREAD_STACK_SIZEOF(_thread_stack),
            _thread_entry,
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

    ~Thread() { stop(); }

    /// Start the thread
    /// note: returns -EINVAL if not in IDLE state
    [[nodiscard]] int start() {
        auto lockguard = _mutex.lockguard();

        if (k_event_test(&_ev, U32(ThreadState::IDLE)) == U32(ThreadState::IDLE)) {
            k_thread_start(&_thread);
            k_event_set_masked(&_ev, U32(ThreadState::STARTING), U32(ThreadState::MASK_STATE));
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

        if (flags & U32(ThreadState::RUNNING | ThreadState::PAUSED)) {
            k_event_set_masked(&_ev, U32(ThreadState::STOPPING), U32(ThreadState::MASK_STATE));
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
    static void _thread_entry(void* thread_v, void* argument_v, void* event_v) {
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

        while (k_event_test(event, U32(ThreadState::MASK_STATE)) != U32(ThreadState::STOPPING)) {
            auto flags = k_event_wait(
                event,
                U32(ThreadState::RUNNING | ThreadState::PAUSING | ThreadState::RESUMING | ThreadState::STOPPING),
                false,
                K_FOREVER
            );

            if (flags == U32(ThreadState::STOPPING)) break;

            if (flags == U32(ThreadState::RESUMING)) {
                delegate(arg, ThreadState::RESUMING);
                k_event_set_masked(event, U32(ThreadState::RUNNING), U32(ThreadState::MASK_STATE));
            };

            while (k_event_test(event, U32(ThreadState::MASK_STATE)) == U32(ThreadState::RUNNING)) {
                delegate(arg, ThreadState::RUNNING);
                k_yield(); // todo: yield on a deadline
            }

            if (k_event_test(event, U32(ThreadState::MASK_STATE)) == U32(ThreadState::PAUSING)) {
                delegate(arg, ThreadState::PAUSING);
                k_event_set_masked(event, U32(ThreadState::PAUSED), U32(ThreadState::MASK_STATE));
            }
        }

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

    k_event _ev;
    Mutex   _mutex;
};

} // namespace spn
