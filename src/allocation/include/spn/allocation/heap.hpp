#pragma once

#include "spn/allocation/shared_ptr.hpp"
#include "spn/allocation/types.hpp"
#include "spn/allocation/unique_ptr.hpp"

#include <etl/memory.h>
#include <etl/utility.h>
#include <zephyr/kernel.h>
#include <zephyr/version.h>

#include <cstddef>

namespace spn {

/// Heap memory allocator interface. (ISR unsafe)
class IHeap {
public:
    IHeap()                        = default;
    virtual ~IHeap()               = default;
    IHeap(const IHeap&)            = delete;
    IHeap& operator=(const IHeap&) = delete;
    IHeap(IHeap&&)                 = delete;
    IHeap& operator=(IHeap&&)      = delete;

    /// Returns capacity in bytes
    virtual size_t capacity() const = 0;

    /// Allocate memory region. Sets *out to nullptr on failure. Returns 0 on success, -ENOMEM when out of memory,
    /// -EINVAL for invalid parameters
    virtual int alloc(void** out, size_t bytes, k_timeout_t timeout = K_NO_WAIT) = 0;

    /// Allocate aligned memory region. Sets *out to nullptr on failure. Returns 0 on success, -ENOMEM when out of
    /// memory, -EINVAL for invalid parameters
    virtual int alloc_aligned(void** out, size_t alignment, size_t bytes, k_timeout_t timeout = K_NO_WAIT) = 0;

    /// Allocate zero-initialized memory region. Sets *out to nullptr on failure. Returns 0 on success, -ENOMEM when out
    /// of memory, -EINVAL for invalid parameters
    virtual int calloc(void** out, size_t num, size_t size, k_timeout_t timeout = K_NO_WAIT) = 0;

    /// Reallocate memory region to new size. Frees and sets *out to nullptr when bytes is zero. Leaves *out unchanged
    /// on -ENOMEM. Returns 0 on success, -ENOMEM when out of memory, -EINVAL for invalid parameters
    virtual int realloc(void** out, size_t bytes, k_timeout_t timeout = K_NO_WAIT) = 0;

    /// Release memory region. Returns 0 on success, -EINVAL if ptr is invalid
    virtual int release(void* ptr) = 0;

    /// Returns true if this heap owns the pointer
    virtual bool owns(const void* ptr) = 0;

    /// Allocate memory for type without initialization. Sets *out to nullptr on failure. Returns 0 on success, -EINVAL
    /// for invalid parameters, -ENOMEM when out of memory
    template<typename T>
    int alloc_for(void** out, const k_timeout_t timeout = K_NO_WAIT) {
        if (out == nullptr) return -EINVAL;
        return alloc_aligned(out, alignof(T), sizeof(T), timeout);
    }

    /// Allocate memory and construct object in place. Returns 0 on success, -EINVAL for invalid parameters, -ENOMEM
    /// when out of memory
    template<typename T, typename... Args>
    [[nodiscard]] int emplace(T** out, const k_timeout_t timeout, Args&&... args) {
        if (out == nullptr) return -EINVAL;
        void* mem = nullptr;
        auto  rc  = alloc_aligned(&mem, alignof(T), sizeof(T), timeout);
        if (rc != 0) return rc;
        *out = etl::construct_at(static_cast<T*>(mem), etl::forward<Args>(args)...);
        return 0;
    }

    /// Destruct object and release memory. Returns 0 on success, -EINVAL if obj is nullptr or not owned by this heap
    template<typename T>
    int destroy(T* obj) {
        if (obj == nullptr || !owns(obj)) return -EINVAL;
        if constexpr (!etl::is_trivially_destructible_v<T>) etl::destroy_at(obj);
        return release(const_cast<void*>(static_cast<const void*>(obj)));
    }

    /// Create allocator for type T
    template<typename T>
    Allocator<T> allocator() noexcept {
        return Allocator<T>{this, [](void* ctx, void** out, k_timeout_t timeout) noexcept -> int {
                                auto* h = static_cast<IHeap*>(ctx);
                                if (out == nullptr) return -EINVAL;
                                return h->template alloc_for<T>(out, timeout);
                            }};
    }

    /// Create deleter for type T
    template<typename T>
    Deleter<T> deleter() noexcept {
        return Deleter<T>{this, [](void* ctx, void* ptr) noexcept {
                              auto* h = static_cast<IHeap*>(ctx);
                              if (ptr == nullptr) return;
                              (void)h->destroy(static_cast<T*>(ptr));
                          }};
    }

    /// Create shared_ptr with object and control block storage
    template<typename T, typename CtrlStorage, typename... Args>
    shared_ptr<T> make_shared(CtrlStorage& ctrl_storage, k_timeout_t timeout, Args&&... args) {
        return spn::make_shared<T>(*this, ctrl_storage, timeout, etl::forward<Args>(args)...);
    }

    /// Create unique_ptr managing a heap allocation
    template<typename T, typename... Args>
    unique_ptr<T> make_unique(k_timeout_t timeout, Args&&... args) {
        return spn::make_unique<T>(*this, timeout, etl::forward<Args>(args)...);
    }
};

/// Allocator for regions of dynamic sizes. Zephyr k_heap stores metadata inside the provided buffer requiring roughly
/// 20% headroom. (ISR unsafe)
template<size_t HeapBytes>
class Heap : public IHeap {
public:
    static constexpr size_t capacity_bytes = HeapBytes;

#ifdef CONFIG_HEAP_MEM_POOL_ALIGNMENT
    static constexpr size_t heap_align = CONFIG_HEAP_MEM_POOL_ALIGNMENT;
#else
    static constexpr size_t heap_align = alignof(std::max_align_t);
#endif
    static_assert((heap_align & (heap_align - 1)) == 0, "heap alignment must be power of two");

    Heap() {
#if KERNEL_VERSION_MAJOR == 3 && KERNEL_VERSION_MINOR == 7
        // Zephyr 3.7 workaround: k_heap_init does not initialize spinlock
        // Fixed in https://github.com/zephyrproject-rtos/zephyr/pull/84942
        _heap.lock = k_spinlock{};
#endif
        k_heap_init(&_heap, _buffer, capacity_bytes);
    }
    ~Heap() override = default;

    Heap(const Heap&)            = delete;
    Heap& operator=(const Heap&) = delete;
    Heap(Heap&&)                 = delete;
    Heap& operator=(Heap&&)      = delete;

    size_t capacity() const override { return capacity_bytes; }

    /// Allocate memory region. Sets *out to nullptr on failure. Returns 0 on success, -ENOMEM when out of memory,
    /// -EINVAL for invalid parameters
    [[nodiscard]] int alloc(void** out, size_t bytes, k_timeout_t timeout = K_NO_WAIT) override {
        if (out == nullptr) return -EINVAL;
        if (bytes == 0) {
            *out = nullptr;
            return -EINVAL;
        }
        void* p = k_heap_alloc(&_heap, bytes, timeout);
        if (p == nullptr) {
            *out = nullptr;
            return -ENOMEM;
        }
        *out = p;
        return 0;
    }

    /// Allocate aligned memory region. Alignment must be power of two. Sets *out to nullptr on failure. Returns 0 on
    /// success, -ENOMEM when out of memory, -EINVAL for invalid parameters
    [[nodiscard]] int
    alloc_aligned(void** out, size_t alignment, size_t bytes, k_timeout_t timeout = K_NO_WAIT) override {
        if (out == nullptr) return -EINVAL;
        if (bytes == 0) {
            *out = nullptr;
            return -EINVAL;
        }
        if ((alignment == 0) || (alignment & (alignment - 1)) != 0) {
            *out = nullptr;
            return -EINVAL;
        }
        void* p = k_heap_aligned_alloc(&_heap, alignment, bytes, timeout);
        if (p == nullptr) {
            *out = nullptr;
            return -ENOMEM;
        }
        *out = p;
        return 0;
    }

    /// Allocate zero-initialized memory region. Sets *out to nullptr on failure. Returns 0 on success, -ENOMEM when out
    /// of memory, -EINVAL for invalid parameters, -EOVERFLOW on size overflow
    [[nodiscard]] int calloc(void** out, size_t num, size_t size, k_timeout_t timeout = K_NO_WAIT) override {
        if (out == nullptr) return -EINVAL;
        if (size != 0 && num > SIZE_MAX / size) {
            *out = nullptr;
            return -EOVERFLOW;
        }
        const size_t bytes = num * size;
        auto         rc    = alloc(out, bytes, timeout);
        if (rc == 0) memset(*out, 0, bytes);
        return rc;
    }

    /// Reallocate memory region to new size. Frees and sets *out to nullptr when bytes is zero. Leaves *out unchanged
    /// on -ENOMEM. Returns 0 on success, -ENOMEM when out of memory, -EINVAL for invalid parameters
    [[nodiscard]] int realloc(void** out, size_t bytes, k_timeout_t timeout = K_NO_WAIT) override {
        if (out == nullptr) return -EINVAL;
        if (bytes == 0) {
            k_heap_free(&_heap, *out);
            *out = nullptr;
            return 0;
        }
        if (*out == nullptr) return alloc(out, bytes, timeout);
        void* p = k_heap_realloc(&_heap, *out, bytes, timeout);
        if (p == nullptr) return -ENOMEM;
        *out = p;
        return 0;
    }

    /// Release memory region. returns 0 on success, -EINVAL if ptr is invalid
    int release(void* ptr) override {
        if (ptr == nullptr || !owns(ptr)) return -EINVAL;
        k_heap_free(&_heap, ptr);
        return 0;
    }

    /// Returns true if ptr is within heap buffer range. Passing pointers different from those acquired is UB.
    bool owns(const void* ptr) override {
        if (ptr == nullptr) return false;
        const auto base = reinterpret_cast<const unsigned char*>(_buffer);
        const auto end  = base + capacity_bytes;
        const auto p    = static_cast<const unsigned char*>(ptr);
        return p >= base && p < end;
    }

private:
    k_heap _heap{};
    alignas(heap_align) unsigned char _buffer[capacity_bytes]{};
};

} // namespace spn
