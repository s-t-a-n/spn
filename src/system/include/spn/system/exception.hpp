#pragma once

#include "spn/system/shutdown.hpp"

#include <zephyr/kernel.h>

#if defined(CONFIG_CPP_EXCEPTIONS)
#    include <etl/type_traits.h>
#endif

namespace spn::system {

/// base exception class compatible with old spn exception system
struct exception {
    const char* _what;
    const char* _type;

    exception(const char* what, const char* type = "exception") : _what(what), _type(type) {}

    const char* what() const { return _what; }
    const char* error_type() const { return _type; }
};

/// exception for problems that can only be discovered during runtime
struct runtime_exception : exception {
    runtime_exception(const char* what) : exception(what, "runtime exception") {}
};

/// exception for problems that should have been discovered at compile time
struct logic_exception : exception {
    logic_exception(const char* what) : exception(what, "logic exception") {}
};

/// exception that will be thrown by assertions
struct assertion_exception : exception {
    assertion_exception(const char* what) : exception(what, "assertion exception") {}
};

/// exception handler interface for compatibility with old spn system
struct ExceptionHandler {
    virtual ~ExceptionHandler() = default;

    ExceptionHandler()                                       = default;
    ExceptionHandler(const ExceptionHandler&)                = delete;
    ExceptionHandler& operator=(const ExceptionHandler&)     = delete;
    ExceptionHandler(ExceptionHandler&&) noexcept            = delete;
    ExceptionHandler& operator=(ExceptionHandler&&) noexcept = delete;

    virtual void handle_exception(const exception& ex) = 0;
};

/// set global exception handler
/// note: returns previous handler for restoration
ExceptionHandler* set_exception_handler(ExceptionHandler* handler);

/// get current exception handler
ExceptionHandler* exception_handler();

/// register system as assert handler
/// note: requires CONFIG_SPN_ASSERT_HANDLER to be enabled
void enable_assert_exceptions();

} // namespace spn::system

namespace spn {

#if defined(CONFIG_CPP_EXCEPTIONS)
/// throw C++ exception when exceptions are enabled
/// note: [[noreturn]] function, preserves derived exception type
template<typename ExceptionType>
[[noreturn]] inline void throw_exception(const ExceptionType& ex) {
    static_assert(
        etl::is_base_of_v<system::exception, ExceptionType>,
        "ExceptionType must derive from system::exception"
    );
    throw ex;
}
#else
/// trigger cooperative shutdown when C++ exceptions are disabled
/// note: [[noreturn]] function, calls handler then initiates shutdown
/// note: in single-threaded context, halts immediately instead of parking
[[noreturn]] inline void throw_exception(const system::exception& ex) {
    if (auto handler = system::exception_handler(); handler != nullptr) {
        handler->handle_exception(ex);
    }

    if (!IS_ENABLED(CONFIG_MULTITHREADING)) {
        k_fatal_halt(K_ERR_KERNEL_PANIC);
    }

    system::request_shutdown(system::shutdown_reason::exception_thrown);

#    if defined(CONFIG_BOARD_NATIVE_SIM)
    system::finalize_shutdown(); // when running in simulation, do shut down
#    endif

    // park the current thread so the scheduler can run the cooperative shutdown
    for (;;) {
        k_sleep(K_FOREVER);
    }
}
#endif

using runtime_exception   = system::runtime_exception;
using logic_exception     = system::logic_exception;
using assertion_exception = system::assertion_exception;

} // namespace spn
