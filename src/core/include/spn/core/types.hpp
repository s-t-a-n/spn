#pragma once

#include <etl/type_traits.h>

#include <cstdint>

namespace spn {

template<typename T>
constexpr uint8_t U8(T value) {
    return static_cast<uint8_t>(value);
}

template<typename T>
constexpr uint16_t U16(T value) {
    return static_cast<uint16_t>(value);
}

template<typename T>
constexpr uint32_t U32(T value) {
    return static_cast<uint32_t>(value);
}

template<typename T>
constexpr int ENUM_IDX(T value) {
    static_assert(etl::is_enum_v<T>, "ENUM_IDX can only be used with enum types");
    return static_cast<int>(value);
}

} // namespace spn