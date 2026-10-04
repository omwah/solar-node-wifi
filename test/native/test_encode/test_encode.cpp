#include <cmath>
#include <encode.h>
#include <unity.h>

using namespace senxx;

void setUp() {}
void tearDown() {}

void test_unsigned_scales_and_rounds()
{
    TEST_ASSERT_EQUAL_UINT16(61, encodeUnsigned(6.09f, 10));
    TEST_ASSERT_EQUAL_UINT16(482, encodeUnsigned(482.0f, 1));
}

void test_unsigned_nan_is_unknown()
{
    TEST_ASSERT_EQUAL_UINT16(UINT_UNKNOWN, encodeUnsigned(NAN, 10));
}

void test_unsigned_clamps_without_aliasing_unknown()
{
    TEST_ASSERT_EQUAL_UINT16(0, encodeUnsigned(-5.0f, 10));
    TEST_ASSERT_EQUAL_UINT16(UINT_UNKNOWN - 1, encodeUnsigned(1e9f, 10));
}

void test_signed_scales_negative_values()
{
    TEST_ASSERT_EQUAL_INT16(-1000, encodeSigned(-5.0f, 200));
    TEST_ASSERT_EQUAL_INT16(5000, encodeSigned(25.0f, 200));
}

void test_signed_nan_and_clamp()
{
    TEST_ASSERT_EQUAL_INT16(INT_UNKNOWN, encodeSigned(NAN, 100));
    TEST_ASSERT_EQUAL_INT16(INT_UNKNOWN - 1, encodeSigned(1e9f, 100));
    TEST_ASSERT_EQUAL_INT16(-32768, encodeSigned(-1e9f, 100));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_unsigned_scales_and_rounds);
    RUN_TEST(test_unsigned_nan_is_unknown);
    RUN_TEST(test_unsigned_clamps_without_aliasing_unknown);
    RUN_TEST(test_signed_scales_negative_values);
    RUN_TEST(test_signed_nan_and_clamp);
    return UNITY_END();
}
