#pragma once

#include <etl/type_traits.h>

namespace spn::core {

template<typename Enum>
constexpr std::enable_if_t<std::is_enum_v<Enum>, bool> has_flag(Enum value, Enum flag) {
    using T = std::underlying_type_t<Enum>;
    return (static_cast<T>(value) & static_cast<T>(flag)) != 0;
}

template<typename Enum>
constexpr std::enable_if_t<std::is_enum_v<Enum>, Enum> operator|(Enum lhs, Enum rhs) {
    using T = std::underlying_type_t<Enum>;
    return static_cast<Enum>(static_cast<T>(lhs) | static_cast<T>(rhs));
}

template<typename Enum>
constexpr std::enable_if_t<std::is_enum_v<Enum>, Enum> operator&(Enum lhs, Enum rhs) {
    using T = std::underlying_type_t<Enum>;
    return static_cast<Enum>(static_cast<T>(lhs) & static_cast<T>(rhs));
}

template<typename Enum>
constexpr std::enable_if_t<std::is_enum_v<Enum>, Enum> operator^(Enum lhs, Enum rhs) {
    using T = std::underlying_type_t<Enum>;
    return static_cast<Enum>(static_cast<T>(lhs) ^ static_cast<T>(rhs));
}

template<typename Enum>
constexpr std::enable_if_t<std::is_enum_v<Enum>, Enum> operator~(Enum flag) {
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

} // namespace spn::core