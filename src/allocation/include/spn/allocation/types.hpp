#pragma once

// Type-erased deleter pattern:
// - Enables smart pointers to return objects to heterogeneous allocator sources
// - Independent from Allocator (unique_ptr only needs deleter)
// - Each storage backend provides deleter<T>() factory method that wraps backend-specific deallocation
// - Does not support unique_ptr base conversion

#include <etl/type_traits.h>
#include <zephyr/kernel.h>

namespace spn {

using deleter_fn_t = void (*)(void*, void*);

/// origin-erased deleter for smart pointers
template<typename T>
class Deleter {
public:
    using pointer = T*;

    constexpr Deleter() noexcept = default;
    constexpr Deleter(void* ctx, deleter_fn_t fn) noexcept : _ctx(ctx), _fn(fn) {}

    constexpr Deleter(const Deleter&) noexcept = default;
    constexpr Deleter(Deleter&&) noexcept      = default;

    template<typename U>
        requires(etl::is_convertible_v<U*, T*>)
    constexpr Deleter(const Deleter<U>& other) noexcept : _ctx(other.context()), _fn(other.function()) {
        static_assert(
            etl::is_same_v<etl::remove_cv_t<U>, etl::remove_cv_t<T>>,
            "type erasure forbids base conversion as a wrong pointer would be deleted"
        );
    }

    template<typename U>
        requires(etl::is_convertible_v<U*, T*>)
    constexpr Deleter(Deleter<U>&& other) noexcept : _ctx(other.context()), _fn(other.function()) {
        static_assert(
            etl::is_same_v<etl::remove_cv_t<U>, etl::remove_cv_t<T>>,
            "type erasure forbids base conversion as a wrong pointer would be deleted"
        );
    }

    Deleter& operator=(const Deleter&) noexcept = default;
    Deleter& operator=(Deleter&&) noexcept      = default;

    void operator()(pointer ptr) const noexcept {
        if (ptr == nullptr || _fn == nullptr) return;
        _fn(_ctx, const_cast<void*>(static_cast<const void*>(ptr)));
    }

    void*        context() const noexcept { return _ctx; }
    deleter_fn_t function() const noexcept { return _fn; }

private:
    template<typename>
    friend class Deleter;

    void*        _ctx{nullptr};
    deleter_fn_t _fn{nullptr};
};

// Type-erased allocator pattern:
// - Enables smart pointers to work with any storage backend (Heap/Pool/Slab)
// - Avoids templating smart pointers on allocator type (e.g., shared_ptr<T> not shared_ptr<T, AllocatorType>)
// - Uses function pointers instead of vtables for zero-overhead polymorphism
// - Each storage backend provides allocator<T>() factory method that wraps backend-specific allocation

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

} // namespace spn
