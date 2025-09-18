#pragma once

#include "spn/debugging/assert.hpp"
#include "spn/threading/condition.hpp"
#include "spn/threading/mutex.hpp"
#include "spn/threading/semaphore.hpp"

#include <etl/queue.h>
#include <zephyr/kernel.h>

namespace spn {

/// Multi-producer multi-consumer queue
template<typename T, size_t N>
class MPMCQueue {
public:
    /// Push item to queue
    /// note: returns false on timeout
    bool push(const T& item, k_timeout_t timeout = K_FOREVER) {
        if (!_empty_sem.take(timeout)) return false;

        _mutex.lock();
        spn_assert(!_queue.full());
        _queue.emplace(item);
        _mutex.unlock();
        _filled_sem.give();

        return true;
    }

    /// Emplace item to queue
    /// note: returns false on timeout
    bool emplace(T&& value, k_timeout_t timeout = K_FOREVER) {
        if (!_empty_sem.take(timeout)) return false;

        _mutex.lock();
        _queue.emplace(std::forward<T>(value));
        _mutex.unlock();
        _filled_sem.give();

        return true;
    }

    /// Pop item from queue
    /// note: returns false on timeout
    bool pop(T& location, k_timeout_t timeout = K_FOREVER) {
        if (!_filled_sem.take(timeout)) return false;

        _mutex.lock();
        spn_assert(!_queue.empty());
        _queue.pop_into(location);
        _mutex.unlock();
        _empty_sem.give();
        return true;
    }

    /// Check if queue is full
    bool full() {
        auto lockguard = _mutex.lockguard();
        return _queue.full();
    }

    /// Check if queue is empty
    bool empty() {
        auto lockguard = _mutex.lockguard();
        return _queue.empty();
    }

    /// Get current size
    size_t size() {
        auto lockguard = _mutex.lockguard();
        return _queue.size();
    }

private:
    etl::queue<T, N> _queue;
    Mutex            _mutex;
    Semaphore<0, N>  _filled_sem;
    Semaphore<N, N>  _empty_sem;
};

} // namespace spn
