#include "spn/debugging/assert.hpp"
#include "spn/logging/logging.hpp"
#include "spn/system/exception.hpp"

#include <zephyr/kernel.h>

LOG_MODULE_REGISTER(with_cpp_exceptions_sample);

int main(void) {
    LOG_INF("=== SPN System Sample (with C++ exceptions) ===");

    try {
        LOG_INF("creating and throwing runtime exception");
        spn::runtime_exception ex("sample runtime error");
        spn::throw_exception(ex);
    } catch (const spn::system::exception& e) {
        LOG_INF("caught exception: %s (%s)", e.what(), e.error_type());
    }

    try {
        LOG_INF("creating and throwing logic exception");
        spn::logic_exception ex("sample logic error");
        spn::throw_exception(ex);
    } catch (const spn::system::exception& e) {
        LOG_INF("caught exception: %s (%s)", e.what(), e.error_type());
    }

    LOG_INF("enabling exception-based assertions");
    spn::system::enable_assert_exceptions();

    LOG_INF("testing successful assertion");
    spn_assert(true);

    try {
        LOG_INF("testing assertion that will throw exception");
        spn_assert(false);
    } catch (const spn::assertion_exception& e) {
        LOG_INF("caught assertion exception: %s", e.what());
    }

    LOG_INF("=== Sample Complete ===");

    return 0;
}