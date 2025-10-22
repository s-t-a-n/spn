#pragma once

#include "spn/core/launder.hpp"
#include "spn/debugging/assert.hpp"

#include <etl/alignment.h>
#include <etl/memory.h>
#include <etl/utility.h>

#include <cstddef>

namespace spn::queue::detail {

/// Fixed-capacity ring buffer with manual storage management. Not inherently thread-safe.
template<typename T, size_t Capacity>
class Ring {
public:
    static_assert(Capacity > 0, "Ring cannot be empty");

    using value_type = T;

    constexpr Ring() = default;
    ~Ring() { clear(); }

    Ring(const Ring&)            = delete;
    Ring& operator=(const Ring&) = delete;
    Ring(Ring&&)                 = delete;
    Ring& operator=(Ring&&)      = delete;

    [[nodiscard]] bool                    empty() const { return _size == 0U; }
    [[nodiscard]] bool                    full() const { return _size == Capacity; }
    [[nodiscard]] size_t                  size() const { return _size; }
    [[nodiscard]] static constexpr size_t capacity() { return Capacity; }

    template<typename... Args>
    void emplace_back(Args&&... args) {
        spn_assert(!full());
        etl::construct_at(ptr(_tail), etl::forward<Args>(args)...);
        _tail = advance(_tail);
        ++_size;
    }

    template<typename... Args>
    void emplace_front(Args&&... args) {
        spn_assert(!full());
        _head = retreat(_head);
        etl::construct_at(ptr(_head), etl::forward<Args>(args)...);
        ++_size;
    }

    void pop_front(T& out) {
        spn_assert(!empty());
        T* slot = spn::launder(ptr(_head));
        out     = etl::move(*slot);
        etl::destroy_at(slot);
        _head = advance(_head);
        --_size;
    }

    void pop_back(T& out) {
        spn_assert(!empty());
        _tail   = retreat(_tail);
        T* slot = spn::launder(ptr(_tail));
        out     = etl::move(*slot);
        etl::destroy_at(slot);
        --_size;
    }

    void discard_front() {
        spn_assert(!empty());
        etl::destroy_at(spn::launder(ptr(_head)));
        _head = advance(_head);
        --_size;
    }

    void discard_back() {
        spn_assert(!empty());
        _tail = retreat(_tail);
        etl::destroy_at(spn::launder(ptr(_tail)));
        --_size;
    }

    void clear() {
        while (!empty()) {
            discard_front();
        }
    }

private:
    using storage_t = typename etl::aligned_storage<sizeof(T), alignof(T)>::type;

    static constexpr size_t advance(size_t index) { return (index + 1U) >= Capacity ? 0U : (index + 1U); }
    static constexpr size_t retreat(size_t index) { return index == 0U ? Capacity - 1U : index - 1U; }

    T* ptr(size_t index) {
        spn_assert(index < Capacity);
        return reinterpret_cast<T*>(&_storage[index]);
    }
    const T* ptr(size_t index) const {
        spn_assert(index < Capacity);
        return reinterpret_cast<const T*>(&_storage[index]);
    }

    storage_t _storage[Capacity];
    size_t    _head{0};
    size_t    _tail{0};
    size_t    _size{0};
};

} // namespace spn::queue::detail
