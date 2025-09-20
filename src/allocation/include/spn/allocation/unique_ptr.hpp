#pragma once

#include "spn/allocation/heap.hpp"

#include <etl/memory.h>

namespace spn {

/// Heap deleter for unique_ptr
template<typename T>
struct HeapDeleter {
    IHeap* _heap{};
    void   operator()(T* p) const noexcept {
          if (!p || _heap == nullptr) return;
        _heap->destroy(p);
    }
};

template<typename T>
using unique_ptr = etl::unique_ptr<T, HeapDeleter<T>>;

/// Array specialization of heap deleter
template<typename T>
struct HeapDeleter<T[]> {
    IHeap* _heap{};
    void   operator()(T* p) const noexcept {
          if (!p || _heap == nullptr) return;
        _heap->release(p); // raw memory release for arrays
    }
};

/// Create unique pointer with heap allocation
template<typename T, typename... Args>
unique_ptr<T> make_unique_ptr(IHeap& heap, Args&&... args) {
    T* p = nullptr;
    if (heap.emplace<T>(&p, static_cast<Args&&>(args)...) != 0) return unique_ptr<T>(nullptr, HeapDeleter<T>{&heap});
    return unique_ptr<T>(p, HeapDeleter<T>{&heap});
}

} // namespace spn
