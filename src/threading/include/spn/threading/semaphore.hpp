#pragma once

#include <zephyr/kernel.h>

namespace spn {

/// Counting semaphore with compile-time limits
template<int InitialCount, int Limit>
class Semaphore {
public:
    Semaphore() { k_sem_init(&_sem, InitialCount, Limit); };
    ~Semaphore() = default;

    Semaphore(const Semaphore&)            = delete;
    Semaphore& operator=(const Semaphore&) = delete;
    Semaphore(Semaphore&&)                 = delete;
    Semaphore& operator=(Semaphore&&)      = delete;

    /// Release semaphore
    void give() { k_sem_give(&_sem); }

    /// Acquire semaphore
    bool take(const k_timeout_t timeout = K_FOREVER) { return k_sem_take(&_sem, timeout) == 0; }

    /// Get current semaphore count
    auto count() { return k_sem_count_get(&_sem); }

    /// Get maximum semaphore limit
    static constexpr auto limit() { return Limit; }

private:
    k_sem _sem{};
};

} // namespace spn
