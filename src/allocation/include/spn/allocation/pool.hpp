#pragma once

#include "spn/threading/mutex.hpp"
#include "spn/threading/semaphore.hpp"

#include <etl/array.h>
#include <etl/bitset.h>
#include <etl/delegate.h>
#include <etl/numeric.h>
#include <etl/vector.h>
#include <zephyr/kernel.h>

#include <cerrno>

namespace spn {

// Notes:
// - index of elements can be known (although direct mapping is not enabled)
// - is useful when it is not known when a pointer belongs to this pool
// - not specificably ISR-safe, although it should be fine when using K_NO_WAIT timout

/// Fixed-size object pool allocator
template<typename T, size_t N>
class Pool {
public:
    using hook_f = etl::delegate<void(T&, size_t)>;

    Pool() {
        _free.resize(N);
        etl::iota(_free.rbegin(), _free.rend(), size_t{0});
    }

    void attach_on_acquire(hook_f f) { _on_acquire = f; }
    void attach_on_release(hook_f f) { _on_release = f; }

    /// Acquire object from pool
    /// note: returns nullptr on timeout
    T* acquire(k_timeout_t timeout = K_NO_WAIT) {
        if (!_available.take(timeout)) return nullptr;
        _lock.lock();
        const auto id = _free.back();
        _free.pop_back();
        _in_use.set(id);
        auto p = &_table[id];
        _lock.unlock();

        if (_on_acquire.is_valid()) _on_acquire(*p, id);
        return p;
    }

    /// Release object back to pool
    int release(T* p) {
        if (p == nullptr) return -EINVAL;
        auto base = _table.data();
        auto end  = base + N;
        if (p < base || p >= end) return -EINVAL;
        const auto id = static_cast<size_t>(p - base);
        return release(id);
    }

    /// Release object by index
    int release(size_t id) {
        if (id >= N) return -EINVAL;
        _lock.lock();
        if (!_in_use.test(id)) {
            _lock.unlock();
            return -EINVAL;
        }
        auto p = &_table[id];

        _in_use.reset(id);
        _lock.unlock();

        if (_on_release.is_valid()) _on_release(*p, id);

        _lock.lock();
        _free.push_back(id);
        _lock.unlock();
        _available.give();
        return 0;
    }

    /// Get slot index of pointer
    etl::optional<size_t> slot_of(const T* p) const {
        const auto base = _table.data();
        const auto end  = base + N;
        if (p < base || p >= end) return {};
        return static_cast<size_t>(p - base);
    }

    /// Get object by index
    /// note: returns nullptr if not allocated or out of range
    T* try_at(size_t id) {
        if (id >= N) return nullptr;
        _lock.lock();
        auto out = _in_use.test(id) ? &_table[id] : nullptr;
        _lock.unlock();
        return out;
    }

    /// Initialize all pool objects
    void initialize_each(hook_f&& f) {
        for (size_t i = 0; i < N; ++i)
            f.call_if(_table[i], i);
    }

    /// Iterate over allocated objects
    template<class F>
    void for_each_allocated(F&& f) {
        auto lg = _lock.lockguard();
        for (size_t i = 0; i < N; ++i)
            if (_in_use.test(i)) f(_table[i], i);
        _lock.unlock();
    }

    /// Get number of allocated objects
    size_t used() {
        auto       lg = _lock.lockguard();
        const auto c  = _in_use.count();
        return c;
    }
    /// Get total pool capacity
    static size_t capacity() { return N; }

private:
    etl::array<T, N>       _table{};
    etl::vector<size_t, N> _free{};
    etl::bitset<N>         _in_use{};
    Mutex                  _lock{};
    Semaphore<N, N>        _available;

    hook_f _on_acquire{};
    hook_f _on_release{};
};

} // namespace spn
