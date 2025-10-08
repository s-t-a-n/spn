#pragma once

// Type-erased allocator pattern:
// - Enables smart pointers to work with any storage backend (Heap/Pool/Slab)
// - Avoids templating smart pointers on allocator type (e.g., shared_ptr<T> not shared_ptr<T, AllocatorType>)
// - Uses function pointers instead of vtables for zero-overhead polymorphism
// - Each storage backend provides allocator<T>() factory method that wraps backend-specific allocation

#include <zephyr/kernel.h>

namespace spn::detail {

using allocator_fn_t = int (*)(void*, void**, k_timeout_t);

/// origin-erased allocator for smart pointers
template<typename T>
class Allocator {
public:
    constexpr Allocator() noexcept = default;
    constexpr Allocator(void* ctx, allocator_fn_t fn) noexcept : _ctx(ctx), _fn(fn) {}

    int allocate(void** out, k_timeout_t timeout = K_NO_WAIT) const noexcept {
        if (out == nullptr || _fn == nullptr) return -EINVAL;
        return _fn(_ctx, out, timeout);
    }

    void*          context() const noexcept { return _ctx; }
    allocator_fn_t function() const noexcept { return _fn; }

private:
    void*          _ctx{nullptr};
    allocator_fn_t _fn{nullptr};
};

} // namespace spn::detail
