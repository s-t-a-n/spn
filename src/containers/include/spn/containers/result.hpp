#pragma once

#include "spn/core/type_traits.hpp"
#include "spn/debugging/assert.hpp"

#include <etl/memory.h>
#include <etl/monostate.h>
#include <etl/utility.h>

#include <cstdint>

namespace spn {

// PARTIALLY DEPRECATED: use of etl::result / etl::expected is encouraged
// Use this Result class for its ability to statefully store intermediary values or for it's syntax

// Only slightly inspired on Rust's Result structure.
// Kudos to Ryan Lucas @ github.com/rlucas585 for the inspiration

/// Container for function-driven processing with safety by default
template<typename T, typename E = T, typename I = etl::conditional_t<etl::is_void_v<T>, etl::monostate, T>>
class Result {
public:
    static_assert(!etl::is_reference_v<T> && !etl::is_reference_v<E> && !etl::is_reference_v<I>);
    static_assert(!etl::is_void_v<E>, "Error type cannot be void");
    static_assert(!etl::is_void_v<I>, "Intermediary type cannot be void");

private:
    // helpers to resolve callable return type for and_then based on success signature for cases where T = void
    template<typename F, bool SuccessIsVoid>
    struct SuccessInvoke;

    template<typename F>
    struct SuccessInvoke<F, true> {
        using type = invoke_result_t<F>;
    };

    template<typename F>
    struct SuccessInvoke<F, false> {
        using type = invoke_result_t<F, const T&>;
    };

    template<typename F>
    using SuccessInvokeResultT = typename SuccessInvoke<F, etl::is_void_v<T>>::type;

    // when T is void, use etl::monostate
    using StorageT = etl::conditional_t<etl::is_void_v<T>, etl::monostate, T>;

public:
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

    /// Move assignment taking ownership. Leaves other in NO_VALUE state.
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
    template<typename U = T>
        requires(!etl::is_void_v<U> && !etl::is_same_v<etl::decay_t<U>, Result>)
    Result(const U& v) : _type(Type::OK) {
        etl::construct_at(&_storage.success, v);
    }
    /// construct with success value
    template<typename U = T>
        requires(!etl::is_void_v<U> && !etl::is_same_v<etl::decay_t<U>, Result>)
    Result(U&& v) : _type(Type::OK) {
        etl::construct_at(&_storage.success, etl::forward<U>(v));
    }

    /// construct with tagged success value
    template<typename U = T>
        requires(!etl::is_void_v<U> && !etl::is_same_v<etl::decay_t<U>, Result>)
    Result(ok_t, const U& v) : Result(v) {}
    /// construct with tagged success value
    template<typename U = T>
        requires(!etl::is_void_v<U> && !etl::is_same_v<etl::decay_t<U>, Result>)
    Result(ok_t, U&& v) : Result(etl::forward<U>(v)) {}
    /// construct with tagged success value for void T
    template<typename U = T>
        requires(etl::is_void_v<U>)
    Result(ok_t) : _type(Type::OK) {
        etl::construct_at(&_storage.success, etl::monostate{});
    }

    /// create result in intermediary state
    template<typename V>
        requires(etl::is_convertible_v<V, I>)
    static Result intermediary(V&& v) {
        Result r;
        etl::construct_at(&r._storage.intermediary, etl::forward<V>(v));
        r._type = Type::INTERMEDIARY;
        return r;
    }

    /// create failed result with error value
    static Result failed(const E& v) {
        Result r;
        etl::construct_at(&r._storage.error, v);
        r._type = Type::FAILED;
        return r;
    }
    /// create failed result with error value
    static Result failed(E&& v) {
        Result r;
        etl::construct_at(&r._storage.error, etl::move(v));
        r._type = Type::FAILED;
        return r;
    }

    /// construct with error value when E != T
    template<typename U = E>
        requires(etl::is_convertible_v<U, E> && !etl::is_same_v<E, T> && !etl::is_same_v<etl::decay_t<U>, Result>)
    Result(const U& error) : _type(Type::FAILED) {
        etl::construct_at(&_storage.error, error);
    }

    /// construct with error value when E != T
    template<typename U = E>
        requires(etl::is_convertible_v<U, E> && !etl::is_same_v<E, T> && !etl::is_same_v<etl::decay_t<U>, Result>)
    Result(U&& error) : _type(Type::FAILED) {
        etl::construct_at(&_storage.error, etl::forward<U>(error));
    }

    /// construct with tagged error value
    template<typename U = E>
        requires(etl::is_convertible_v<U, E> && !etl::is_same_v<etl::decay_t<U>, Result>)
    Result(err_t, U&& e) : _type(Type::FAILED) {
        etl::construct_at(&_storage.error, etl::forward<U>(e));
    }
    /// construct with tagged intermediary value
    template<typename U>
        requires(etl::is_convertible_v<U, I>)
    Result(mid_t, U&& i) : _type(Type::INTERMEDIARY) {
        etl::construct_at(&_storage.intermediary, etl::forward<U>(i));
    }

    /// check if result contains a success value
    bool is_ok() const { return _type == Type::OK; }
    /// check if result is in intermediary state
    bool is_intermediary() const { return _type == Type::INTERMEDIARY; }
    /// check if result contains an error
    bool is_err() const { return _type == Type::FAILED; }

    /// convert to true if success state
    operator bool() const { return _type == Type::OK; }

    /// Get error value. Asserts if not in failed state.
    const E& error() const {
        spn_assert(is_err());
        return _storage.error;
    }

    /// Get intermediary value. Asserts if not in intermediary state.
    const I& intermediary_value() const {
        spn_assert(is_intermediary());
        return _storage.intermediary;
    }

    /// Get success value. Asserts if not in success state.
    template<typename U = T>
        requires(!etl::is_void_v<U>)
    const U& value() const {
        spn_assert(is_ok());
        return _storage.success;
    }
    /// Get success value for void T (no-op). Asserts if not in success state.
    template<typename U = T>
        requires(etl::is_void_v<U>)
    void value() const {
        spn_assert(is_ok());
    }

    /// Move error value out. Asserts if not in failed state.
    E unwrap_error() {
        spn_assert(is_err());
        E result = etl::move(_storage.error);
        etl::destroy_at(&_storage.error);
        _type = Type::NO_VALUE;
        return result;
    }

    /// Move intermediary value out. Asserts if not in intermediary state.
    I unwrap_intermediary_value() {
        spn_assert(is_intermediary());
        I result = etl::move(_storage.intermediary);
        etl::destroy_at(&_storage.intermediary);
        _type = Type::NO_VALUE;
        return result;
    }

    /// Move success value out. Asserts if not in success state.
    template<typename U = T>
        requires(!etl::is_void_v<U>)
    U unwrap() {
        spn_assert(is_ok());
        U result = etl::move(_storage.success);
        etl::destroy_at(&_storage.success);
        _type = Type::NO_VALUE;
        return result;
    }
    /// Move success value out for void T (no-op). Asserts if not in success state.
    template<typename U = T>
        requires(etl::is_void_v<U>)
    void unwrap() {
        spn_assert(is_ok());
        etl::destroy_at(&_storage.success);
        _type = Type::NO_VALUE;
    }

    /// Access success value members. Asserts if not in success state.
    template<typename U = T>
        requires(!etl::is_void_v<U>)
    const U* operator->() const {
        return &value();
    }
    /// Dereference to success value. Asserts if not in success state.
    template<typename U = T>
        requires(!etl::is_void_v<U>)
    const U& operator*() const {
        return value();
    }

    /// Chain processors together, falls through on failure or success. Only processes intermediary state.
    template<typename F>
    [[nodiscard]] Result chain(F&& func) {
        using ExpectedResult = Result<etl::decay_t<T>, etl::decay_t<E>, etl::decay_t<I>>;
        static_assert(
            spn::is_invocable_r_v<ExpectedResult, F, I&>,
            "Function passed to chain must return a Result<T, E, I> and take I& as a parameter."
        );
        if (is_err() || is_ok()) {
            return *this;
        }
        return func(intermediary_value_mut());
    }

    /// Transform success value. Returns Result with new success type.
    template<typename F, typename U = T>
        requires(!etl::is_void_v<U>)
    [[nodiscard]] auto map(F&& func) const -> Result<invoke_result_t<F, const U&>, E, I> {
        using NewResultType = Result<invoke_result_t<F, const U&>, E, I>;
        if (is_ok()) return NewResultType(func(value()));
        if (is_err()) return NewResultType::failed(error());
        if (is_intermediary()) return NewResultType::intermediary(intermediary_value());
        return NewResultType(); // NO_VALUE case
    }

    /// Transform error value. Returns Result with new error type.
    template<typename F>
    [[nodiscard]] auto map_error(F&& func) const -> Result<T, invoke_result_t<F, const E&>, I> {
        using NewResultType = Result<T, invoke_result_t<F, const E&>, I>;
        if (is_err()) return NewResultType::failed(func(error()));
        if (is_ok()) {
            if constexpr (etl::is_void_v<T>) {
                return NewResultType{NewResultType::ok};
            } else {
                return NewResultType(value());
            }
        }
        if (is_intermediary()) return NewResultType::intermediary(intermediary_value());
        return NewResultType(); // NO_VALUE case
    }

    /// Transform intermediary value. Returns Result with new intermediary type.
    template<typename F>
    [[nodiscard]] auto map_intermediary(F&& func) const -> Result<T, E, invoke_result_t<F, const I&>> {
        using NewResultType = Result<T, E, invoke_result_t<F, const I&>>;
        if (is_intermediary()) return NewResultType::intermediary(func(intermediary_value()));
        if (is_ok()) {
            if constexpr (etl::is_void_v<T>) {
                return NewResultType{NewResultType::ok};
            } else {
                return NewResultType(value());
            }
        }
        if (is_err()) return NewResultType::failed(error());
        return NewResultType(); // NO_VALUE case
    }

    /// Chain operation on success. Returns Result from function.
    template<typename F>
    [[nodiscard]] auto and_then(F&& func) const -> SuccessInvokeResultT<F> {
        using NewResultType = SuccessInvokeResultT<F>;
        if (is_ok()) {
            if constexpr (etl::is_void_v<T>) {
                return func();
            } else {
                return func(value());
            }
        }
        if (is_err()) return NewResultType::failed(error());
        if (is_intermediary()) return NewResultType::intermediary(intermediary_value());
        return NewResultType(); // NO_VALUE case
    }

    /// Provide fallback on error. Returns Result from function or preserves success.
    template<typename F>
    [[nodiscard]] auto or_else(F&& func) const -> invoke_result_t<F, const E&> {
        using NewResultType = invoke_result_t<F, const E&>;
        if (is_err()) return func(error());
        if (is_ok()) {
            if constexpr (etl::is_void_v<T>) {
                return NewResultType{NewResultType::ok};
            } else {
                return NewResultType(value());
            }
        }
        if (is_intermediary()) return NewResultType::intermediary(intermediary_value());
        return NewResultType(); // NO_VALUE case
    }

    /// Pattern match on all states. Returns result of called function.
    template<typename SuccessFn, typename ErrorFn, typename IntermediaryFn>
    [[nodiscard]] auto match(SuccessFn&& success_fn, ErrorFn&& error_fn, IntermediaryFn&& intermediary_fn) const {
        if (is_ok()) {
            if constexpr (etl::is_void_v<T>) {
                return success_fn();
            } else {
                return success_fn(value());
            }
        }
        if (is_intermediary()) return intermediary_fn(intermediary_value());
        return error_fn(error());
    }

    /// Get success value or return default if not in ok state.
    template<class U, typename V = T>
        requires(!etl::is_void_v<V>)
    [[nodiscard]] T value_or(U&& default_value) const {
        if (is_ok()) return value();
        return static_cast<T>(etl::forward<U>(default_value));
    }

    /// Get error value or return default if not in error state.
    template<class U>
    [[nodiscard]] E error_or(U&& default_error) const {
        if (is_err()) return error();
        return static_cast<E>(etl::forward<U>(default_error));
    }

    /// Get success value or compute fallback if not in ok state.
    template<typename F, typename V = T>
        requires(!etl::is_void_v<V>)
    [[nodiscard]] T unwrap_or_else(F&& fallback_func) const {
        if (is_ok()) return value();
        return fallback_func();
    }

protected:
    /// Get mutable reference to intermediary value. Asserts if not in intermediary state.
    I& intermediary_value_mut() {
        spn_assert(is_intermediary());
        return _storage.intermediary;
    }

private:
    enum class Type : uint8_t { NO_VALUE, OK, INTERMEDIARY, FAILED };

    union Storage {
        etl::monostate empty; // for NO_VALUE state
        StorageT       success;
        E              error;
        I              intermediary;

        Storage() : empty{} {}
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
