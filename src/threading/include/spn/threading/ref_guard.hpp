#pragma once

#include <etl/atomic.h>
#include <zephyr/kernel.h>
#include <zephyr/sys_clock.h>

#include <cstdint>

namespace spn {

/// Lock-free reference counter with alive state for coordinating graceful teardown. (ISR safe)
class RefGuard {
public:
    RefGuard() = default;
    ~RefGuard() {
        if (active()) {
            (void)stop(K_FOREVER);
        }
    }

    RefGuard(const RefGuard&)            = delete;
    RefGuard& operator=(const RefGuard&) = delete;
    RefGuard(RefGuard&&)                 = delete;
    RefGuard& operator=(RefGuard&&)      = delete;

    /// Enter critical section by acquiring reference. Returns false if stopped or overflow. (ISR safe)
    [[nodiscard]] bool enter() {
        uint32_t expected = _state.load(etl::memory_order_seq_cst);
        while (true) {
            if (!(expected & ALIVE_BIT) || (expected & REFCOUNT_MASK) == REFCOUNT_MASK) {
                return false;
            }
            if (_state.compare_exchange_weak(
                    expected,
                    expected + 1,
                    etl::memory_order_seq_cst,
                    etl::memory_order_seq_cst
                )) {
                return true;
            }
        }
    }

    /// Leave critical section by releasing reference. (ISR safe)
    void leave() { _state.fetch_sub(1, etl::memory_order_seq_cst); }

    /// Stop guard: clear active bit, wait for all enter() calls to leave(), release base ref. Returns 0 on success,
    /// -ETIMEDOUT on timeout. Thread context only.
    [[nodiscard]] int stop(k_timeout_t timeout = K_FOREVER) {
        if ((_state.load(etl::memory_order_seq_cst) & REFCOUNT_MASK) == 0) {
            return 0; // already stopped
        }

        _state.fetch_and(~ALIVE_BIT, etl::memory_order_seq_cst);

        k_timepoint_t end = sys_timepoint_calc(timeout);

        while ((_state.load(etl::memory_order_seq_cst) & REFCOUNT_MASK) != 1) {
            if (sys_timepoint_expired(end)) {
                return -ETIMEDOUT;
            }
            k_sleep(K_USEC(100));
        }

        _state.fetch_sub(1, etl::memory_order_seq_cst);
        return 0;
    }

    /// Returns true if guard is active. (ISR safe)
    bool active() const { return (_state.load(etl::memory_order_seq_cst) & ALIVE_BIT) != 0; }

    /// Returns current reference count including base reference. For debugging/assertions. (ISR safe)
    uint32_t remaining_refs() const { return _state.load(etl::memory_order_seq_cst) & REFCOUNT_MASK; }

private:
    static constexpr uint32_t ALIVE_BIT     = 1U << 31;
    static constexpr uint32_t REFCOUNT_MASK = ~ALIVE_BIT;

    etl::atomic<uint32_t> _state{ALIVE_BIT | 1};
};

} // namespace spn
