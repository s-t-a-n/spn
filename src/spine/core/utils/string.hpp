#pragma once

#include <cstdint>
#include <string_view>

namespace spn::core::utils {

/// Returns a string_view with the beginning of the string `strv` stripped of any char in `delimiters`
std::string_view lstrip(const std::string_view& strv, const std::string_view& delimiters = " \f\n\r\t\v");

/// Returns a string_view with the end of the string `strv` stripped of any char in `delimiters`
std::string_view rstrip(const std::string_view& strv, const std::string_view& delimiters = " \f\n\r\t\v");

/// Returns true if string `strv` starts with `needle
bool starts_with(const std::string_view& strv, const std::string_view& needle);

/// Returns true if string `strv` ends with `needle
bool ends_with(const std::string_view& strv, const std::string_view& needle);

/// Find the first occurence of any char in `delimiters` in the string `strv` or return std::string::npos when nothing
/// was found
std::size_t find_first_of(const std::string_view& strv, const std::string_view& delimiters);

/// Returns a float read from a string_view, or 0 when no float could be found
// float to_float(const std::string_view& sv) ;

/// Returns a float read from a string_view, or 0 when no float could be found
float to_float(const std::string_view& sv);

/// Returns a uint32 read from a string_view, or 0 when no uint32 could be found
uint32_t to_uint32(const std::string_view& sv, int base = 10);

} // namespace spn::core::utils
