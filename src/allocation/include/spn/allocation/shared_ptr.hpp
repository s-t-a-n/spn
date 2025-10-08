#pragma once

#include "spn/allocation/detail/allocator.hpp"
#include "spn/allocation/detail/deleter.hpp"

#include <etl/atomic.h>
#include <etl/memory.h>
#include <etl/type_traits.h>
#include <etl/utility.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/barrier.h>

// The class rationale is to provide an analogue to std::shared_pointer tailored for refcounted heap/slab use
// The problems of using refcounting on embedded are obvious performance wise, but the safety and origin erasure sure
// can cure a lot of problems!

namespace spn {

namespace detail {

/// Shared control block. Manages refcount and deletion of object and control block
class SharedControlBlock {
public:
    /// Construct control block with object pointer and deletion handlers
    SharedControlBlock(void* ptr, Deleter<void> obj_del, Deleter<void> ctrl_del) noexcept
        : _ptr(ptr), _object_deleter(obj_del), _control_deleter(ctrl_del) {}

    /// Increment reference count
    void add_ref() noexcept { _refcount.fetch_add(1, etl::memory_order_relaxed); }

    /// Decrement reference count. Returns previous count before decrement
    size_t dec_ref() noexcept { return _refcount.fetch_sub(1, etl::memory_order_release); }

    /// Returns current reference count
    size_t ref_count() const noexcept { return _refcount.load(etl::memory_order_acquire); }

    /// Destroy managed object and control block using stored deleters
    void destroy() noexcept {
        const auto object  = _object_deleter;
        const auto control = _control_deleter;
        void*      ptr     = _ptr;

        _ptr             = nullptr;
        _object_deleter  = {};
        _control_deleter = {};
        _refcount.store(0, etl::memory_order_relaxed);

        if (ptr != nullptr && object.function() != nullptr) object(ptr);
        if (control.function() != nullptr) control(this);
    }

    ~SharedControlBlock() = default;

    SharedControlBlock(const SharedControlBlock&)            = delete;
    SharedControlBlock& operator=(const SharedControlBlock&) = delete;
    SharedControlBlock(SharedControlBlock&&)                 = delete;
    SharedControlBlock& operator=(SharedControlBlock&&)      = delete;

private:
    etl::atomic<size_t> _refcount{1};
    void*               _ptr{nullptr};
    Deleter<void>       _object_deleter{};
    Deleter<void>       _control_deleter{};
};

} // namespace detail

/// Control block storage type for allocating shared_ptr control blocks
using ControlBlockStorage = detail::SharedControlBlock;

/// Reference-counted smart pointer
template<typename T>
class shared_ptr {
public:
    using element_type = T;
    using pointer      = T*;
    using reference    = T&;

    static_assert(!etl::is_array_v<T>, "spn::shared_ptr does not support array types");

    constexpr shared_ptr() noexcept = default;

    shared_ptr(detail::SharedControlBlock* control, pointer p) noexcept : _ptr(p), _control(control) {}

    shared_ptr(const shared_ptr& other) noexcept : _ptr(other._ptr), _control(other._control) { add_ref(); }

    shared_ptr(shared_ptr&& other) noexcept : _ptr(other._ptr), _control(other._control) {
        other._ptr     = nullptr;
        other._control = nullptr;
    }

    template<typename U>
        requires(etl::is_convertible_v<U*, T*>)
    shared_ptr(const shared_ptr<U>& other) noexcept : _ptr(static_cast<pointer>(other._ptr)), _control(other._control) {
        add_ref();
    }

    template<typename U>
        requires(etl::is_convertible_v<U*, T*>)
    shared_ptr(shared_ptr<U>&& other) noexcept : _ptr(static_cast<pointer>(other._ptr)), _control(other._control) {
        other._ptr     = nullptr;
        other._control = nullptr;
    }

    ~shared_ptr() { dec_ref(); }

    shared_ptr& operator=(const shared_ptr& other) noexcept {
        if (this == &other || _control == other._control) return *this;
        if (other._control) inc(other._control);
        dec_ref();
        _ptr     = other._ptr;
        _control = other._control;
        return *this;
    }

    shared_ptr& operator=(shared_ptr&& other) noexcept {
        if (this == &other) return *this;
        dec_ref();
        _ptr           = other._ptr;
        _control       = other._control;
        other._ptr     = nullptr;
        other._control = nullptr;
        return *this;
    }

    template<typename U>
        requires(etl::is_convertible_v<U*, T*>)
    shared_ptr& operator=(const shared_ptr<U>& other) noexcept {
        if (_control == other._control) return *this;
        if (other._control) inc(other._control);
        dec_ref();
        _ptr     = static_cast<pointer>(other._ptr);
        _control = other._control;
        return *this;
    }

    template<typename U>
        requires(etl::is_convertible_v<U*, T*>)
    shared_ptr& operator=(shared_ptr<U>&& other) noexcept {
        dec_ref();
        _ptr           = static_cast<pointer>(other._ptr);
        _control       = other._control;
        other._ptr     = nullptr;
        other._control = nullptr;
        return *this;
    }

    shared_ptr& operator=(etl::nullptr_t) noexcept {
        reset();
        return *this;
    }

    pointer get() const noexcept { return _ptr; }

    reference operator*() const noexcept { return *get(); }

    pointer operator->() const noexcept { return get(); }

    /// Returns true if shared_ptr manages an object
    explicit operator bool() const noexcept { return _control != nullptr; }

    /// Returns number of shared_ptr instances sharing ownership. Returns 0 if empty
    size_t use_count() const noexcept { return _control ? _control->ref_count() : 0; }

    /// Returns true if this is the only shared_ptr managing the object
    bool unique() const noexcept { return use_count() == 1; }

    void reset() noexcept { dec_ref(); }
    void reset(etl::nullptr_t) noexcept { reset(); }

    void swap(shared_ptr& other) noexcept {
        pointer                     tmp_ptr  = _ptr;
        detail::SharedControlBlock* tmp_ctrl = _control;
        _ptr                                 = other._ptr;
        _control                             = other._control;
        other._ptr                           = tmp_ptr;
        other._control                       = tmp_ctrl;
    }

private:
    void add_ref() noexcept {
        if (_control) inc(_control);
    }

    static void inc(detail::SharedControlBlock* c) noexcept { c->add_ref(); }

    void dec_ref() noexcept {
        if (!_control) return;
        detail::SharedControlBlock* control = _control;
        _ptr                                = nullptr;
        _control                            = nullptr;
        if (control->dec_ref() == 1) {
            // barrier ensures destructor sees all previous writes to control_block
            barrier_dmem_fence_full();
            control->destroy();
        }
    }

    template<typename U>
    friend class shared_ptr;

    pointer                     _ptr{nullptr};
    detail::SharedControlBlock* _control{nullptr};
};

template<typename T, typename U>
bool operator==(const shared_ptr<T>& lhs, const shared_ptr<U>& rhs) noexcept {
    return lhs.get() == rhs.get();
}

template<typename T>
bool operator==(const shared_ptr<T>& lhs, etl::nullptr_t) noexcept {
    return !lhs;
}

template<typename T>
bool operator==(etl::nullptr_t, const shared_ptr<T>& rhs) noexcept {
    return !rhs;
}

/// Create shared_ptr with object and control block storage. Returns empty shared_ptr on allocation failure or timeout
template<typename T, typename ObjStorage, typename CtrlStorage, typename... Args>
shared_ptr<T> make_shared(ObjStorage& obj_storage, CtrlStorage& ctrl_storage, k_timeout_t timeout, Args&&... args) {
    auto  obj_alloc = obj_storage.template allocator<T>();
    void* obj_raw   = nullptr;
    if (obj_alloc.allocate(&obj_raw, timeout) != 0) {
        return {};
    }
    T* obj = etl::construct_at(static_cast<T*>(obj_raw), etl::forward<Args>(args)...);

    auto obj_deleter_typed = obj_storage.template deleter<T>();
    auto obj_del           = detail::Deleter<void>(obj_deleter_typed.context(), obj_deleter_typed.function());

    auto  ctrl_alloc = ctrl_storage.template allocator<detail::SharedControlBlock>();
    void* ctrl_raw   = nullptr;
    if (ctrl_alloc.allocate(&ctrl_raw, timeout) != 0) {
        obj_deleter_typed(obj);
        return {};
    }

    auto ctrl_deleter_typed = ctrl_storage.template deleter<detail::SharedControlBlock>();
    auto ctrl_del           = detail::Deleter<void>(ctrl_deleter_typed.context(), ctrl_deleter_typed.function());

    auto* ctrl = etl::construct_at(
        static_cast<detail::SharedControlBlock*>(ctrl_raw),
        const_cast<void*>(static_cast<const void*>(obj)),
        obj_del,
        ctrl_del
    );

    return shared_ptr<T>(ctrl, obj);
}

} // namespace spn
