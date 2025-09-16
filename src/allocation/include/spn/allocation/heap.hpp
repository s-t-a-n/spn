#pragma once

#include <etl/memory.h>
#include <zephyr/kernel.h>

namespace spn {

template<typename T>
struct HeapDeleter;
template<typename T>
using unique_ptr = etl::unique_ptr<T, HeapDeleter<T>>;

/// Heap memory allocator interface
class IHeap {
public:
    virtual ~IHeap() = default;

    /// query capacity in bytes
    virtual size_t capacity() const = 0;

    /// allocate a region of size bytes
    virtual int alloc(void** out, size_t bytes, k_timeout_t timeout = K_NO_WAIT) = 0;

    /// allocate a region with alignment
    virtual int alloc_aligned(void** out, size_t alignment, size_t bytes, k_timeout_t timeout = K_NO_WAIT) = 0;

    /// allocate num * size bytes, zero-initialized
    virtual int calloc(void** out, size_t num, size_t size, k_timeout_t timeout = K_NO_WAIT) = 0;

    /// reallocate a region to size bytes
    virtual int realloc(void** out, size_t bytes, k_timeout_t timeout = K_NO_WAIT) = 0;

    /// release a region
    virtual void release(void* ptr) = 0;


    /// allocate for an object and construct in place
    template<typename T, typename... Args>
    [[nodiscard]] int emplace(T** out, Args&&... args) {
        if (out == nullptr) return -EINVAL;
        void* mem = nullptr;
        int   rc  = alloc_aligned(&mem, alignof(T), sizeof(T));
        if (rc != 0) return rc;
        *out = static_cast<T*>(mem);
        new (*out) T(static_cast<Args&&>(args)...);
        return 0;
    }

    /// destruct object and release
    template<typename T>
    void destroy(T* obj) {
        if (obj == nullptr) return;
        if constexpr (!std::is_trivially_destructible_v<T>) {
            std::destroy_at(obj);
        }
        release(static_cast<void*>(obj));
    }

    /// allocate and construct object in heap-managed unique_ptr
    template<typename T, typename... Args>
    [[nodiscard]] unique_ptr<T> make_unique(Args&&... args) {
        T* p = nullptr;
        if (emplace<T>(&p, static_cast<Args&&>(args)...) != 0) return unique_ptr<T>(nullptr, HeapDeleter<T>{this});
        return unique_ptr<T>(p, HeapDeleter<T>{this});
    }

};

/// Allocator for regions of dynamic sizes
template<size_t HeapBytes>
class Heap : public IHeap {
public:
    static constexpr size_t capacity_bytes = HeapBytes;

    Heap() { k_heap_init(&_heap, _buffer, capacity_bytes); }

    Heap(const Heap&)            = delete;
    Heap& operator=(const Heap&) = delete;
    Heap(Heap&&)                 = delete;
    Heap& operator=(Heap&&)      = delete;

    size_t capacity() const override { return capacity_bytes; }

    /// Allocate memory region
    /// note: returns -ENOMEM on failure, -EINVAL for invalid parameters
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

    /// Allocate aligned memory region
    /// note: alignment must be power of two, returns -ENOMEM on failure
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

    /// Allocate zero-initialized memory region
    /// note: returns -EOVERFLOW on size overflow
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

    /// Reallocate memory region to new size
    /// note: returns -ENOMEM on failure, frees on zero size
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

    /// Release memory region
    void release(void* ptr) override {
        if (ptr == nullptr) return;
        k_heap_free(&_heap, ptr);
    }

private:
    k_heap _heap{};
    alignas(8) unsigned char _buffer[capacity_bytes]{};
};

} // namespace spn
