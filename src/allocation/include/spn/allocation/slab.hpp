#pragma once

#include "spn/logging/logging.hpp"

#include <etl/memory.h>
#include <etl/type_traits.h>
#include <zephyr/kernel.h>

namespace spn {

/// Fixed-size memory slab allocator
template<typename T, size_t NumBlocks>
class Slab {
public:
    static constexpr auto   block_count = NumBlocks;
    static constexpr size_t slab_align  = (alignof(T) > sizeof(void*)) ? alignof(T) : sizeof(void*);

    static_assert((sizeof(T) % slab_align) == 0, "block size must be multiple of slab_align");

    Slab() { k_mem_slab_init(&_slab, _chunk_buffer, sizeof(T), block_count); }
    ~Slab() = default;

    Slab(const Slab& other)      = delete;
    Slab(Slab&& other)           = delete;
    Slab& operator=(const Slab&) = delete;
    Slab& operator=(Slab&&)      = delete;

    /// Allocate memory block
    /// note: returns non-zero on timeout or failure
    int alloc(T** block, k_timeout_t timeout = K_NO_WAIT) {
        if (block == nullptr) return -EINVAL;
        void* tmp = nullptr;
        int   rc  = k_mem_slab_alloc(&_slab, &tmp, timeout);
        *block    = static_cast<T*>(tmp);
        return rc;
    }

    /// Allocate and construct object
    template<typename... Args>
    int emplace(T** block, Args&&... args) requires(!std::is_array_v<T>) {
        if (block == nullptr) return -EINVAL;

        void* tmp = nullptr;
        int   rc  = k_mem_slab_alloc(&_slab, &tmp, K_NO_WAIT);
        if (rc != 0) return rc;

        T* p = static_cast<T*>(tmp);
        std::construct_at(p, static_cast<Args&&>(args)...);
        *block = p;
        return 0;
    }

    /// Release memory block
    int release(T* block) {
        if (block == nullptr) return -EINVAL;
        k_mem_slab_free(&_slab, block);
        return 0;
    }

    /// Destroy object and release block
    int destroy(T* block) {
        if (block == nullptr) return -EINVAL;

        if constexpr (!std::is_trivially_destructible_v<T>) {
            etl::destroy_at(block);
        }
        return release(block);
    }

    /// Get number of free chunks
    size_t chunks_free() { return k_mem_slab_num_free_get(&_slab); }

    /// Get number of allocated chunks
    size_t chunks_allocated() { return k_mem_slab_num_used_get(&_slab); }

    /// Get total number of chunks
    static size_t chunks_total() { return block_count; }

private:
    k_mem_slab _slab                                                         = {};
    alignas(slab_align) unsigned char _chunk_buffer[block_count * sizeof(T)] = {};
};

} // namespace spn
