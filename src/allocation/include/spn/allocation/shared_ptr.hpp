#pragma once

#include "spn/logging/logging.hpp"

#include <etl/atomic.h>
#include <zephyr/kernel.h>

namespace spn {

/// Allocator concept for heap-like interfaces
template<typename T>
concept HeapLike = requires(T b, void* ptr, size_t bytes) {
    { b.alloc(&ptr, bytes) } -> std::convertible_to<int>;
    { b.release(&ptr) } -> std::convertible_to<void>;
};

// note: written since ETL doesnt provide shared_ptr or similar mechanicry

/// Reference-counted smart pointer with custom allocator
template<typename T, HeapLike PoolType>
class SharedPtr {
public:
    using underlying_t = T;
    using pool_t       = PoolType;
    using refcount_t   = etl::atomic<size_t>;

    /// Control block for reference counting
    struct ControlBlock {
        pool_t*    _pool;
        refcount_t _refcount;
        T          _data;

        template<typename... Args>
        explicit ControlBlock(pool_t* pool, Args&&... args)
            : _pool(pool), _refcount(1), _data(std::forward<Args>(args)...) {}
    };

    struct Static {};

    SharedPtr() noexcept = default;

    explicit SharedPtr(pool_t& pool) : _control(allocate_and_construct(pool)) {}

    template<typename... Args>
        requires std::constructible_from<T, Args...>
    SharedPtr(pool_t& pool, std::in_place_t, Args&&... args)
        : _control(allocate_and_construct(pool, std::forward<Args>(args)...)) {}

    SharedPtr(const SharedPtr& other) noexcept : _control(other._control) { add_ref(); }

    SharedPtr(SharedPtr&& other) noexcept : _control(other._control) { other._control = nullptr; }

    template<typename... Args>
        requires std::constructible_from<T, Args...>
    SharedPtr(ControlBlock* other_cb, Args&&... args) noexcept : _control(other_cb) {
        _control->_data = {std::forward<Args>(args)...};
        add_ref();
    }

    ~SharedPtr() { release(); }

    SharedPtr& operator=(const SharedPtr& other) noexcept {
        if (this == &other) return *this;
        if (other._control) inc(other._control);
        release();
        _control = other._control;
        return *this;
    }

    SharedPtr& operator=(SharedPtr&& other) noexcept {
        if (this == &other) return *this;
        release();
        _control       = other._control;
        other._control = nullptr;
        return *this;
    }

    /// Get pointer to managed object
    T*       get() noexcept { return _control ? std::addressof(_control->_data) : nullptr; }
    const T* get() const noexcept { return _control ? std::addressof(_control->_data) : nullptr; }

    /// Dereference managed object
    T&       operator*() noexcept { return _control->_data; }
    const T& operator*() const noexcept { return _control->_data; }

    /// Access managed object member
    T*       operator->() noexcept { return std::addressof(_control->_data); }
    const T* operator->() const noexcept { return std::addressof(_control->_data); }

    /// Check if pointer is not null
    explicit operator bool() const noexcept { return _control != nullptr; }

    /// Get reference count
    std::size_t use_count() const noexcept {
        if (!_control) return 0;
        return _control->_refcount.load(std::memory_order_acquire);
    }

    /// Check if this is the only reference
    bool unique() const noexcept { return use_count() == 1; }

    /// Reset to empty state
    void reset() noexcept { release(); }

    /// Reset with new object
    template<typename... Args>
        requires std::constructible_from<T, Args...>
    void reset(pool_t& pool, Args&&... args) {
        release();
        _control = allocate_and_construct(pool, std::forward<Args>(args)...);
    }

    /// Swap with another shared pointer
    void swap(SharedPtr& other) noexcept {
        auto* tmp      = _control;
        _control       = other._control;
        other._control = tmp;
    }

    /// Create external static control blocks
    template<typename... Args>
    static ControlBlock make_control_block(Args&&... args) {
        return construct(std::forward<Args>(args)...);
    }

private:
    void add_ref() noexcept {
        if (_control) inc(_control);
    }

    static void inc(ControlBlock* c) noexcept {
        MLOG_INF(spn_allocation, "Increasing shared ptr ref count");

        c->_refcount.fetch_add(1, std::memory_order_relaxed);
    }

    static bool dec_and_test_zero(ControlBlock* c) noexcept {
        MLOG_INF(spn_allocation, "Decreasing shared ptr ref count");
        return c->_refcount.fetch_sub(1, std::memory_order_acq_rel) == 1;
    }

    void release() noexcept {
        auto* c = _control;
        if (!c) return;
        _control = nullptr;
        if (dec_and_test_zero(c)) {
            c->_data.~T();
            auto* pool = c->_pool;
            c->~ControlBlock();
            if (pool) pool->release(static_cast<void*>(c));
            MLOG_INF(spn_allocation, "Releasing shared ptr");
        }
    }

    template<typename... Args>
    static ControlBlock* allocate_and_construct(pool_t& pool, Args&&... args) {
        void* raw{};
        if (pool.alloc_aligned(&raw, alignof(ControlBlock), sizeof(ControlBlock)) != 0) return nullptr;
        return new (raw) ControlBlock(&pool, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static ControlBlock construct(Args&&... args) {
        return ControlBlock(nullptr, std::forward<Args>(args)...);
    }

private:
    ControlBlock* _control{nullptr};
};

/// Allocate new shared pointer
template<typename T, HeapLike PoolType, typename... Args>
auto make_shared_ptr(PoolType& pool, Args&&... args) -> SharedPtr<T, PoolType> {
    return SharedPtr<T, PoolType>(pool, std::in_place, std::forward<Args>(args)...);
}

/// Reuse control block or allocate new
/// note: not thread-safe
template<typename T, HeapLike PoolType, typename... Args>
auto reuse_or_realloc(typename SharedPtr<T, PoolType>::ControlBlock* ext_cb, PoolType& pool, Args&&... args)
    -> SharedPtr<T, PoolType> {
    if (ext_cb->_refcount.load(std::memory_order_acquire) == 1)
        return SharedPtr<T, PoolType>(ext_cb, std::forward<Args>(args)...);
    return make_shared_ptr<T, PoolType>(pool, std::forward<Args>(args)...);
}

} // namespace spn
