#pragma once

#include "spn/threading/mutex.hpp"
#include "spn/threading/semaphore.hpp"

#include <zephyr/kernel.h>

#include <new>
#include <type_traits>

namespace spn {

/// Multi-producer single-consumer bounded queue
template<typename T, size_t N>
class MPSCQueue {
public:
    MPSCQueue()  = default;
    ~MPSCQueue() = default;

    MPSCQueue(const MPSCQueue&)            = delete;
    MPSCQueue& operator=(const MPSCQueue&) = delete;
    MPSCQueue(MPSCQueue&&)                 = delete;
    MPSCQueue& operator=(MPSCQueue&&)      = delete;

    /// Push item to queue
    /// note: returns false on timeout
    bool push(const T& item, k_timeout_t timeout = K_FOREVER) {
        if (!_empty_sem.take(timeout)) return false;

        {
            auto lg = _producer_mutex.lockguard();
            construct_at(_write_idx, item);
            advance(_write_idx);
        }

        _filled_sem.give();
        return true;
    }

    /// Emplace item to queue
    /// note: returns false on timeout
    bool emplace(T&& value, k_timeout_t timeout = K_FOREVER) {
        if (!_empty_sem.take(timeout)) return false;

        {
            auto lg = _producer_mutex.lockguard();
            construct_at(_write_idx, etl::move(value));
            advance(_write_idx);
        }

        _filled_sem.give();
        return true;
    }

    /// Pop item from queue
    /// note: returns false on timeout
    bool pop(T& out, k_timeout_t timeout = K_FOREVER) {
        if (!_filled_sem.take(timeout)) return false;

        // single consumer: no mutex on the read path
        auto idx = _read_idx;
        T*   ptr = ptr_at(idx);
        out      = etl::move(*ptr);
        destroy_at(idx);
        advance(_read_idx);
        _empty_sem.give();
        return true;
    }

    /// Check if queue is full
    bool full() const { return _empty_sem.count() == 0; }
    /// Check if queue is empty
    bool empty() const { return _filled_sem.count() == 0; }
    /// Get queue size
    size_t size() const { return static_cast<size_t>(_filled_sem.count()); }

private:
    using storage_t = typename std::aligned_storage<sizeof(T), alignof(T)>::type;

    static constexpr size_t next(size_t i) { return (i + 1) >= N ? 0 : (i + 1); }

    T*       ptr_at(size_t i) { return reinterpret_cast<T*>(&_buffer[i]); }
    const T* ptr_at(size_t i) const { return reinterpret_cast<const T*>(&_buffer[i]); }

    template<typename U>
    void construct_at(size_t i, U&& value) {
        ::new (static_cast<void*>(ptr_at(i))) T(static_cast<U&&>(value));
    }

    void destroy_at(size_t i) { ptr_at(i)->~T(); }

    void advance(size_t& i) { i = next(i); }

    storage_t _buffer[N];

    // indices are only accessed under their respective side's ownership:
    // - _write_idx under producer mutex; potentially many producers
    // - _read_idx exclusively by the single consumer
    size_t _write_idx{0};
    size_t _read_idx{0};

    mutable Mutex   _producer_mutex;
    Semaphore<0, N> _filled_sem; // number of items available
    Semaphore<N, N> _empty_sem;  // number of free slots
};

} // namespace spn
