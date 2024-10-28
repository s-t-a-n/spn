#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace spn::core::meta {

// todo: C++20: use std::underlying and enum bitmask trait

template<typename T, typename = std::enable_if_t<std::is_enum_v<T>>>
/// Shorthand cast to int. Useful for bitmasks and indexing.
constexpr int ENUM_IDX(T idx) {
    return static_cast<int>(idx);
}

template<typename T, typename = std::enable_if_t<std::is_enum_v<T>>>
/// Shorthand cast to uint32_t. Useful for bitmasks and indexing.
constexpr uint32_t U32(T idx) {
    return static_cast<uint32_t>(idx);
}

template<typename T, typename = std::enable_if_t<std::is_enum_v<T>>>
/// Shorthand cast to uint16_t. Useful for bitmasks and indexing.
constexpr uint16_t U16(T idx) {
    return static_cast<uint16_t>(idx);
}

template<typename Enum>
constexpr typename std::enable_if_t<std::is_enum_v<Enum>, Enum> operator|(Enum lhs, Enum rhs) {
    using T = std::underlying_type_t<Enum>;
    return static_cast<Enum>(static_cast<T>(lhs) | static_cast<T>(rhs));
}
template<typename Enum>
constexpr typename std::enable_if_t<std::is_enum_v<Enum>, Enum> operator&(Enum lhs, Enum rhs) {
    using T = std::underlying_type_t<Enum>;
    return static_cast<Enum>(static_cast<T>(lhs) & static_cast<T>(rhs));
}
template<typename Enum>
constexpr typename std::enable_if_t<std::is_enum_v<Enum>, Enum> operator^(Enum lhs, Enum rhs) {
    using T = std::underlying_type_t<Enum>;
    return static_cast<Enum>(static_cast<T>(lhs) ^ static_cast<T>(rhs));
}
template<typename Enum>
constexpr typename std::enable_if_t<std::is_enum_v<Enum>, Enum> operator~(Enum flag) {
    using T = std::underlying_type_t<Enum>;
    return static_cast<Enum>(~static_cast<T>(flag));
}

template<typename Enum>
constexpr std::enable_if_t<std::is_enum_v<Enum>, Enum&> operator|=(Enum& lhs, Enum rhs) {
    lhs = lhs | rhs;
    return lhs;
}

template<typename Enum>
constexpr std::enable_if_t<std::is_enum_v<Enum>, Enum&> operator&=(Enum& lhs, Enum rhs) {
    lhs = lhs & rhs;
    return lhs;
}

template<typename Enum>
constexpr std::enable_if_t<std::is_enum_v<Enum>, Enum&> operator^=(Enum& lhs, Enum rhs) {
    lhs = lhs ^ rhs;
    return lhs;
}

} // namespace spn::core::meta