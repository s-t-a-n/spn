#include "spine/core/utils/string.hpp"

// #include <charconv>
#include <string>
#include <string_view>

namespace spn::core::utils {

/// Returns a string_view with the beginning of the string `strv` stripped of any char in `delimiters`
std::string_view lstrip(const std::string_view& strv, const std::string_view& delimiters) {
    auto it = strv.begin();
    while (delimiters.find(*it) != std::string::npos)
        ++it;
    const auto len = it - strv.begin();
    return {strv.data() + len, strv.size() - len};
}

/// Returns a string_view with the end of the string `strv` stripped of any char in `delimiters`
std::string_view rstrip(const std::string_view& strv, const std::string_view& delimiters) {
    auto it = strv.rbegin();
    while (delimiters.find(*it) != std::string::npos)
        ++it;
    const auto len = it - strv.rbegin();
    return {strv.data() + len, strv.size() - len};
}

/// Returns true if string `strv` starts with `needle
bool starts_with(const std::string_view& strv, const std::string_view& needle) {
    return strv.compare(0, needle.size(), needle) == 0;
}

/// Returns true if string `strv` ends with `needle
bool ends_with(const std::string_view& strv, const std::string_view& needle) {
    return strv.compare(strv.size() - needle.size(), strv.size(), needle) == 0;
}

/// Find the first occurence of any char in `delimiters` in the string `strv` or return std::string::npos when nothing
/// was found
std::size_t find_first_of(const std::string_view& strv, const std::string_view& delimiters) {
    for (std::size_t i = 0; i < strv.size(); ++i) {
        if (delimiters.find(strv[i]) != std::string::npos) {
            return i;
        }
    }
    return std::string::npos;
}

/// Returns a float read from a string_view, or 0 when no float could be found
// float to_float(const std::string_view& sv) {
//     float result;
//
//     auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), result);
//     if (ec == std::errc()) {
//         return result;
//     }
//     return 0.0f;
// }

float to_float(const std::string_view& sv) {
    char* end;

    float result = std::strtof(sv.data(), &end);
    if (end == sv.data() || *end != '\0') return 0.0f;

    return result;
}

uint32_t to_uint32(const std::string_view& sv, int base) {
    char* end;

    uint32_t result = std::strtoul(sv.data(), &end, base);
    if (end == sv.data() || *end != '\0') return 0;

    return result;
}

} // namespace spn::core::utils
