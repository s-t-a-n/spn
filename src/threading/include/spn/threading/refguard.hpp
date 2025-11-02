#pragma once

#include "spn/debugging/assert.hpp"

#include <etl/atomic.h>
#include <zephyr/kernel.h>
#include <zephyr/sys_clock.h>

namespace spn {

/// Lock-free reference counter for graceful coordination of shared/exclusive access. (ISR safe)
class RefGuard {
public:
    /// RAII reference that raises the RefGuard by one. Must be checked with `is_acquired` for validity.
    class Ref {
    public:
        explicit Ref(RefGuard& g) : _guard(&g), _acquired(g.try_acquire()) {}
        ~Ref() {
            if (_acquired) _guard->release();
        }
        Ref(const Ref&)            = delete;
        Ref& operator=(const Ref&) = delete;
        Ref(Ref&& other) noexcept : _guard(other._guard), _acquired(other._acquired) { other._acquired = false; }
        Ref& operator=(Ref&& other) noexcept {
            if (this == &other) return *this;
            if (_acquired) _guard->release();
            _guard          = other._guard;
            _acquired       = other._acquired;
            other._acquired = false;
            return *this;
        }

        /// Returns true if reference was successfully acquired
        bool     is_acquired() const { return _acquired; }
        explicit operator bool() const { return _acquired; }

    private:
        RefGuard* _guard;
        bool      _acquired;
    };

    /// Construct RefGuard with optional initial alive state
    explicit RefGuard(bool start_alive = true) : _state{start_alive ? ALIVE_BIT : 0u} {}
    ~RefGuard() {
        if (is_alive()) (void)teardown(K_FOREVER);
    }

    RefGuard(const RefGuard&)            = delete;
    RefGuard& operator=(const RefGuard&) = delete;
    RefGuard(RefGuard&&)                 = delete;
    RefGuard& operator=(RefGuard&&)      = delete;

    /// Returns RAII-lease that auto releases on destruction
    [[nodiscard]] Ref try_acquire_scoped() { return Ref{*this}; }

    /// Enter critical section by acquiring reference. Returns false if stopped, exclusive held, or overflow. (ISR safe)
    [[nodiscard]] bool try_acquire() {
        uint32_t expected = _state.load(etl::memory_order_seq_cst);
        while (true) {
            bool is_alive        = expected & ALIVE_BIT;
            bool is_exclusive    = expected & EXCLUSIVE_BIT;
            bool zero_references = (expected & REFCOUNT_MASK) == REFCOUNT_MASK;
            if (!is_alive || is_exclusive || zero_references) return false;

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
    void release() {
        auto prev = _state.fetch_sub(1, etl::memory_order_seq_cst);
        spn_assert((prev & REFCOUNT_MASK) > 0); // underflow
    }

    /// Acquire exclusive lease blocking all shared acquisitions. Returns false if not alive, exclusive held, or other
    /// refs exist. (ISR safe)
    [[nodiscard]] bool try_acquire_exclusive() {
        uint32_t expected = _state.load(etl::memory_order_seq_cst);
        while (true) {
            bool     is_alive     = expected & ALIVE_BIT;
            bool     is_exclusive = expected & EXCLUSIVE_BIT;
            uint32_t refcount     = expected & REFCOUNT_MASK;
            if (!is_alive || is_exclusive || refcount != 0) return false;

            uint32_t desired = (expected | EXCLUSIVE_BIT) + 1;

            if (_state.compare_exchange_weak(expected, desired, etl::memory_order_seq_cst, etl::memory_order_seq_cst)) {
                return true;
            }
        }
    }

    /// Release exclusive lease allowing shared acquisitions. (ISR safe)
    void release_exclusive() {
        uint32_t expected = _state.load(etl::memory_order_seq_cst);
        while (true) {
            uint32_t desired = (expected & ~EXCLUSIVE_BIT) - 1;

            if (_state.compare_exchange_weak(expected, desired, etl::memory_order_seq_cst, etl::memory_order_seq_cst)) {
                return;
            }
        }
    }

    /// Downgrade an exclusive lease to a shared lease. Returns true if successful. (ISR safe)
    bool downgrade_exclusive_to_shared() {
        uint32_t expected = _state.load(etl::memory_order_seq_cst);
        while (true) {
            const bool     is_alive     = expected & ALIVE_BIT;
            const bool     is_exclusive = expected & EXCLUSIVE_BIT;
            const uint32_t refs         = expected & REFCOUNT_MASK;
            if (!is_alive || !is_exclusive || refs != 1) return false;

            const uint32_t desired = expected & ~EXCLUSIVE_BIT;
            if (_state.compare_exchange_weak(expected, desired, etl::memory_order_seq_cst, etl::memory_order_seq_cst)) {
                return true;
            }
        }
    }

    /// Wait for all references to release. Returns 0 on success, -ETIMEDOUT on timeout.
    [[nodiscard]] int wait_for_release(k_timeout_t timeout = K_FOREVER) const {
        k_timepoint_t end = sys_timepoint_calc(timeout);

        if ((_state.load(etl::memory_order_seq_cst) & REFCOUNT_MASK) == 0) return 0;

        k_yield();

        while ((_state.load(etl::memory_order_seq_cst) & REFCOUNT_MASK) != 0) {
            if (sys_timepoint_expired(end)) return -ETIMEDOUT;
            k_sleep(K_TICKS(1));
        }

        return 0;
    }

    /// Denies future acquisitions (ISR safe)
    void deny_acquisitions() { _state.fetch_and(~ALIVE_BIT, etl::memory_order_seq_cst); }

    /// Allows future acquisitions. (ISR safe)
    void allow_acquisitions() { _state.fetch_or(ALIVE_BIT, etl::memory_order_seq_cst); }

    /// Actualize the teardown of this guard. Halts acquisitions and waits for release
    [[nodiscard]] int teardown(k_timeout_t timeout = K_FOREVER) {
        deny_acquisitions();
        return wait_for_release(timeout);
    }

    /// Returns ephemeral true if guard has references. (ISR safe)
    bool is_alive() const { return (_state.load(etl::memory_order_seq_cst) & ALIVE_BIT) != 0; }

    /// Returns ephemeral reference count. For debugging/assertions. (ISR safe)
    uint32_t ref_count() const { return _state.load(etl::memory_order_seq_cst) & REFCOUNT_MASK; }

private:
    static constexpr uint32_t ALIVE_BIT     = 1u << 31;
    static constexpr uint32_t EXCLUSIVE_BIT = 1u << 30;
    static constexpr uint32_t REFCOUNT_MASK = ~(ALIVE_BIT | EXCLUSIVE_BIT);

    etl::atomic<uint32_t> _state{ALIVE_BIT};
};
} // namespace spn
