#pragma once

#include "spn/core/launder.hpp"
#include "spn/debugging/assert.hpp"

#include <etl/alignment.h>
#include <etl/memory.h>
#include <etl/utility.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/barrier.h>

#include <cstddef>

namespace spn::queue::detail {

/// Lock-free ring buffer specialized for single-producer/single-consumer access.
template<typename T, size_t Capacity>
class LockfreeRing {
public:
    static_assert(Capacity > 0, "Ring capacity must be greater than zero");
    static_assert(sizeof(atomic_t) >= sizeof(size_t), "atomic_t must be able to hold size_t");

    using value_type = T;

    LockfreeRing()                               = default;
    LockfreeRing(const LockfreeRing&)            = delete;
    LockfreeRing& operator=(const LockfreeRing&) = delete;
    LockfreeRing(LockfreeRing&&)                 = delete;
    LockfreeRing& operator=(LockfreeRing&&)      = delete;

    ~LockfreeRing() { clear(); }

    template<typename... Args>
    bool try_emplace(Args&&... args) {
        size_t write = atomic_get(&_write_count);
        if (!has_room(write)) {
            return false;
        }

        etl::construct_at(ptr(index(write)), etl::forward<Args>(args)...);
        barrier_dmem_fence_full();
        atomic_set(&_write_count, write + 1);
        return true;
    }

    /// Construct item via callable, invoked only if space available. Defers construction until after capacity check.
    template<typename Fn>
    bool try_emplace_with(Fn&& factory) {
        size_t write = atomic_get(&_write_count);
        if (!has_room(write)) {
            return false;
        }

        etl::construct_at(ptr(index(write)), etl::forward<Fn>(factory)());
        barrier_dmem_fence_full();
        atomic_set(&_write_count, write + 1);
        return true;
    }

    bool try_pop(T& out) {
        size_t read = atomic_get(&_read_count);
        if (!has_data(read)) {
            return false;
        }

        auto* slot = spn::launder(ptr(index(read)));
        out        = etl::move(*slot);
        etl::destroy_at(slot);
        barrier_dmem_fence_full();
        atomic_set(&_read_count, read + 1);
        return true;
    }

    [[nodiscard]] bool empty() const { return size() == 0; }

    [[nodiscard]] bool full() const { return size() >= Capacity; }

    [[nodiscard]] size_t size() const {
        auto write = atomic_get(&_write_count);
        auto read  = atomic_get(&_read_count);
        return write - read;
    }

    [[nodiscard]] static constexpr size_t capacity() { return Capacity; }

    void clear() {
        size_t read  = atomic_get(&_read_count);
        size_t write = atomic_get(&_write_count);
        while (read != write) {
            etl::destroy_at(spn::launder(ptr(index(read))));
            ++read;
        }
        atomic_set(&_read_count, write);
        _cached_read  = write;
        _cached_write = write;
    }

private:
    using storage_t = typename etl::aligned_storage<sizeof(T), alignof(T)>::type;

    static constexpr size_t index(size_t count) { return count % Capacity; }

    bool has_room(size_t write) {
        size_t cached = _cached_read;
        if (write - cached >= Capacity) {
            cached = atomic_get(&_read_count);
            barrier_dmem_fence_full();
            _cached_read = cached;
            if (write - cached >= Capacity) {
                return false;
            }
        }
        return true;
    }

    bool has_data(size_t read) {
        size_t cached = _cached_write;
        if (cached - read == 0U) {
            cached = atomic_get(&_write_count);
            barrier_dmem_fence_full();
            _cached_write = cached;
            if (cached - read == 0U) {
                return false;
            }
        }
        return true;
    }

    T* ptr(size_t i) {
        spn_assert(i < Capacity);
        return reinterpret_cast<T*>(&_storage[i]);
    }

private:
    storage_t _storage[Capacity];

    // no cache alignment since MCU's rarely posses caches
    atomic_t _write_count  = ATOMIC_INIT(0);
    atomic_t _read_count   = ATOMIC_INIT(0);
    size_t   _cached_read  = 0;
    size_t   _cached_write = 0;
};

} // namespace spn::queue::detail
