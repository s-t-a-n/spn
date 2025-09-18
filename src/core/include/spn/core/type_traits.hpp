#pragma once

#include <etl/type_traits.h>

namespace spn {

/// check if type is empty (has no non-static data members)
template<typename T>
struct is_empty : etl::bool_constant<__is_empty(T)> {};

/// convenience variable template for is_empty
template<typename T>
inline constexpr bool is_empty_v = is_empty<T>::value;

/// check if type is a function type
template<typename T>
struct is_function : etl::bool_constant<!etl::is_const_v<const T> && !etl::is_reference_v<T>> {};

/// convenience variable template for is_function
template<typename T>
inline constexpr bool is_function_v = is_function<T>::value;

} // namespace spn