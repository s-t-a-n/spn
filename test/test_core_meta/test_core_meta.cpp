#include "spine/core/meta/enum.hpp"

#include <unity.h>

namespace {

void ut_meta_enum() {
    enum class Flags : uint32_t {
        Connected = 1 << 0,
        Disconnected = 1 << 1,
        MaskConnection = Connected | Disconnected,
    };

    using namespace spn::core::meta;

    // U16 / U32
    TEST_ASSERT_EQUAL_UINT32(1, U32(Flags::Connected));
    TEST_ASSERT_EQUAL_UINT32(3, U32(Flags::MaskConnection));
    TEST_ASSERT_EQUAL_UINT16(1, U16(Flags::Connected));
    TEST_ASSERT_EQUAL_UINT16(3, U16(Flags::MaskConnection));

    // bitmask operators with proper casting
    TEST_ASSERT_EQUAL_UINT32(3, U32(Flags::Connected | Flags::Disconnected));
    TEST_ASSERT_EQUAL_UINT32(3, U32(Flags::Connected ^ Flags::Disconnected));
    TEST_ASSERT_EQUAL_UINT32(0, U32(Flags::Connected & Flags::Disconnected));
    TEST_ASSERT_EQUAL_UINT32(~1u, U32(~Flags::Connected));

    // compound assignment operators
    Flags flags = Flags::Connected;

    flags |= Flags::Disconnected;
    TEST_ASSERT_EQUAL_UINT32(3, U32(flags));

    flags ^= Flags::Connected;
    TEST_ASSERT_EQUAL_UINT32(2, U32(flags));

    flags &= Flags::MaskConnection;
    TEST_ASSERT_EQUAL_UINT32(2, U32(flags));
}

} // namespace

int run_all_tests() {
    UNITY_BEGIN();
    RUN_TEST(ut_meta_enum);
    return UNITY_END();
}

#if defined(ARDUINO)
#    include <Arduino.h>
void setup() {
    // NOTE!!! Wait for >2 secs
    // if board doesn't support software reset via Serial.DTR/RTS
    delay(2000);
    run_all_tests();
}

void loop() {}
#else
int main(int argc, char** argv) {
    run_all_tests();
    return 0;
}
#endif
