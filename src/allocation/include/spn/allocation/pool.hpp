#pragma once

#include "spn/allocation/shared_ptr.hpp"
#include "spn/allocation/types.hpp"
#include "spn/allocation/unique_ptr.hpp"
#include "spn/threading/mutex.hpp"
#include "spn/threading/semaphore.hpp"

#include <etl/array.h>
#include <etl/bitset.h>
#include <etl/delegate.h>
#include <etl/memory.h>
#include <etl/numeric.h>
#include <etl/type_traits.h>
#include <etl/vector.h>
#include <zephyr/kernel.h>

#include <cerrno>

namespace spn {

/// Fixed-size reusable indexed object pool allocator. Objects are default-constructed when pool is created and persist
/// until pool destruction. Release (either here directly, or through smartpointers) therefore do not destroy objects.
/// If RAII-lifetime is required, use a Slab instead. (ISR unsafe)
template<typename T, size_t N>
class Pool {
public:
    using hook_f = etl::delegate<void(T&, size_t)>;

    static_assert(
        !etl::is_same_v<T, detail::SharedControlBlock>,
        "Pool is not meant to store SharedControlBlock. Use Slab for control blocks."
    );

    Pool() {
        _free.resize(N);
        etl::iota(_free.rbegin(), _free.rend(), size_t{0});
    }

    /// Attach hook called when object is acquired. Hook executes after object is acquired from pool
    void attach_on_acquire(hook_f f) { _on_acquire = f; }
    /// Attach hook called when object is released. Hook executes before object is returned to pool
    void attach_on_release(hook_f f) { _on_release = f; }

    /// Acquire object from pool. Sets *out to nullptr on failure. Returns 0 on success, -ETIMEDOUT when timeout
    /// expires, -EINVAL for invalid parameters
    int acquire(void** out, k_timeout_t timeout = K_NO_WAIT) {
        if (out == nullptr) return -EINVAL;
        if (_available.take(timeout) != 0) {
            *out = nullptr;
            return -ETIMEDOUT;
        }
        size_t id{};
        {
            auto lg = _lock.lockguard();
            id      = _free.back();
            _free.pop_back();
            _in_use.set(id);
            *out = &_table[id];
        }
        if (_on_acquire.is_valid()) _on_acquire(*static_cast<T*>(*out), id);
        return 0;
    }

    /// Release object back to pool. Returns 0 on success, -EINVAL if ptr is nullptr or not owned by this pool
    int release(void* ptr) {
        if (ptr == nullptr) return -EINVAL;
        auto slot = slot_of(ptr);
        if (!slot.has_value()) return -EINVAL;
        return release(slot.value());
    }

    /// Release object by index. Returns 0 on success, -EINVAL if id is out of range or object not in use
    int release(size_t id) {
        if (id >= N) return -EINVAL;
        if (_on_release.is_valid()) _on_release(_table[id], id);
        {
            auto lg = _lock.lockguard();
            if (!_in_use.test(id)) return -EINVAL;
            _in_use.reset(id);
            _free.push_back(id);
        }
        _available.give();
        return 0;
    }

    /// Returns true if ptr is owned by this pool
    bool owns(const void* ptr) const { return slot_of(ptr).has_value(); }

    /// Returns slot index of pointer, or nullopt if not in pool
    etl::optional<size_t> slot_of(const void* p) const {
        const auto base = reinterpret_cast<const unsigned char*>(_table.data());
        const auto ptr  = reinterpret_cast<const unsigned char*>(p);
        const auto end  = base + (N * sizeof(T));
        if (ptr < base || ptr >= end) return {};
        const auto offset = static_cast<size_t>(ptr - base);
        if ((offset % sizeof(T)) != 0) return {};
        return offset / sizeof(T);
    }

    /// Returns object by index, or nullptr if not allocated or out of range
    T* try_at(size_t id) {
        if (id >= N) return nullptr;
        (void)_lock.lock(K_FOREVER);
        auto out = _in_use.test(id) ? &_table[id] : nullptr;
        _lock.unlock();
        return out;
    }

    /// Initialize all pool objects
    void initialize_each(const hook_f& f) {
        auto lg = _lock.lockguard();
        for (size_t i = 0; i < N; ++i)
            f.call_if(_table[i], i);
    }

    /// Iterate over allocated objects
    template<class F>
    void for_each_allocated(F&& f) {
        auto lg = _lock.lockguard();
        for (size_t i = 0; i < N; ++i)
            if (_in_use.test(i)) f(_table[i], i);
    }

    /// Returns number of allocated objects
    size_t used() {
        auto       lg = _lock.lockguard();
        const auto c  = _in_use.count();
        return c;
    }
    /// Returns total pool capacity
    static size_t capacity() { return N; }

    /// Create allocator for pool type
    template<typename U>
    Allocator<U> allocator() noexcept {
        static_assert(etl::is_same_v<U, T>, "Pool can only allocate its own type T");
        return Allocator<U>{this, [](void* ctx, void** out, k_timeout_t timeout) noexcept -> int {
                                auto* p = static_cast<Pool<T, N>*>(ctx);
                                if (auto rc = p->acquire(out, timeout); rc != 0) return rc;
                                if constexpr (!etl::is_trivially_destructible_v<T>)
                                    etl::destroy_at(static_cast<T*>(*out));
                                return 0;
                            }};
    }

    /// Create deleter for pool type. Deleter returns object to pool without calling destructor (by design)
    template<typename U>
    Deleter<U> deleter() noexcept {
        static_assert(etl::is_same_v<U, T>, "Pool can only delete its own type T");
        return Deleter<U>{this, [](void* ctx, void* ptr) noexcept {
                              if (ptr == nullptr) return;
                              auto* p = static_cast<Pool<T, N>*>(ctx);
                              (void)p->release(static_cast<T*>(ptr));
                          }};
    }

    /// Create shared_ptr acquiring pre-constructed pool object. By design, object is not destroyed on release. Use
    /// on_release hook to reset state
    template<typename CtrlStorage>
    shared_ptr<T> make_shared(CtrlStorage& ctrl_storage, k_timeout_t timeout) {
        void* obj_raw = nullptr;
        if (acquire(&obj_raw, timeout) != 0) return {};

        auto obj_del_typed = deleter<T>();
        auto obj_del       = Deleter<void>(obj_del_typed.context(), obj_del_typed.function());

        void* ctrl_raw = nullptr;
        if (ctrl_storage.template allocator<detail::SharedControlBlock>().allocate(&ctrl_raw, timeout) != 0) {
            release(obj_raw);
            return {};
        }

        auto ctrl_del_typed = ctrl_storage.template deleter<detail::SharedControlBlock>();
        auto ctrl_del       = Deleter<void>(ctrl_del_typed.context(), ctrl_del_typed.function());

        auto* ctrl = etl::construct_at(static_cast<detail::SharedControlBlock*>(ctrl_raw), obj_raw, obj_del, ctrl_del);
        return shared_ptr<T>(ctrl, static_cast<T*>(obj_raw));
    }

    /// Create unique_ptr acquiring pre-constructed pool object. By design, object is not destroyed on release. Use
    /// on_release hook to reset state
    unique_ptr<T> make_unique(k_timeout_t timeout) {
        void* raw = nullptr;
        if (acquire(&raw, timeout) != 0) return {};
        return unique_ptr<T>(static_cast<T*>(raw), deleter<T>());
    }

private:
    etl::array<T, N>       _table{};
    etl::vector<size_t, N> _free{};
    etl::bitset<N>         _in_use{};
    Mutex                  _lock{};
    Semaphore<N, N>        _available;

    hook_f _on_acquire{};
    hook_f _on_release{};
};

} // namespace spn
