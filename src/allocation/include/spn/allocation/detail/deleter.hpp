#pragma once

// Type-erased deleter pattern:
// - Enables smart pointers to return objects to heterogeneous allocator sources
// - Independent from Allocator (unique_ptr only needs deleter)
// - Each storage backend provides deleter<T>() factory method that wraps backend-specific deallocation
// - Does not support unique_ptr base conversion

#include <etl/type_traits.h>

namespace spn::detail {

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

} // namespace spn::detail
