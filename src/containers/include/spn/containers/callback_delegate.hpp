#pragma once

#include "spn/threading/refguard.hpp"

#include <etl/atomic.h>
#include <etl/delegate.h>
#include <etl/expected.h>
#include <etl/type_traits.h>
#include <etl/utility.h>
#include <zephyr/kernel.h>
#include <zephyr/sys_clock.h>

#include <cstdint>

namespace spn {

/// Hot-swappable detachable callback backed by etl::delegate. (ISR safe)
/// Also see callback.hpp if a plain function ptr suffices
template<typename R, typename... Args>
class callback_delegate {
public:
    using delegate_t = etl::delegate<R(Args...)>;

    callback_delegate() = default;

    ~callback_delegate() {
        (void)_guard.teardown(K_FOREVER);
        for (auto& slot_guard : _slot_guards)
            (void)slot_guard.teardown(K_FOREVER);
    }

    callback_delegate(const callback_delegate&)            = delete;
    callback_delegate& operator=(const callback_delegate&) = delete;
    callback_delegate(callback_delegate&&)                 = delete;
    callback_delegate& operator=(callback_delegate&&)      = delete;

    /// Attach delegate. Returns 0 on success, -ETIMEDOUT on timeout, -EINVAL for invalid delegate. (ISR safe if used
    /// with K_NO_WAIT)
    [[nodiscard]] int attach(delegate_t fn, k_timeout_t timeout = K_FOREVER) {
        if (!fn.is_valid()) return -EINVAL;

        auto          inactive = static_cast<uint8_t>(_active.load(etl::memory_order_acquire) ^ 1U);
        k_timepoint_t end      = sys_timepoint_calc(timeout);
        while (!_slot_guards[inactive].try_acquire_exclusive()) {
            if (sys_timepoint_expired(end)) return -ETIMEDOUT;
            k_sleep(K_TICKS(1));
        }

        _slots[inactive] = etl::move(fn);
        _slot_guards[inactive].downgrade_exclusive_to_shared();
        _active.store(inactive, etl::memory_order_release);
        _slot_guards[inactive].release();

        _guard.allow_acquisitions();
        return 0;
    }

    /// Detach delegate and block new invocations (ISR safe)
    void detach() { _guard.deny_acquisitions(); }

    /// Wait for active invocations to complete. Returns 0 on success, -ETIMEDOUT on timeout.
    [[nodiscard]] int join(k_timeout_t timeout) const { return _guard.wait_for_release(timeout); }

    /// Invoke attached delegate. Returns function result or error code. (ISR safe)
    etl::expected<R, int> invoke(Args... args) const {
        auto ref = _guard.try_acquire_scoped();
        if (!ref) return etl::unexpected{-EAGAIN};

        const auto slot_index = _active.load(etl::memory_order_acquire);

        delegate_t fn{};
        {
            auto slot_ref = _slot_guards[slot_index].try_acquire_scoped();
            if (!slot_ref) return etl::unexpected{-EAGAIN};
            fn = _slots[slot_index];
        }

        if constexpr (etl::is_void_v<R>) {
            fn(etl::forward<Args>(args)...);
            return etl::expected<void, int>{};
        } else {
            return fn(etl::forward<Args>(args)...);
        }
    }

    /// Returns true if a delegate is currently attached.
    [[nodiscard]] bool is_attached() const { return _guard.is_alive(); }

private:
    delegate_t           _slots[2]{};
    etl::atomic<uint8_t> _active{0};
    mutable RefGuard     _guard{false};
    mutable RefGuard     _slot_guards[2];
};

} // namespace spn
