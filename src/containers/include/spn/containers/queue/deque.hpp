#pragma once

#include "spn/containers/concepts.hpp"
#include "spn/containers/queue/detail/policies.hpp"
#include "spn/containers/queue/detail/ring.hpp"
#include "spn/threading/semaphore.hpp"

#include <etl/limits.h>
#include <etl/utility.h>
#include <zephyr/kernel.h>

namespace spn::queue {

/// Bounded deque with semaphore-based flow control and policy-based locking. (ISR unsafe)
template<typename T, size_t Capacity, typename LockPolicy>
class Deque {
public:
    using value_type = T;

    Deque()                        = default;
    Deque(const Deque&)            = delete;
    Deque& operator=(const Deque&) = delete;
    Deque(Deque&&)                 = delete;
    Deque& operator=(Deque&&)      = delete;

    ~Deque() { clear(); }

    /// Construct item in place at front, blocking until space available or timeout. Returns false on timeout
    template<typename... Args>
    bool emplace_front(k_timeout_t timeout, Args&&... args) {
        return enqueue(timeout, [&](auto& ring) { ring.emplace_front(etl::forward<Args>(args)...); });
    }

    /// Construct item in place at back, blocking until space available or timeout. Returns false on timeout
    template<typename... Args>
    bool emplace_back(k_timeout_t timeout, Args&&... args) {
        return enqueue(timeout, [&](auto& ring) { ring.emplace_back(etl::forward<Args>(args)...); });
    }

    /// Construct item in place at back, blocking until space available or timeout. Returns false on timeout
    template<typename... Args>
    bool emplace(k_timeout_t timeout, Args&&... args) {
        return emplace_back(timeout, etl::forward<Args>(args)...);
    }

    /// Push item to front, blocking until space available or timeout. Returns false on timeout
    bool push_front(const T& value, k_timeout_t timeout = K_FOREVER) {
        return enqueue(timeout, [&](auto& ring) { ring.emplace_front(value); });
    }

    /// Push item to front by move, blocking until space available or timeout. Returns false on timeout
    bool push_front(T&& value, k_timeout_t timeout = K_FOREVER) {
        return enqueue(timeout, [&](auto& ring) { ring.emplace_front(etl::move(value)); });
    }

    /// Push item to back, blocking until space available or timeout. Returns false on timeout
    bool push_back(const T& value, k_timeout_t timeout = K_FOREVER) {
        return enqueue(timeout, [&](auto& ring) { ring.emplace_back(value); });
    }

    /// Push item to back by move, blocking until space available or timeout. Returns false on timeout
    bool push_back(T&& value, k_timeout_t timeout = K_FOREVER) {
        return enqueue(timeout, [&](auto& ring) { ring.emplace_back(etl::move(value)); });
    }

    /// Push item to back, blocking until space available or timeout. Returns false on timeout
    bool push(const T& value, k_timeout_t timeout = K_FOREVER) { return push_back(value, timeout); }

    /// Push item to back by move, blocking until space available or timeout. Returns false on timeout
    bool push(T&& value, k_timeout_t timeout = K_FOREVER) { return push_back(etl::move(value), timeout); }

    /// Pop item from front, blocking until item available or timeout. Returns false on timeout
    bool pop_front(T& out, k_timeout_t timeout = K_FOREVER) {
        return dequeue(timeout, [&](auto& ring) { ring.pop_front(out); });
    }

    /// Pop item from back, blocking until item available or timeout. Returns false on timeout
    bool pop_back(T& out, k_timeout_t timeout = K_FOREVER) {
        return dequeue(timeout, [&](auto& ring) { ring.pop_back(out); });
    }

    /// Pop item from front, blocking until item available or timeout. Returns false on timeout
    bool pop(T& out, k_timeout_t timeout = K_FOREVER) { return pop_front(out, timeout); }

    [[nodiscard]] bool                    empty() const { return _filled.count() == 0; }
    [[nodiscard]] bool                    full() const { return _empty.count() == 0; }
    [[nodiscard]] size_t                  size() const { return static_cast<size_t>(_filled.count()); }
    [[nodiscard]] static constexpr size_t capacity() { return Capacity; }

    /// Destroy all items and reset internal state. Must only be called when no producers or consumers are active
    void clear() {
        auto lg = _policy.clear_lock();
        _ring.clear();
        while (_filled.take(K_NO_WAIT) == 0) {
            _empty.give();
        }
    }

protected:
    template<typename Fn>
    bool enqueue(k_timeout_t timeout, Fn&& fn) {
        if (_empty.take(timeout) != 0) return false;
        {
            auto lg = _policy.producer_lock();
            fn(_ring);
        }
        _filled.give();
        return true;
    }

    template<typename Fn>
    bool dequeue(k_timeout_t timeout, Fn&& fn) {
        if (_filled.take(timeout) != 0) return false;
        {
            auto lg = _policy.consumer_lock();
            fn(_ring);
        }
        _empty.give();
        return true;
    }

protected:
    static_assert(
        Capacity <= static_cast<size_t>(etl::numeric_limits<int>::max()),
        "Capacity must fit into the semaphore integer limits"
    );

    detail::Ring<T, Capacity>                                                 _ring{};
    LockPolicy                                                                _policy{};
    mutable Semaphore<0, static_cast<int>(Capacity)>                          _filled;
    mutable Semaphore<static_cast<int>(Capacity), static_cast<int>(Capacity)> _empty;
};

} // namespace spn::queue
