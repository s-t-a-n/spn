#include "spine/core/exception.hpp"

#include "spine/core/debugging.hpp"
#include "spine/core/logging.hpp"
#include "spine/platform/hal.hpp"

namespace spn::core {

namespace {
etl::unique_ptr<ExceptionHandler>& eh_instance() {
    static etl::unique_ptr<ExceptionHandler> handler = {};
    return handler;
}
} // namespace

etl::unique_ptr<ExceptionHandler> set_machine_exception_handler(etl::unique_ptr<ExceptionHandler> handler) {
    SPN_DBG("Replacing machine wide exception handler: %p", handler.get());
    eh_instance().swap(handler);
    return handler;
}

ExceptionHandler* machine_exception_handler() { return eh_instance() ? eh_instance().get() : nullptr; }

void Exception::throw_error() const {
    if (const auto eh = machine_exception_handler(); eh != nullptr) {
        SPN_DBG("Throwing exception {%s}: %s", error_type(), _error_msg);
        eh->handle_exception(*this);
    }

    SPN_WARN("%s was thrown: %s, halting without handling exception", error_type(), _error_msg);
    HAL::halt("Exception was thrown.");
}
} // namespace spn::core
