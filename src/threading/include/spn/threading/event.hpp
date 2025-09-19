#pragma once

#include <zephyr/kernel.h>

#include <type_traits>

namespace spn {

/// Typed event wrapper for Zephyr k_event
template<typename Enum>
class Event {
public:
    using flag_t = Enum;
    using mask_t = std::underlying_type_t<Enum>;

    static_assert(std::is_enum_v<Enum>, "Event requires an enum or enum class type");
    static_assert(sizeof(mask_t) == sizeof(uint32_t), "Event flag underlying type must be 32-bit");

public:
    Event() { k_event_init(&_ev); }
    ~Event()                       = default;
    Event(const Event&)            = delete;
    Event& operator=(const Event&) = delete;
    Event(Event&&)                 = delete;
    Event& operator=(Event&&)      = delete;

    /// Convert flag to mask
    static constexpr mask_t mask(Enum f) { return static_cast<mask_t>(f); }

    /// Combine flags into mask
    template<typename... Fs>
    static constexpr mask_t mask(Fs... fs) {
        return (mask(fs) | ...);
    }

    /// Post bits preserving others
    void post(Enum f) { k_event_post(&_ev, mask(f)); }
    void post_mask(mask_t m) { k_event_post(&_ev, m); }

    /// Clear bits
    void clear(Enum f) { k_event_clear(&_ev, mask(f)); }
    void clear_mask(mask_t m) { k_event_clear(&_ev, m); }

    /// Replace bits atomically
    /// note: clears group_mask bits then sets set_mask bits
    void replace(mask_t group_mask, mask_t set_mask) { k_event_set_masked(&_ev, set_mask, group_mask); }
    void replace(Enum group_mask, Enum set_mask) { replace(mask(group_mask), mask(set_mask)); }

    /// Test if any bits are set
    bool test_any(Enum f) const { return k_event_test(const_cast<k_event*>(&_ev), mask(f)) != 0; }
    bool test_any_mask(mask_t m) const { return k_event_test(const_cast<k_event*>(&_ev), m) != 0; }

    /// Wait until any bits are set
    /// note: returns false on timeout, auto_reset clears triggering bits
    bool wait_any(mask_t m, k_timeout_t timeout = K_FOREVER, bool auto_reset = false) {
        return k_event_wait(&_ev, m, auto_reset, timeout) != 0;
    }
    bool wait_any(Enum f, k_timeout_t timeout = K_FOREVER, bool auto_reset = false) {
        return wait_any(mask(f), timeout, auto_reset);
    }

    /// Wait until all bits are set
    /// note: returns false on timeout, auto_reset clears all mask bits on success
    bool wait_all(mask_t m, k_timeout_t timeout = K_FOREVER, bool auto_reset = false) {
        // Fast path: already satisfied
        if ((k_event_test(&_ev, m) & m) == m) {
            if (auto_reset) k_event_clear(&_ev, m);
            return true;
        }

        // Loop waiting for any bit to change until all required bits are present or timeout
        const int64_t t0 = k_uptime_get();
        while (true) {
            // Compute remaining timeout
            k_timeout_t rem = timeout;
            if (K_TIMEOUT_EQ(timeout, K_FOREVER)) {
                rem = K_FOREVER;
            } else {
                int64_t elapsed = k_uptime_get() - t0;
                auto    left    = k_ticks_to_ms_floor64(timeout.ticks) - elapsed;
                if (left <= 0) return false;
                rem = K_MSEC(static_cast<uint32_t>(left));
            }

            if (k_event_wait(&_ev, m, false, rem) == 0) return false; // timed out
            if ((k_event_test(&_ev, m) & m) == m) {
                if (auto_reset) k_event_clear(&_ev, m);
                return true;
            }
        }
    }
    bool wait_all(Enum f, k_timeout_t timeout = K_FOREVER, bool auto_reset = false) {
        return wait_all(mask(f), timeout, auto_reset);
    }

    /// Get native handle for advanced use
    k_event*       native_handle() { return &_ev; }
    const k_event* native_handle() const { return &_ev; }

private:
    k_event _ev{};
};

} // namespace spn
