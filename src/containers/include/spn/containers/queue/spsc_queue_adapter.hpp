#pragma once

#include "spn/containers/concepts.hpp"

#include <etl/utility.h>
#include <zephyr/kernel.h>
#ifdef CONFIG_OBJ_CORE_SEM
#    include <zephyr/kernel/obj_core.h>
#endif

namespace spn::queue {

/// Adapter that wraps a non-blocking SPSC queue to satisfy the Queue concept and provides blocking semantics. (ISR
/// unsafe)
template<typename Queue>
class SPSCQueueAdapter {
public:
    using value_type          = typename Queue::value_type;
    using single_consumer_tag = SingleConsumer;

    explicit SPSCQueueAdapter(Queue& queue) : _queue(queue) {
        constexpr auto cap = Queue::capacity();
        static_assert(cap <= K_SEM_MAX_LIMIT, "Queue capacity exceeds semaphore limit");

        const auto populated = _queue.size();
        const auto available = cap - populated;

        k_sem_init(&_slots, static_cast<unsigned int>(available), static_cast<unsigned int>(cap));
        k_sem_init(&_items, static_cast<unsigned int>(populated), static_cast<unsigned int>(cap));
    }

    ~SPSCQueueAdapter() {
#ifdef CONFIG_OBJ_CORE_SEM
        k_obj_core_unlink(&_slots.obj_core);
        k_obj_core_unlink(&_items.obj_core);
#endif
    }

    /// Push item, blocking until space available or timeout. Returns false on timeout
    bool push(const value_type& value, k_timeout_t timeout = K_NO_WAIT) {
        if (k_sem_take(&_slots, timeout) != 0) {
            return false;
        }
        if (_queue.try_push(value)) {
            k_sem_give(&_items);
            return true;
        }
        k_sem_give(&_slots);
        return false;
    }

    /// Push item by move, blocking until space available or timeout. Returns false on timeout
    bool push(value_type&& value, k_timeout_t timeout = K_NO_WAIT) {
        if (k_sem_take(&_slots, timeout) != 0) {
            return false;
        }
        if (_queue.try_push(etl::move(value))) {
            k_sem_give(&_items);
            return true;
        }
        k_sem_give(&_slots);
        return false;
    }

    /// Construct item in place, blocking until space available or timeout. Returns false on timeout
    template<typename... Args>
    bool emplace(k_timeout_t timeout, Args&&... args) {
        if (k_sem_take(&_slots, timeout) != 0) {
            return false;
        }
        if (_queue.try_emplace(etl::forward<Args>(args)...)) {
            k_sem_give(&_items);
            return true;
        }
        k_sem_give(&_slots);
        return false;
    }

    /// Pop item, blocking until item available or timeout. Returns false on timeout
    bool pop(value_type& out, k_timeout_t timeout = K_NO_WAIT) {
        if (k_sem_take(&_items, timeout) != 0) {
            return false;
        }
        if (_queue.try_pop(out)) {
            k_sem_give(&_slots);
            return true;
        }
        k_sem_give(&_items);
        return false;
    }

    [[nodiscard]] bool                    empty() const { return _queue.empty(); }
    [[nodiscard]] bool                    full() const { return _queue.full(); }
    [[nodiscard]] size_t                  size() const { return _queue.size(); }
    [[nodiscard]] static constexpr size_t capacity() { return Queue::capacity(); }

private:
    Queue& _queue;
    k_sem  _slots{};
    k_sem  _items{};
};

} // namespace spn::queue
