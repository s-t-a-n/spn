#pragma once

#include <etl/delegate.h>
#include <zephyr/sys/atomic.h>

namespace spn {

/// Threadsafe container to store a callback function
template<typename T>
class Callback {
public:
    using callback_f = etl::delegate<void(const T&)>;

    Callback() = default;
    explicit Callback(const callback_f& f) { attach(f); }

    /// Attach a callback function
    void attach(const callback_f& f) {
        auto* current = static_cast<callback_f*>(atomic_ptr_get(&_ptr));
        auto  next    = (current == &_slots[0]) ? &_slots[1] : &_slots[0];
        *next         = f;           // prepare new value
        atomic_ptr_set(&_ptr, next); // publish atomically
    }
    /// Detach the callback function
    void detach() { atomic_ptr_set(&_ptr, nullptr); }

    /// Returns true if a callback function is attached
    bool     is_attached() const { return atomic_ptr_get(&_ptr) != nullptr; }
    explicit operator bool() const { return is_attached(); }

    /// Invoke callback with the given value. Returns true if the callback function was called
    bool invoke(const T& v) const {
        auto* p = static_cast<callback_f*>(atomic_ptr_get(&_ptr));
        return p ? p->call_if(v) : false; // safe, no locks
    }

private:
    atomic_ptr_t _ptr = ATOMIC_PTR_INIT(nullptr);
    callback_f   _slots[2]{}; // double-buffered storage
};

} // namespace spn
