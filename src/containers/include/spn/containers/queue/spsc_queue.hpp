#pragma once

#include "spn/containers/concepts.hpp"
#include "spn/containers/queue/detail/lockfree_ring.hpp"

#include <etl/utility.h>

namespace spn {

/// Single-producer single-consumer bounded queue with lock-free try operations. (ISR safe)
template<typename T, size_t Capacity>
class SPSCQueue {
public:
    static_assert(Capacity > 0, "Queue capacity must be greater than zero");

    using value_type          = T;
    using single_consumer_tag = SingleConsumer;

    SPSCQueue()                            = default;
    ~SPSCQueue()                           = default;
    SPSCQueue(const SPSCQueue&)            = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;
    SPSCQueue(SPSCQueue&&)                 = delete;
    SPSCQueue& operator=(SPSCQueue&&)      = delete;

    /// Try to push item without blocking. Returns false when the queue is full.
    bool try_push(const T& value) { return _ring.try_emplace(value); }

    /// Try to push item by move without blocking. Returns false when the queue is full. Defers move until space
    /// confirmed.
    bool try_push(T&& value) {
        return _ring.try_emplace_with([&] { return etl::move(value); });
    }

    template<typename... Args>
    /// Try to construct item in place without blocking. Returns false when the queue is full.
    bool try_emplace(Args&&... args) {
        return _ring.try_emplace(etl::forward<Args>(args)...);
    }

    /// Try to pop item without blocking. Returns false when the queue is empty.
    bool try_pop(T& out) { return _ring.try_pop(out); }

    [[nodiscard]] bool                    empty() const { return _ring.empty(); }
    [[nodiscard]] bool                    full() const { return _ring.full(); }
    [[nodiscard]] size_t                  size() const { return _ring.size(); }
    [[nodiscard]] static constexpr size_t capacity() { return Capacity; }

    /// Destroy buffered items without synchronization. Only call once producers and consumers stop.
    void clear() { _ring.clear(); }

private:
    queue::detail::LockfreeRing<T, Capacity> _ring{};
};

} // namespace spn
