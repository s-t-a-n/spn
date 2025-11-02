#pragma once

#include "spn/containers/callback_delegate.hpp"
#include "spn/core/enum_flags.hpp"
#include "spn/core/types.hpp"
#include "spn/debugging/assert.hpp"
#include "spn/logging/logging.hpp"
#include "spn/threading/pacing/pacing_strategy.hpp"
#include "spn/threading/pacing/spin_pacing.hpp"

#include <etl/atomic.h>
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
    using Delegate = etl::delegate<void(ArgT*, ThreadState)>;

    Thread(
        Delegate                    delegate,
        ArgT*                       arg,
        int                         priority = 10,
        const char*                 name     = nullptr,
        threading::IPacingStrategy& pacing   = threading::SpinPacing::instance()
    )
        : _thread{}, _thread_stack{}, _thread_priority{priority}, _delegate{}, _arg(arg), _pacing{&pacing},
          _state_bits{U32(ThreadState::IDLE)}, _ev{} {
        k_event_init(&_ev);
        k_event_set(&_ev, U32(ThreadState::IDLE));

        auto rc = attach(delegate);
        spn_assert(rc == 0);

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

    Thread(const Thread&)            = delete;
    Thread& operator=(const Thread&) = delete;
    Thread(Thread&&)                 = delete;
    Thread& operator=(Thread&&)      = delete;

    ~Thread() {
        detach();
        if (auto rc = stop(); rc != 0) {
            MLOG_WRN(spn_threading, "failed to stop thread {%s} with error=%i", name(), rc);
            abort();
        }
#ifdef CONFIG_OBJ_CORE_THREAD
        k_obj_core_unlink(&_thread.obj_core);
#endif
#ifdef CONFIG_OBJ_CORE_EVENT
        k_obj_core_unlink(&_ev.obj_core);
#endif
    }

    /// Start the thread. Returns -EINVAL if not in IDLE state.
    [[nodiscard]] int start() {
        if (!_delegate.is_attached()) return -ENOENT;
        if (transition(ThreadState::IDLE, ThreadState::STARTING)) {
            k_thread_start(&_thread);
            return 0;
        }
        return -EINVAL;
    }

    /// Pause running thread. Returns -ETIMEDOUT on timeout, -EINVAL if not RUNNING.
    int pause(k_timeout_t timeout = K_FOREVER) {
        auto current = state();

        if (current == ThreadState::PAUSED) return 0;
        if (current != ThreadState::RUNNING) return -EINVAL;

        if (!transition(ThreadState::RUNNING, ThreadState::PAUSING)) return -EINVAL;

        pacing_strategy().interrupt();
        k_wakeup(&_thread);

        if (k_current_get() == &_thread) return 0;

        auto flags = k_event_wait(&_ev, U32(ThreadState::PAUSED), false, timeout);

        if (flags == 0) {
            if (timeout.ticks != 0) MLOG_WRN(spn_threading, "timed out waiting for thread to pause");
            return -ETIMEDOUT;
        }

        if (flags == U32(ThreadState::PAUSED)) return 0;

        MLOG_WRN(spn_threading, "failed to pause thread. Thread is in state: %u", flags);
        return -EINVAL;
    }

    /// Start idle thread or resume paused thread
    [[nodiscard]] int start_or_resume() {
        if (state() == ThreadState::PAUSED) return resume();
        return start();
    }

    /// Resume paused thread. Returns -EINVAL if not in PAUSED state.
    [[nodiscard]] int resume() {
        if (transition(ThreadState::PAUSED, ThreadState::RESUMING)) return 0;
        return -EINVAL;
    }

    /// Stop thread and wait for completion. Returns -ETIMEDOUT on timeout.
    int stop(k_timeout_t timeout = K_FOREVER) {
        k_timepoint_t end = sys_timepoint_calc(timeout);

        do {
            auto current = state();

            if (current == ThreadState::STOPPED) return 0;
            if (current == ThreadState::ABORTED) return -EINVAL;
            if (current == ThreadState::STOPPING) break;

            if (current == ThreadState::IDLE) {
                if (transition(ThreadState::IDLE, ThreadState::STOPPED)) return 0;
                continue;
            }

            if (transition_from_mask(
                    U32(ThreadState::STARTING | ThreadState::RUNNING | ThreadState::RESUMING | ThreadState::PAUSING
                        | ThreadState::PAUSED),
                    ThreadState::STOPPING
                ))
                break;
        } while (!sys_timepoint_expired(end));

        pacing_strategy().interrupt();
        k_wakeup(&_thread);

        if (!K_TIMEOUT_EQ(timeout, K_NO_WAIT) && sys_timepoint_expired(end)) return -ETIMEDOUT;

        if (k_current_get() == &_thread) return 0;

        k_timeout_t remaining = sys_timepoint_timeout(end);
        auto        flags     = k_event_wait(&_ev, U32(ThreadState::STOPPED), false, remaining);

        if (flags == 0) return -ETIMEDOUT;

        if (flags == U32(ThreadState::STOPPED)) return k_thread_join(&_thread, sys_timepoint_timeout(end));

        MLOG_WRN(spn_threading, "failed to stop thread. Thread is in state: %u", flags);
        return -EINVAL;
    }

    /// Forcefully abort the thread. Sets state to ABORTED.
    void abort() {
        if (state() == ThreadState::ABORTED) return;
        k_thread_abort(&_thread);
        _state_bits.store(U32(ThreadState::ABORTED), etl::memory_order_seq_cst);
        k_event_set(&_ev, U32(ThreadState::ABORTED));
    }

    /// Attach  delegate. Returns 0 on success, -ETIMEDOUT on timeout, -EINVAL for invalid delegate.
    [[nodiscard]] int attach(Delegate delegate, k_timeout_t timeout = K_FOREVER) {
        return _delegate.attach(delegate, timeout);
    }

    /// Detach delegate
    void detach() { _delegate.detach(); }

    /// Returns true if a delegate is currently attached
    [[nodiscard]] bool is_attached() const { return _delegate.is_attached(); }

    /// Set pacing strategy when the thread is idle, paused, or halted. Returns -EBUSY if the thread is running or
    /// transitioning.
    [[nodiscard]] int set_pacing_strategy(threading::IPacingStrategy& pacing) {
        if (U32(state())
            & U32(
                ThreadState::RUNNING | ThreadState::STARTING | ThreadState::RESUMING | ThreadState::PAUSING
                | ThreadState::STOPPING
            )) {
            return -EBUSY;
        }

        _pacing.store(&pacing, etl::memory_order_release);
        return 0;
    }

    /// Returns the pacing strategy currently associated with the thread
    threading::IPacingStrategy& pacing_strategy() {
        auto* pacing = _pacing.load(etl::memory_order_acquire);
        spn_assert(pacing != nullptr);
        return *pacing;
    }

    /// Returns the pacing strategy currently associated with the thread
    const threading::IPacingStrategy& pacing_strategy() const {
        auto* pacing = _pacing.load(etl::memory_order_acquire);
        spn_assert(pacing != nullptr);
        return *pacing;
    }

    /// Change thread priority
    void adjust_priority(int new_priority) {
        k_thread_priority_set(&_thread, new_priority);
        _thread_priority.store(new_priority, etl::memory_order_relaxed);
    }

    /// Get thread name
    const char* name() const { return k_thread_name_get(const_cast<k_thread*>(&_thread)); }

    /// Get current thread state
    ThreadState state() const { return static_cast<ThreadState>(_state_bits.load(etl::memory_order_acquire)); }

    /// Get current thread priority
    int priority() const { return _thread_priority.load(etl::memory_order_relaxed); }

private:
    bool transition(ThreadState from, ThreadState to) {
        uint32_t expected = U32(from);
        uint32_t desired  = U32(to);

        if (_state_bits
                .compare_exchange_strong(expected, desired, etl::memory_order_acq_rel, etl::memory_order_acquire)) {
            k_event_set_masked(&_ev, desired, U32(ThreadState::MASK_STATE));
            return true;
        }
        return false;
    }

    bool transition_from_mask(uint32_t from_mask, ThreadState to) {
        uint32_t expected = _state_bits.load(etl::memory_order_acquire);
        uint32_t desired  = U32(to);

        while (expected & from_mask) {
            if (_state_bits
                    .compare_exchange_weak(expected, desired, etl::memory_order_acq_rel, etl::memory_order_acquire)) {
                k_event_set_masked(&_ev, desired, U32(ThreadState::MASK_STATE));
                return true;
            }
        }
        return false;
    }

    static void thread_entry(void* thread_v, void* argument_v, void* event_v) {
        auto* thread_obj = static_cast<Thread*>(thread_v);
        auto* arg        = static_cast<ArgT*>(argument_v);
        auto* event      = static_cast<k_event*>(event_v);

        spn_assert(thread_obj != nullptr);
        spn_assert(arg != nullptr);
        spn_assert(event_v != nullptr);

        auto& delegate = thread_obj->_delegate;

        const auto invoke = [&arg, &delegate](ThreadState state) {
            auto rc = delegate.invoke(arg, state);
            if (rc.has_value()) return 0;
            if (rc.error() != -EAGAIN)
                MLOG_WRN_ONCE(spn_threading, "delegate failed in state %s: err=%d", to_string(state), rc.error());
            k_sleep(K_TICKS(1));
            return rc.error();
        };

        threading::IPacingStrategy* active_pacing = nullptr;

        const auto activate_pacing = [&thread_obj, &active_pacing]() {
            auto* desired = thread_obj->_pacing.load(etl::memory_order_acquire);
            spn_assert(desired != nullptr);
            desired->on_enter_running();
            active_pacing = desired;
        };

        const auto deactivate_pacing = [&active_pacing]() {
            if (active_pacing != nullptr) {
                active_pacing->on_exit_running();
                active_pacing = nullptr;
            }
        };

        const auto run_iterations = [&active_pacing, &invoke, &thread_obj]() {
            while (thread_obj->state() == ThreadState::RUNNING) {
                active_pacing->wait();
                invoke(ThreadState::RUNNING);
                active_pacing->after_iteration();
            }
        };

        const auto handle_resuming = [&thread_obj, &invoke, &activate_pacing]() {
            invoke(ThreadState::RESUMING);
            if (!thread_obj->transition(ThreadState::RESUMING, ThreadState::RUNNING)) {
                MLOG_WRN_ONCE(spn_threading, "RESUMING->RUNNING transition failed");
                return false;
            }
            activate_pacing();
            return true;
        };

        const auto handle_pausing = [&thread_obj, &invoke, &deactivate_pacing]() {
            deactivate_pacing();
            invoke(ThreadState::PAUSING);
            if (!thread_obj->transition(ThreadState::PAUSING, ThreadState::PAUSED)) {
                MLOG_WRN_ONCE(spn_threading, "PAUSING->PAUSED transition failed");
            }
        };

        MLOG_DBG(spn_threading, "thread with name {%s} started.", k_thread_name_get(k_current_get()));

        invoke(ThreadState::STARTING);

        if (!thread_obj->transition(ThreadState::STARTING, ThreadState::RUNNING)) {
            MLOG_WRN_ONCE(spn_threading, "STARTING->RUNNING transition failed, likely stopped during delegate");
            invoke(ThreadState::STOPPING);
            thread_obj->transition(ThreadState::STOPPING, ThreadState::STOPPED);
            thread_obj->abort();
            return;
        }

        activate_pacing();

        while (thread_obj->state() != ThreadState::STOPPING) {
            auto flags = k_event_wait(
                event,
                U32(ThreadState::RUNNING | ThreadState::PAUSING | ThreadState::RESUMING | ThreadState::STOPPING),
                false,
                K_FOREVER
            );

            if (flags == U32(ThreadState::STOPPING)) {
                break;
            }

            if (flags == U32(ThreadState::RESUMING)) {
                if (!handle_resuming()) break;
            }

            run_iterations();

            auto state = thread_obj->state();
            if (state == ThreadState::PAUSING) {
                handle_pausing();
            } else if (state == ThreadState::STOPPING) {
                break;
            }
        }

        deactivate_pacing();
        invoke(ThreadState::STOPPING);

        if (!thread_obj->transition(ThreadState::STOPPING, ThreadState::STOPPED)) {
            MLOG_WRN_ONCE(spn_threading, "STOPPING->STOPPED transition failed");
        }
        MLOG_DBG(spn_threading, "thread with name {%s}: end of life.", k_thread_name_get(k_current_get()));
    }

private:
    using CallbackDelegate = callback_delegate<void, ArgT*, ThreadState>;

    k_thread _thread;
    K_THREAD_STACK_MEMBER(_thread_stack, STACK_SIZE);

    etl::atomic<int> _thread_priority;

    CallbackDelegate _delegate;
    ArgT*            _arg;

    etl::atomic<threading::IPacingStrategy*> _pacing;

    etl::atomic<uint32_t> _state_bits;
    k_event               _ev;
};

} // namespace spn
