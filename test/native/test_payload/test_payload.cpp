#include <cmath>
#include <cstring>
#include <payload.h>
#include <string>
#include <unity.h>

using namespace payload;

void setUp() {}
void tearDown() {}

namespace
{

const uint32_t NOW = 1791052648;

// The example from docs/mqtt_payload.md §3, with the reference install's °F readings.
const char *REFERENCE = R"json({
  "ts": 1791052648,
  "aq": {
    "device": "Roof AirGradient Open Air",
    "prefix": "roof_airgradient_open_air_",
    "values": {
      "pm1":            [0,      "µg/m³", 1791052610],
      "pm2_5":          [6.09,   "µg/m³", 1791052610],
      "pm10":           [2.17,   "µg/m³", 1791052610],
      "pm0_3":          [228,    "particles/dL", 1791052610],
      "carbon_dioxide": [482,    "ppm",   1791052610],
      "voc_index":      [71,     null,    1791052610],
      "nox_index":      [1,      null,    1791052610],
      "temperature":    [85.514, "°F",    1791052610],
      "humidity":       [49.96,  "%",     1791052610]
    }
  },
  "env": {
    "device": "ST-00000000",
    "prefix": "st_00000000_",
    "values": {
      "temperature":          [77.45,  "°F",   1791052595],
      "wet_bulb_temperature": [63.61,  "°F",   1791052595],
      "humidity":             [47.28,  "%",    1791052595],
      "irradiance":           [0,      "W/m²", 1791052595],
      "air_pressure":         [982.39, "hPa",  1791052595]
    }
  },
  "debug": false,
  "debug_until": null
})json";

Telemetry parseOk(const std::string &json, uint32_t now = NOW, ParseConfig config = {})
{
    Telemetry t;
    TEST_ASSERT_EQUAL(static_cast<int>(ParseStatus::Ok), static_cast<int>(parse(json.c_str(), json.size(), now, config, t)));
    return t;
}

std::string replace(std::string s, const std::string &from, const std::string &to)
{
    size_t pos = s.find(from);
    TEST_ASSERT_TRUE(pos != std::string::npos);
    return s.replace(pos, from.size(), to);
}

} // namespace

void test_reference_payload()
{
    Telemetry t = parseOk(REFERENCE);
    const senxx::Measurement &m = t.measurement;
    TEST_ASSERT_EQUAL_FLOAT(0.0f, m.pm1);
    TEST_ASSERT_EQUAL_FLOAT(6.09f, m.pm25);
    TEST_ASSERT_TRUE(std::isnan(m.pm4));
    TEST_ASSERT_EQUAL_FLOAT(2.17f, m.pm10);
    TEST_ASSERT_EQUAL_FLOAT(482.0f, m.co2);
    TEST_ASSERT_EQUAL_FLOAT(71.0f, m.vocIndex);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, m.noxIndex);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.25f, m.temperature); // Tempest, °F -> °C
    TEST_ASSERT_EQUAL_FLOAT(47.28f, m.humidity);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, t.irradiance);
    TEST_ASSERT_TRUE(std::isnan(m.nc05));
    TEST_ASSERT_FALSE(t.debug);
    TEST_ASSERT_EQUAL_UINT32(0, t.debugUntil);
    TEST_ASSERT_EQUAL_UINT32(1791052648, t.publishedAt);
    TEST_ASSERT_EQUAL_UINT16(0, t.unknownUnits);
    TEST_ASSERT_EQUAL_UINT16(0, t.staleValues);
}

void test_wet_bulb_never_matches_temperature()
{
    std::string json = replace(REFERENCE, R"("temperature":          [77.45,  "°F",   1791052595],)", "");
    Telemetry t = parseOk(json);
    TEST_ASSERT_TRUE(std::isnan(t.measurement.temperature));
}

void test_aq_temperature_fallback_is_off_by_default()
{
    std::string json = replace(REFERENCE, R"("temperature":          [77.45,  "°F",   1791052595],)", "");
    TEST_ASSERT_TRUE(std::isnan(parseOk(json).measurement.temperature));
    ParseConfig config;
    config.thFallbackAq = true;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 29.73f, parseOk(json, NOW, config).measurement.temperature);
}

void test_stale_value_is_unknown()
{
    std::string json = replace(REFERENCE, R"([482,    "ppm",   1791052610])", R"([482, "ppm", 1791050000])");
    Telemetry t = parseOk(json);
    TEST_ASSERT_TRUE(std::isnan(t.measurement.co2));
    TEST_ASSERT_EQUAL_UINT16(1, t.staleValues);
    TEST_ASSERT_EQUAL_FLOAT(6.09f, t.measurement.pm25); // others unaffected
}

void test_missing_timestamp_is_stale()
{
    std::string json = replace(REFERENCE, R"([482,    "ppm",   1791052610])", R"([482, "ppm", null])");
    TEST_ASSERT_TRUE(std::isnan(parseOk(json).measurement.co2));
}

void test_unset_clock_makes_everything_unknown()
{
    Telemetry t = parseOk(REFERENCE, 0);
    TEST_ASSERT_TRUE(std::isnan(t.measurement.pm25));
    TEST_ASSERT_TRUE(std::isnan(t.measurement.temperature));
}

void test_null_value_is_unknown()
{
    std::string json = replace(REFERENCE, R"([6.09,   "µg/m³", 1791052610])", R"([null, "µg/m³", 1791052610])");
    TEST_ASSERT_TRUE(std::isnan(parseOk(json).measurement.pm25));
}

void test_unrecognised_unit_is_unknown()
{
    std::string json = replace(REFERENCE, R"([482,    "ppm",   1791052610])", R"([482, "ppb", 1791052610])");
    Telemetry t = parseOk(json);
    TEST_ASSERT_TRUE(std::isnan(t.measurement.co2));
    TEST_ASSERT_EQUAL_UINT16(1, t.unknownUnits);
}

void test_missing_unit_where_required_is_unknown()
{
    std::string json = replace(REFERENCE, R"([47.28,  "%",    1791052595])", R"([47.28, null, 1791052595])");
    TEST_ASSERT_TRUE(std::isnan(parseOk(json).measurement.humidity));
}

void test_unit_spellings()
{
    std::string greekMu = replace(REFERENCE, R"([6.09,   "µg/m³", 1791052610])", "[6.09, \"μg/m³\", 1791052610]");
    TEST_ASSERT_EQUAL_FLOAT(6.09f, parseOk(greekMu).measurement.pm25);
    std::string ascii = replace(REFERENCE, R"([6.09,   "µg/m³", 1791052610])", R"([6.09, "ug/m3", 1791052610])");
    TEST_ASSERT_EQUAL_FLOAT(6.09f, parseOk(ascii).measurement.pm25);
    std::string celsius = replace(REFERENCE, R"([77.45,  "°F",   1791052595])", R"([25.25, "°C", 1791052595])");
    TEST_ASSERT_EQUAL_FLOAT(25.25f, parseOk(celsius).measurement.temperature);
    std::string kelvin = replace(REFERENCE, R"([77.45,  "°F",   1791052595])", R"([298.4, "K", 1791052595])");
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.25f, parseOk(kelvin).measurement.temperature);
}

void test_aliases()
{
    std::string json = replace(REFERENCE, R"("carbon_dioxide":)", R"("co2":)");
    json = replace(json, R"("pm2_5":)", R"("pm25":)");
    Telemetry t = parseOk(json);
    TEST_ASSERT_EQUAL_FLOAT(482.0f, t.measurement.co2);
    TEST_ASSERT_EQUAL_FLOAT(6.09f, t.measurement.pm25);
}

void test_null_block()
{
    std::string json = R"({"ts": 1791052648, "aq": null, "env": null})";
    Telemetry t = parseOk(json);
    TEST_ASSERT_TRUE(std::isnan(t.measurement.pm25));
    TEST_ASSERT_TRUE(std::isnan(t.measurement.temperature));
}

void test_debug_fields()
{
    std::string json = replace(REFERENCE, R"("debug": false,
  "debug_until": null)", R"("debug": true, "debug_until": 1791056248)");
    Telemetry t = parseOk(json);
    TEST_ASSERT_TRUE(t.debug);
    TEST_ASSERT_EQUAL_UINT32(1791056248, t.debugUntil);
}

void test_malformed_is_rejected()
{
    Telemetry t;
    const char *bad = R"({"aq": {"values": {"pm1": [1, "µg/m³", 1)";
    TEST_ASSERT_EQUAL(static_cast<int>(ParseStatus::Malformed), static_cast<int>(parse(bad, std::strlen(bad), NOW, {}, t)));
    const char *array = "[1, 2, 3]";
    TEST_ASSERT_EQUAL(static_cast<int>(ParseStatus::Malformed), static_cast<int>(parse(array, std::strlen(array), NOW, {}, t)));
}

void test_oversize_is_rejected()
{
    std::string big(MAX_PAYLOAD_BYTES + 1, ' ');
    Telemetry t;
    TEST_ASSERT_EQUAL(static_cast<int>(ParseStatus::TooLarge), static_cast<int>(parse(big.c_str(), big.size(), NOW, {}, t)));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_reference_payload);
    RUN_TEST(test_wet_bulb_never_matches_temperature);
    RUN_TEST(test_aq_temperature_fallback_is_off_by_default);
    RUN_TEST(test_stale_value_is_unknown);
    RUN_TEST(test_missing_timestamp_is_stale);
    RUN_TEST(test_unset_clock_makes_everything_unknown);
    RUN_TEST(test_null_value_is_unknown);
    RUN_TEST(test_unrecognised_unit_is_unknown);
    RUN_TEST(test_missing_unit_where_required_is_unknown);
    RUN_TEST(test_unit_spellings);
    RUN_TEST(test_aliases);
    RUN_TEST(test_null_block);
    RUN_TEST(test_debug_fields);
    RUN_TEST(test_malformed_is_rejected);
    RUN_TEST(test_oversize_is_rejected);
    return UNITY_END();
}
