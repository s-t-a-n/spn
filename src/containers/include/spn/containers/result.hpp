#pragma once

#include "spn/core/type_traits.hpp"
#include "spn/debugging/assert.hpp"

#include <etl/memory.h>
#include <etl/utility.h>

#include <cstdint>

namespace spn {

// Only slightly inspired on Rust's Result structure.
// Kudos to Ryan Lucas @ github.com/rlucas585 for the inspiration

/// Container for function-driven processing with safety by default
template<typename T, typename E = T, typename I = T>
class Result {
public:
    static_assert(!etl::is_reference_v<T> && !etl::is_reference_v<E> && !etl::is_reference_v<I>);

    struct ok_t {};
    static constexpr ok_t ok{};
    struct err_t {};
    static constexpr err_t err{};
    struct mid_t {};
    static constexpr mid_t mid{};

    /// default-construct to NO_VALUE state
    Result() : _type(Type::NO_VALUE) {}

    /// copy-construct preserving state
    Result(const Result& other) : _type(other._type) { copy_from(other); }
    /// move-construct taking ownership
    Result(Result&& other) noexcept : _type(other._type) { move_from(etl::move(other)); }

    /// copy assignment preserving state
    Result& operator=(const Result& other) {
        if (this != &other) {
            destroy_current();
            _type = other._type;
            copy_from(other);
        }
        return *this;
    }

    /// move assignment taking ownership
    /// note: leaves other in NO_VALUE state
    Result& operator=(Result&& other) noexcept {
        if (this != &other) {
            destroy_current();
            _type = other._type;
            move_from(etl::move(other));
            other._type = Type::NO_VALUE;
        }
        return *this;
    }

    /// destroy contained value if any
    ~Result() { destroy_current(); }

    /// construct with success value
    Result(const T& v) : _type(Type::OK) { etl::construct_at(&_storage.success, v); }
    /// construct with success value
    Result(T&& v) : _type(Type::OK) { etl::construct_at(&_storage.success, etl::move(v)); }

    /// construct with tagged success value
    Result(ok_t, const T& v) : Result(v) {}
    /// construct with tagged success value
    Result(ok_t, T&& v) : Result(etl::move(v)) {}

    /// Create result in intermediary state
    static Result intermediary(const I& v) {
        Result r;
        etl::construct_at(&r._storage.intermediary, v);
        r._type = Type::INTERMEDIARY;
        return r;
    }
    /// Create result in intermediary state
    static Result intermediary(I&& v) {
        Result r;
        etl::construct_at(&r._storage.intermediary, etl::move(v));
        r._type = Type::INTERMEDIARY;
        return r;
    }

    /// Create failed result with error value
    static Result failed(const E& v) {
        Result r;
        etl::construct_at(&r._storage.error, v);
        r._type = Type::FAILED;
        return r;
    }
    /// Create failed result with error value
    static Result failed(E&& v) {
        Result r;
        etl::construct_at(&r._storage.error, etl::move(v));
        r._type = Type::FAILED;
        return r;
    }

    /// construct with error value when E != T
    template<typename U = E, typename = etl::enable_if_t<etl::is_convertible_v<U, E> && !etl::is_same_v<E, T>>>
    Result(const U& error) : _type(Type::FAILED) {
        etl::construct_at(&_storage.error, error);
    }

    /// construct with error value when E != T
    template<typename U = E, typename = etl::enable_if_t<etl::is_convertible_v<U, E> && !etl::is_same_v<E, T>>>
    Result(U&& error) : _type(Type::FAILED) {
        etl::construct_at(&_storage.error, etl::forward<U>(error));
    }

    /// construct with tagged error value
    template<typename U = E, typename = etl::enable_if_t<etl::is_convertible_v<U, E>>>
    Result(err_t, U&& e) : _type(Type::FAILED) {
        etl::construct_at(&_storage.error, etl::forward<U>(e));
    }
    /// construct with tagged intermediary value
    template<typename U = I, typename = etl::enable_if_t<etl::is_convertible_v<U, I>>>
    Result(mid_t, I&& i) : _type(Type::INTERMEDIARY) {
        etl::construct_at(&_storage.intermediary, etl::forward<U>(i));
    }

    /// Check if result contains a success value
    bool is_success() const { return _type == Type::OK; }
    /// Check if result is in intermediary state
    bool is_intermediary() const { return _type == Type::INTERMEDIARY; }
    /// Check if result contains an error
    bool is_failed() const { return _type == Type::FAILED; }

    /// convert to true if success state
    operator bool() const { return _type == Type::OK; }

    /// Get error value
    /// note: asserts if not in failed state
    const E& error_value() const {
        spn_assert(is_failed());
        return _storage.error;
    }

    /// Get intermediary value
    /// note: asserts if not in intermediary state
    const I& intermediary_value() const {
        spn_assert(is_intermediary());
        return _storage.intermediary;
    }

    /// Get success value
    /// note: asserts if not in success state
    const T& value() const {
        spn_assert(is_success());
        return _storage.success;
    }

    /// Move error value out
    /// note: asserts if not in failed state
    E unwrap_error_value() {
        spn_assert(is_failed());
        E result = etl::move(_storage.error);
        etl::destroy_at(&_storage.error);
        _type = Type::NO_VALUE;
        return result;
    }

    /// Move intermediary value out
    /// note: asserts if not in intermediary state
    I unwrap_intermediary_value() {
        spn_assert(is_intermediary());
        I result = etl::move(_storage.intermediary);
        etl::destroy_at(&_storage.intermediary);
        _type = Type::NO_VALUE;
        return result;
    }

    /// Move success value out
    /// note: asserts if in failed state
    T unwrap() {
        spn_assert(is_success());
        T result = etl::move(_storage.success);
        etl::destroy_at(&_storage.success);
        _type = Type::NO_VALUE;
        return result;
    }

    /// access success value members
    /// note: asserts if not in success state
    const T* operator->() const { return &value(); }
    /// dereference to success value
    /// note: asserts if not in success state
    const T& operator*() const { return value(); }

    /// Chain processors together, falls through on failure or success
    /// note: only processes intermediary state
    template<typename F>
    [[nodiscard]] Result chain(F&& func) {
        using ExpectedResult = Result<etl::decay_t<T>, etl::decay_t<E>, etl::decay_t<I>>;
        static_assert(
            std::is_invocable_r_v<ExpectedResult, F, I&>,
            "Function passed to chain must return a Result<T, E, I> and take I& as a parameter."
        );
        if (is_failed() || is_success()) {
            return *this;
        }
        return func(intermediary_value_mut());
    }

    /// Transform success value
    template<typename F>
    [[nodiscard]] auto map(F&& func) const -> Result<invoke_result_t<F, const T&>, E, I> {
        using NewResultType = Result<invoke_result_t<F, const T&>, E, I>;
        if (is_success()) return NewResultType(func(value()));
        if (is_failed()) return NewResultType::failed(error_value());
        if (is_intermediary()) return NewResultType::intermediary(intermediary_value());
        return NewResultType(); // NO_VALUE case
    }

    /// Transform error value
    template<typename F>
    [[nodiscard]] auto map_error(F&& func) const -> Result<T, invoke_result_t<F, const E&>, I> {
        using NewResultType = Result<T, invoke_result_t<F, const E&>, I>;
        if (is_failed()) return NewResultType::failed(func(error_value()));
        if (is_success()) return NewResultType(value());
        if (is_intermediary()) return NewResultType::intermediary(intermediary_value());
        return NewResultType(); // NO_VALUE case
    }

    /// Transform intermediary value
    template<typename F>
    [[nodiscard]] auto map_intermediary(F&& func) const -> Result<T, E, invoke_result_t<F, const I&>> {
        using NewResultType = Result<T, E, invoke_result_t<F, const I&>>;
        if (is_intermediary()) return NewResultType::intermediary(func(intermediary_value()));
        if (is_success()) return NewResultType(value());
        if (is_failed()) return NewResultType::failed(error_value());
        return NewResultType(); // NO_VALUE case
    }

    /// Chain operation on success
    template<typename F>
    [[nodiscard]] auto and_then(F&& func) const -> invoke_result_t<F, const T&> {
        using NewResultType = invoke_result_t<F, const T&>;
        if (is_success()) return func(value()); // Chain the operation
        if (is_failed()) return NewResultType::failed(error_value());
        if (is_intermediary()) return NewResultType::intermediary(intermediary_value());
        return NewResultType(); // NO_VALUE case
    }

    /// Provide fallback on error
    template<typename F>
    [[nodiscard]] auto or_else(F&& func) const -> invoke_result_t<F, const E&> {
        using NewResultType = invoke_result_t<F, const E&>;
        if (is_failed()) return func(error_value());
        if (is_success()) return NewResultType(value());
        if (is_intermediary()) return NewResultType::intermediary(intermediary_value());
        return NewResultType(); // NO_VALUE case
    }

    /// Pattern match on all states
    template<typename SuccessFn, typename ErrorFn, typename IntermediaryFn>
    [[nodiscard]] auto match(SuccessFn&& success_fn, ErrorFn&& error_fn, IntermediaryFn&& intermediary_fn) const {
        if (is_success()) return success_fn(value());
        if (is_intermediary()) return intermediary_fn(intermediary_value());
        return error_fn(error_value());
    }

    /// Get value or default
    template<class U>
    [[nodiscard]] T value_or(U&& default_value) const {
        if (is_success()) return value();
        return static_cast<T>(etl::forward<U>(default_value));
    }

    /// Get error or default
    template<class U>
    [[nodiscard]] E error_or(U&& default_error) const {
        if (is_failed()) return error_value();
        return static_cast<E>(etl::forward<U>(default_error));
    }

    /// Get value or compute fallback
    template<typename F>
    [[nodiscard]] T unwrap_or_else(F&& fallback_func) const {
        if (is_success()) return value();
        return fallback_func();
    }

protected:
    I& intermediary_value_mut() {
        spn_assert(is_intermediary());
        return _storage.intermediary;
    }

private:
    enum class Type : uint8_t { NO_VALUE, OK, INTERMEDIARY, FAILED };

    union Storage {
        char dummy; // for NO_VALUE state
        T    success;
        E    error;
        I    intermediary;

        Storage() : dummy{} {}
        ~Storage() {} // Manual lifetime management
    };

    void copy_from(const Result& other) {
        switch (other._type) {
        case Type::OK: etl::construct_at(&_storage.success, other._storage.success); break;
        case Type::FAILED: etl::construct_at(&_storage.error, other._storage.error); break;
        case Type::INTERMEDIARY: etl::construct_at(&_storage.intermediary, other._storage.intermediary); break;
        case Type::NO_VALUE: break;
        }
    }

    void move_from(Result&& other) {
        switch (other._type) {
        case Type::OK: etl::construct_at(&_storage.success, etl::move(other._storage.success)); break;
        case Type::FAILED: etl::construct_at(&_storage.error, etl::move(other._storage.error)); break;
        case Type::INTERMEDIARY:
            etl::construct_at(&_storage.intermediary, etl::move(other._storage.intermediary));
            break;
        case Type::NO_VALUE: break;
        }
    }

    void destroy_current() {
        switch (_type) {
        case Type::OK: etl::destroy_at(&_storage.success); break;
        case Type::FAILED: etl::destroy_at(&_storage.error); break;
        case Type::INTERMEDIARY: etl::destroy_at(&_storage.intermediary); break;
        case Type::NO_VALUE: break;
        }
    }

    Type    _type;
    Storage _storage;
};

} // namespace spn