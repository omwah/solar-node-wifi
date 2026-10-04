#include <crc.h>
#include <unity.h>

void setUp() {}
void tearDown() {}

// Example from the Sensirion SEN5x/SEN6x datasheets: CRC(0xBEEF) = 0x92.
void test_crc8_datasheet_example()
{
    const uint8_t word[] = {0xBE, 0xEF};
    TEST_ASSERT_EQUAL_HEX8(0x92, senxx::crc8(word, sizeof(word)));
}

// "Unknown" sentinels the emulator sends for null fields.
void test_crc8_unknown_sentinels()
{
    const uint8_t uintUnknown[] = {0xFF, 0xFF};
    const uint8_t intUnknown[] = {0x7F, 0xFF};
    TEST_ASSERT_EQUAL_HEX8(0xAC, senxx::crc8(uintUnknown, sizeof(uintUnknown)));
    TEST_ASSERT_EQUAL_HEX8(0x8F, senxx::crc8(intUnknown, sizeof(intUnknown)));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_crc8_datasheet_example);
    RUN_TEST(test_crc8_unknown_sentinels);
    return UNITY_END();
}
