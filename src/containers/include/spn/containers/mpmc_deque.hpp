#pragma once

#include "spn/debugging/assert.hpp"
#include "spn/threading/mutex.hpp"
#include "spn/threading/semaphore.hpp"

#include <etl/type_traits.h>
#include <etl/utility.h>
#include <zephyr/kernel.h>

namespace spn {

/// Multi-producer multi-consumer deque
template<typename T, size_t N>
class MPMCDeque {
public:
    static_assert(N > 0);
    static constexpr size_t capacity = N;

    MPMCDeque() = default;
    ~MPMCDeque() { clear(); }

    MPMCDeque(const MPMCDeque&)            = delete;
    MPMCDeque& operator=(const MPMCDeque&) = delete;
    MPMCDeque(MPMCDeque&&)                 = delete;
    MPMCDeque& operator=(MPMCDeque&&)      = delete;

    /// Push item to back
    /// note: returns false on timeout
    bool push_back(const T& item, k_timeout_t timeout = K_FOREVER) {
        if (!_empty_sem.take(timeout)) return false;

        _mutex.lock();
        spn_assert(_size_count < N);
        construct_at(_tail_index, item);
        _tail_index = advance(_tail_index);
        ++_size_count;
        _mutex.unlock();

        _filled_sem.give();
        return true;
    }

    /// Emplace item at back
    /// note: returns false on timeout
    bool emplace_back(T&& value, k_timeout_t timeout = K_FOREVER) {
        if (!_empty_sem.take(timeout)) return false;

        _mutex.lock();
        spn_assert(_size_count < N);
        construct_at(_tail_index, std::move(value));
        _tail_index = advance(_tail_index);
        ++_size_count;
        _mutex.unlock();

        _filled_sem.give();
        return true;
    }

    /// Push item to front
    /// note: returns false on timeout
    bool push_front(const T& item, k_timeout_t timeout = K_FOREVER) {
        if (!_empty_sem.take(timeout)) return false;

        _mutex.lock();
        spn_assert(_size_count < N);
        _head_index = retreat(_head_index);
        construct_at(_head_index, item);
        ++_size_count;
        _mutex.unlock();

        _filled_sem.give();
        return true;
    }

    /// Emplace item at front
    /// note: returns false on timeout
    bool emplace_front(T&& value, k_timeout_t timeout = K_FOREVER) {
        if (!_empty_sem.take(timeout)) return false;

        _mutex.lock();
        spn_assert(_size_count < N);
        _head_index = retreat(_head_index);
        construct_at(_head_index, std::move(value));
        ++_size_count;
        _mutex.unlock();

        _filled_sem.give();
        return true;
    }

    /// Pop item from front
    /// note: returns false on timeout
    bool pop_front(T& out, k_timeout_t timeout = K_FOREVER) {
        if (!_filled_sem.take(timeout)) return false;

        _mutex.lock();
        spn_assert(_size_count > 0);
        move_out(_head_index, out);
        _head_index = advance(_head_index);
        --_size_count;
        _mutex.unlock();

        _empty_sem.give();
        return true;
    }

    /// Pop item from back
    /// note: returns false on timeout
    bool pop_back(T& out, k_timeout_t timeout = K_FOREVER) {
        if (!_filled_sem.take(timeout)) return false;

        _mutex.lock();
        spn_assert(_size_count > 0);
        _tail_index = retreat(_tail_index);
        move_out(_tail_index, out);
        --_size_count;
        _mutex.unlock();

        _empty_sem.give();
        return true;
    }

    /// Check if deque is empty
    bool empty() {
        auto g = _mutex.lockguard();
        return _size_count == 0;
    }

    /// Check if deque is full
    bool full() {
        auto g = _mutex.lockguard();
        return _size_count == N;
    }

    /// Get current size
    size_t size() {
        auto g = _mutex.lockguard();
        return _size_count;
    }

private:
    using storage_t = std::aligned_storage_t<sizeof(T), alignof(T)>;

    static constexpr size_t advance(size_t i) { return (i + 1) % N; }
    static constexpr size_t retreat(size_t i) { return (i + N - 1) % N; }

    T* ptr(size_t i) { return reinterpret_cast<T*>(&_storage[i]); }

    template<typename U>
    void construct_at(size_t i, U&& v) {
        new (&_storage[i]) T(etl::forward<U>(v));
    }

    void destroy_at(size_t i) { ptr(i)->~T(); }

    void move_out(size_t i, T& out) {
        T* p = ptr(i);
        out  = etl::move(*p);
        destroy_at(i);
    }

    void clear() {
        _mutex.lock();
        while (_size_count) {
            destroy_at(_head_index);
            _head_index = advance(_head_index);
            --_size_count;
        }
        _mutex.unlock();
    }

private:
    storage_t _storage[N];
    size_t    _head_index{0};
    size_t    _tail_index{0};
    size_t    _size_count{0};

    Mutex           _mutex;
    Semaphore<0, N> _filled_sem;
    Semaphore<N, N> _empty_sem;
};

} // namespace spn
