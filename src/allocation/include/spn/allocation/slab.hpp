#pragma once

// Slab wraps Zephyr k_mem_slab to hand out raw fixed blocks with minimal extra bookkeeping.
// - use a Pool if instances need to be reused without destruction

#include "spn/allocation/shared_ptr.hpp"
#include "spn/allocation/types.hpp"
#include "spn/allocation/unique_ptr.hpp"
#include "spn/logging/logging.hpp"

#include <etl/memory.h>
#include <etl/type_traits.h>
#include <etl/utility.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

namespace spn {

/// Fixed-size memory slab allocator. (ISR unsafe)
template<typename T, size_t N>
class Slab {
public:
    using value_type = T;

    static constexpr auto   block_count = N;
    static constexpr size_t slab_align  = (alignof(T) > sizeof(void*)) ? alignof(T) : sizeof(void*);
    static constexpr size_t chunk_size  = ROUND_UP(sizeof(T), slab_align);

    Slab() { k_mem_slab_init(&_slab, _chunk_buffer, chunk_size, block_count); }
    ~Slab() = default;

    Slab(const Slab& other)      = delete;
    Slab(Slab&& other)           = delete;
    Slab& operator=(const Slab&) = delete;
    Slab& operator=(Slab&&)      = delete;

    /// Allocate memory block. sets *out to nullptr on failure. returns non-zero on timeout or failure
    int alloc(void** out, k_timeout_t timeout = K_NO_WAIT) {
        if (out == nullptr) return -EINVAL;
        int rc = k_mem_slab_alloc(&_slab, out, timeout);
        if (rc != 0) *out = nullptr;
        return rc;
    }

    /// Allocate and construct object. sets *out to nullptr on failure
    template<typename... Args>
    int emplace(void** out, const k_timeout_t timeout, Args&&... args)
        requires(!etl::is_array_v<T>)
    {
        if (out == nullptr) return -EINVAL;
        int rc = k_mem_slab_alloc(&_slab, out, timeout);
        if (rc != 0) {
            *out = nullptr;
            return rc;
        }
        *out = etl::construct_at(static_cast<T*>(*out), etl::forward<Args>(args)...);
        return 0;
    }

    /// Release memory block
    int release(void* ptr) {
        if (ptr == nullptr || !owns(ptr)) return -EINVAL;
        k_mem_slab_free(&_slab, ptr);
        return 0;
    }

    /// Destroy object and release block
    int destroy(T* obj) {
        if (obj == nullptr) return -EINVAL;
        if constexpr (!etl::is_trivially_destructible_v<T>) etl::destroy_at(obj);
        return release(obj);
    }

    /// Returns true if ptr is owned by this slab and aligned to chunk boundary
    bool owns(const void* ptr) const {
        if (ptr == nullptr) return false;
        const auto base = reinterpret_cast<const unsigned char*>(_chunk_buffer);
        const auto end  = base + (block_count * chunk_size);
        const auto p    = static_cast<const unsigned char*>(ptr);
        if (p < base || p >= end) return false;
        const auto offset = static_cast<size_t>(p - base);
        return (offset % chunk_size) == 0;
    }

    /// Get number of free chunks
    size_t free() { return k_mem_slab_num_free_get(&_slab); }

    /// Get number of allocated chunks
    size_t allocated() { return k_mem_slab_num_used_get(&_slab); }

    /// Get total number of chunks
    static size_t capacity() { return block_count; }

    /// create allocator for slab type
    template<typename U>
    Allocator<U> allocator() noexcept {
        static_assert(etl::is_same_v<U, T>, "Slab can only allocate its own type T");
        return Allocator<U>{this, [](void* ctx, void** out, k_timeout_t timeout) noexcept -> int {
                                auto* s = static_cast<Slab<T, N>*>(ctx);
                                if (out == nullptr) return -EINVAL;
                                return s->alloc(out, timeout);
                            }};
    }

    /// create deleter for slab type
    template<typename U>
    Deleter<U> deleter() noexcept {
        static_assert(etl::is_same_v<U, T>, "Slab can only delete its own type T");
        return Deleter<U>{this, [](void* ctx, void* ptr) noexcept {
                              auto* s = static_cast<Slab<T, N>*>(ctx);
                              if (ptr == nullptr) return;
                              (void)s->destroy(static_cast<T*>(ptr));
                          }};
    }

    /// create shared_ptr with control block storage
    template<typename CtrlStorage, typename... Args>
    shared_ptr<T> make_shared(CtrlStorage& ctrl_storage, k_timeout_t timeout, Args&&... args) {
        return spn::make_shared<T>(*this, ctrl_storage, timeout, etl::forward<Args>(args)...);
    }

    /// create unique_ptr managing a slab allocation
    template<typename... Args>
    unique_ptr<T> make_unique(k_timeout_t timeout, Args&&... args) {
        return spn::make_unique<T>(*this, timeout, etl::forward<Args>(args)...);
    }

private:
    k_mem_slab _slab;
    alignas(slab_align) unsigned char _chunk_buffer[block_count * chunk_size] = {};
};

} // namespace spn
