#pragma once

#include <etl/tuple.h>
#include <etl/utility.h>

namespace spn {

/// apply implementation for tuple unpacking
template<typename F, typename Tuple, size_t... I>
constexpr decltype(auto) apply_impl(F&& f, Tuple&& t, etl::index_sequence<I...>) {
    return etl::forward<F>(f)(etl::get<I>(etl::forward<Tuple>(t))...);
}

/// apply tuple elements as arguments to callable
template<typename F, typename Tuple>
constexpr decltype(auto) apply(F&& f, Tuple&& t) {
    return apply_impl(
        etl::forward<F>(f),
        etl::forward<Tuple>(t),
        etl::make_index_sequence<etl::tuple_size_v<etl::remove_reference_t<Tuple>>>{}
    );
}

} // namespace spn