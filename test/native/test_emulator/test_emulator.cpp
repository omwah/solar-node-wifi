#include <crc.h>
#include <cstring>
#include <emulator.h>
#include <unity.h>
#include <vector>

using namespace senxx;

void setUp() {}
void tearDown() {}

namespace
{

void sendCommand(Emulator &emu, uint16_t cmd, std::vector<uint16_t> args = {})
{
    std::vector<uint8_t> bytes = {static_cast<uint8_t>(cmd >> 8), static_cast<uint8_t>(cmd & 0xFF)};
    for (uint16_t w : args) {
        uint8_t word[2] = {static_cast<uint8_t>(w >> 8), static_cast<uint8_t>(w & 0xFF)};
        bytes.push_back(word[0]);
        bytes.push_back(word[1]);
        bytes.push_back(crc8(word, 2));
    }
    emu.onWrite(bytes.data(), bytes.size());
}

// Decode the staged reply the way the Meshtastic driver does, checking every CRC.
std::vector<uint16_t> readWords(const Emulator &emu)
{
    const uint8_t *r = emu.response();
    size_t len = emu.responseLength();
    TEST_ASSERT_EQUAL_UINT(0, len % 3);
    std::vector<uint16_t> words;
    for (size_t i = 0; i < len; i += 3) {
        TEST_ASSERT_EQUAL_HEX8(crc8(&r[i], 2), r[i + 2]);
        words.push_back(static_cast<uint16_t>((r[i] << 8) | r[i + 1]));
    }
    return words;
}

Measurement referenceMeasurement()
{
    Measurement m;
    m.pm1 = 0.0f;
    m.pm25 = 6.09f;
    m.pm10 = 2.17f;
    m.humidity = 47.28f;
    m.temperature = 25.25f;
    m.vocIndex = 71.0f;
    m.noxIndex = 1.0f;
    m.co2 = 482.0f;
    return m;
}

} // namespace

void test_addresses()
{
    TEST_ASSERT_EQUAL_HEX8(0x6B, Emulator(Model::SEN66).address());
    TEST_ASSERT_EQUAL_HEX8(0x69, Emulator(Model::SEN55).address());
}

void test_address_probe_acks_with_no_reply()
{
    Emulator emu(Model::SEN66);
    emu.onWrite(nullptr, 0);
    TEST_ASSERT_EQUAL_UINT(0, emu.responseLength());
}

void test_register_reads_return_zero()
{
    Emulator emu(Model::SEN66);
    for (uint8_t reg : {0x0A, 0x14, 0x0F, 0x00, 0x75}) {
        emu.onWrite(&reg, 1);
        TEST_ASSERT_EQUAL_UINT(1, emu.responseLength());
        TEST_ASSERT_EQUAL_HEX8(0x00, emu.response()[0]);
    }
}

void test_product_name_is_48_bytes_with_model_string()
{
    Emulator emu(Model::SEN66);
    sendCommand(emu, 0xD014);
    TEST_ASSERT_EQUAL_UINT(48, emu.responseLength());
    std::vector<uint16_t> words = readWords(emu);
    char name[33] = {};
    for (size_t i = 0; i < words.size(); i++) {
        name[i * 2] = static_cast<char>(words[i] >> 8);
        name[i * 2 + 1] = static_cast<char>(words[i] & 0xFF);
    }
    TEST_ASSERT_EQUAL_STRING("SEN66", name);
}

void test_firmware_version_major_at_least_2()
{
    Emulator emu(Model::SEN66);
    sendCommand(emu, 0xD100);
    std::vector<uint16_t> words = readWords(emu);
    TEST_ASSERT_EQUAL_UINT(4, words.size());
    TEST_ASSERT_GREATER_OR_EQUAL_UINT8(2, words[0] >> 8);
}

void test_data_ready_in_any_state()
{
    Emulator emu(Model::SEN66);
    sendCommand(emu, 0x0202);
    TEST_ASSERT_EQUAL_UINT16(1, readWords(emu)[0] & 0xFF);
    sendCommand(emu, 0x0021);
    sendCommand(emu, 0x0202);
    TEST_ASSERT_EQUAL_UINT16(1, readWords(emu)[0] & 0xFF);
}

void test_sen66_measured_values_layout()
{
    Emulator emu(Model::SEN66);
    emu.setMeasurement(referenceMeasurement());
    sendCommand(emu, 0x0300);
    std::vector<uint16_t> w = readWords(emu);
    TEST_ASSERT_EQUAL_UINT(9, w.size());
    TEST_ASSERT_EQUAL_UINT16(0, w[0]);
    TEST_ASSERT_EQUAL_UINT16(61, w[1]);
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, w[2]); // PM4 unknown
    TEST_ASSERT_EQUAL_UINT16(22, w[3]);
    TEST_ASSERT_EQUAL_INT16(4728, static_cast<int16_t>(w[4]));
    TEST_ASSERT_EQUAL_INT16(5050, static_cast<int16_t>(w[5]));
    TEST_ASSERT_EQUAL_INT16(710, static_cast<int16_t>(w[6]));
    TEST_ASSERT_EQUAL_INT16(10, static_cast<int16_t>(w[7]));
    TEST_ASSERT_EQUAL_UINT16(482, w[8]);
}

void test_unknown_snapshot_reports_unknown_codes()
{
    Emulator emu(Model::SEN66);
    sendCommand(emu, 0x0300);
    std::vector<uint16_t> w = readWords(emu);
    for (int i : {0, 1, 2, 3, 8}) {
        TEST_ASSERT_EQUAL_UINT16(0xFFFF, w[i]);
    }
    for (int i : {4, 5, 6, 7}) {
        TEST_ASSERT_EQUAL_UINT16(0x7FFF, w[i]);
    }
}

void test_sen66_number_concentrations()
{
    Emulator emu(Model::SEN66);
    sendCommand(emu, 0x0316);
    std::vector<uint16_t> w = readWords(emu);
    TEST_ASSERT_EQUAL_UINT(5, w.size());
    for (uint16_t v : w) {
        TEST_ASSERT_EQUAL_UINT16(0xFFFF, v);
    }
}

void test_sen66_device_status_is_ok()
{
    Emulator emu(Model::SEN66);
    sendCommand(emu, 0xD206);
    std::vector<uint16_t> w = readWords(emu);
    TEST_ASSERT_EQUAL_UINT(2, w.size());
    TEST_ASSERT_EQUAL_UINT16(0, w[0]);
    TEST_ASSERT_EQUAL_UINT16(0, w[1]);
}

void test_sen55_layouts()
{
    Emulator emu(Model::SEN55);
    emu.setMeasurement(referenceMeasurement());
    sendCommand(emu, 0x03C4);
    TEST_ASSERT_EQUAL_UINT(8, readWords(emu).size());
    sendCommand(emu, 0x0413);
    std::vector<uint16_t> w = readWords(emu);
    TEST_ASSERT_EQUAL_UINT(10, w.size());
    TEST_ASSERT_EQUAL_UINT16(61, w[1]);
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, w[9]);
}

void test_sen66_ignores_sen5x_only_commands()
{
    Emulator emu(Model::SEN66);
    sendCommand(emu, 0x03C4);
    TEST_ASSERT_EQUAL_UINT(0, emu.responseLength());
}

void test_new_command_clears_previous_reply()
{
    Emulator emu(Model::SEN66);
    sendCommand(emu, 0xD014);
    TEST_ASSERT_EQUAL_UINT(48, emu.responseLength());
    sendCommand(emu, 0x0021);
    TEST_ASSERT_EQUAL_UINT(0, emu.responseLength());
}

void test_bad_argument_crc_is_ignored()
{
    Emulator emu(Model::SEN66);
    const uint8_t bad[] = {0x61, 0x81, 0x12, 0x34, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    emu.onWrite(bad, sizeof(bad));
    sendCommand(emu, 0x6181);
    for (uint16_t v : readWords(emu)) {
        TEST_ASSERT_EQUAL_UINT16(0, v);
    }
}

void test_voc_state_round_trip()
{
    Emulator emu(Model::SEN66);
    sendCommand(emu, 0x6181, {0x0102, 0x0304, 0x0506, 0x0708});
    sendCommand(emu, 0x6181);
    std::vector<uint16_t> w = readWords(emu);
    TEST_ASSERT_EQUAL_UINT(4, w.size());
    TEST_ASSERT_EQUAL_UINT16(0x0102, w[0]);
    TEST_ASSERT_EQUAL_UINT16(0x0708, w[3]);
}

void test_state_transitions()
{
    Emulator emu(Model::SEN55);
    TEST_ASSERT_EQUAL(static_cast<int>(SensorState::Idle), static_cast<int>(emu.state()));
    sendCommand(emu, 0x0021);
    TEST_ASSERT_EQUAL(static_cast<int>(SensorState::Measuring), static_cast<int>(emu.state()));
    sendCommand(emu, 0x0037);
    TEST_ASSERT_EQUAL(static_cast<int>(SensorState::RhtGasOnly), static_cast<int>(emu.state()));
    sendCommand(emu, 0xD304);
    TEST_ASSERT_EQUAL(static_cast<int>(SensorState::Idle), static_cast<int>(emu.state()));
}

void test_set_measurement_replaces_snapshot()
{
    Emulator emu(Model::SEN66);
    Measurement m;
    m.co2 = 400.0f;
    emu.setMeasurement(m);
    m.co2 = 800.0f;
    emu.setMeasurement(m);
    sendCommand(emu, 0x0300);
    TEST_ASSERT_EQUAL_UINT16(800, readWords(emu)[8]);
}

void test_transactions_are_counted()
{
    Emulator emu(Model::SEN66);
    emu.onWrite(nullptr, 0);
    sendCommand(emu, 0x0202);
    TEST_ASSERT_EQUAL_UINT32(2, emu.transactionCount());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_addresses);
    RUN_TEST(test_address_probe_acks_with_no_reply);
    RUN_TEST(test_register_reads_return_zero);
    RUN_TEST(test_product_name_is_48_bytes_with_model_string);
    RUN_TEST(test_firmware_version_major_at_least_2);
    RUN_TEST(test_data_ready_in_any_state);
    RUN_TEST(test_sen66_measured_values_layout);
    RUN_TEST(test_unknown_snapshot_reports_unknown_codes);
    RUN_TEST(test_sen66_number_concentrations);
    RUN_TEST(test_sen66_device_status_is_ok);
    RUN_TEST(test_sen55_layouts);
    RUN_TEST(test_sen66_ignores_sen5x_only_commands);
    RUN_TEST(test_new_command_clears_previous_reply);
    RUN_TEST(test_bad_argument_crc_is_ignored);
    RUN_TEST(test_voc_state_round_trip);
    RUN_TEST(test_state_transitions);
    RUN_TEST(test_set_measurement_replaces_snapshot);
    RUN_TEST(test_transactions_are_counted);
    return UNITY_END();
}
