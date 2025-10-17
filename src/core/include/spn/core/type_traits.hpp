#pragma once

#include <etl/type_traits.h>
#include <etl/utility.h>

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

/// deduce return type of callable when invoked with specific arguments
template<typename F, typename... Args>
using invoke_result_t = decltype(etl::declval<F>()(etl::declval<Args>()...));

/// check if callable can be invoked with arguments and return type is convertible to R
template<typename R, typename F, typename... Args>
struct is_invocable_r {
private:
    template<typename R2, typename F2, typename... Args2>
    static auto test(int) -> decltype(etl::is_convertible_v<invoke_result_t<F2, Args2...>, R2>, etl::true_type{});

    template<typename, typename, typename...>
    static etl::false_type test(...);

public:
    static constexpr bool value = decltype(test<R, F, Args...>(0))::value;
};

/// convenience variable template for is_invocable_r
template<typename R, typename F, typename... Args>
inline constexpr bool is_invocable_r_v = is_invocable_r<R, F, Args...>::value;

} // namespace spn