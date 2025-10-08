#pragma once

#include "spn/allocation/detail/allocator.hpp"
#include "spn/allocation/detail/deleter.hpp"

#include <etl/memory.h>
#include <etl/utility.h>

namespace spn {

/// Unique_ptr with origin-erased deleter that can return objects to their source allocator
template<typename T>
using unique_ptr = etl::unique_ptr<T, detail::Deleter<T>>;

/// Allocate and construct object. Returns empty unique_ptr on allocation failure or timeout
template<typename T, typename Storage, typename... Args>
unique_ptr<T> make_unique(Storage& storage, k_timeout_t timeout, Args&&... args) {
    auto  alloc = storage.template allocator<T>();
    void* raw   = nullptr;

    if (alloc.allocate(&raw, timeout) != 0) return {};
    T* obj = etl::construct_at(static_cast<T*>(raw), etl::forward<Args>(args)...);

    return unique_ptr<T>(obj, storage.template deleter<T>());
}

} // namespace spn
