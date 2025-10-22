#pragma once

#include <zephyr/kernel.h>
#ifdef CONFIG_OBJ_CORE_SEM
#    include <zephyr/kernel/obj_core.h>
#endif

namespace spn {

/// Counting semaphore with compile-time limits
template<int InitialCount, int Limit>
class Semaphore {
public:
    Semaphore() { k_sem_init(&_sem, InitialCount, Limit); };
    ~Semaphore() {
#ifdef CONFIG_OBJ_CORE_SEM
        k_obj_core_unlink(&_sem.obj_core);
#endif
    }

    Semaphore(const Semaphore&)            = delete;
    Semaphore& operator=(const Semaphore&) = delete;
    Semaphore(Semaphore&&)                 = delete;
    Semaphore& operator=(Semaphore&&)      = delete;

    /// Release semaphore
    void give() { k_sem_give(&_sem); }

    /// Acquire semaphore. Returns non-zero on timeout
    int take(const k_timeout_t timeout = K_FOREVER) { return k_sem_take(&_sem, timeout); }

    /// Get current semaphore count
    size_t count() const { return k_sem_count_get(&_sem); }

    /// Get maximum semaphore limit
    static constexpr auto limit() { return Limit; }

private:
    mutable k_sem _sem{};
};

// note: no tests for this class as we are not adding any behaviour over C-components provided upstream

} // namespace spn
