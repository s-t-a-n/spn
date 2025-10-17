#pragma once

#ifndef __has_builtin
#    define __has_builtin(x) 0
#endif

namespace spn {

// we add the laundering method not to please the criminally minded, but because importing new is criminal in itself :)

/// obtain a pointer to an object at the same address with updated lifetime tracking. required after
/// destroy_at/construct_at cycles to avoid undefined behavior.
template<typename T>
constexpr T* launder(T* ptr) noexcept {
#if __has_builtin(__builtin_launder)
    return __builtin_launder(ptr);
#else
    return ptr;
#endif
}

} // namespace spn
