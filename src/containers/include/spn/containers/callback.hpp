#pragma once

#include "spn/threading/refguard.hpp"

#include <etl/atomic.h>
#include <etl/expected.h>
#include <etl/type_traits.h>
#include <etl/utility.h>
#include <zephyr/kernel.h>

#include <cerrno>

namespace spn {

/// Hot-swappable detachable and joinable callback. (ISR safe)
/// Also see callback_delegate.hpp which employs etl::delegate that allows passing non-capturing lambda's
template<typename R, typename... Args>
class callback {
public:
    using callback_f = R (*)(Args...);

    callback() = default;

    ~callback() { (void)_guard.teardown(K_FOREVER); }

    callback(const callback&)            = delete;
    callback& operator=(const callback&) = delete;
    callback(callback&&)                 = delete;
    callback& operator=(callback&&)      = delete;

    /// Attach callback function. Hot-swappable during invoke. Accepts nullptr.
    void attach(callback_f f) {
        _fn.store(f, etl::memory_order_seq_cst);
        _guard.allow_acquisitions();
    }

    /// Detach callback function.
    void detach() {
        _fn.store(nullptr, etl::memory_order_seq_cst);
        _guard.deny_acquisitions();
    }

    /// Wait for active invocations to complete. Returns 0 on success, -ETIMEDOUT on timeout.
    [[nodiscard]] int join(k_timeout_t timeout) const { return _guard.wait_for_release(timeout); }

    /// Invoke attached callback. Returns function result or error code. (ISR safe)
    etl::expected<R, int> invoke(Args... args) const {
        auto ref = _guard.try_acquire_scoped();
        if (!ref) return etl::unexpected{-EAGAIN};

        auto* fn = _fn.load(etl::memory_order_seq_cst);
        if (fn == nullptr) return etl::unexpected{-ENOENT};

        if constexpr (etl::is_void_v<R>) {
            fn(etl::forward<Args>(args)...);
            return etl::expected<void, int>{};
        } else {
            return fn(etl::forward<Args>(args)...);
        }
    }

    /// Returns true if a callback function is attached. (ISR safe)
    bool is_attached() const { return _fn.load(etl::memory_order_seq_cst) != nullptr; }

private:
    etl::atomic<callback_f> _fn{nullptr};
    mutable RefGuard        _guard;
};

} // namespace spn
