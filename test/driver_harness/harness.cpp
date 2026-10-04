// T2 driver-in-the-loop test: Meshtastic's unmodified SENXXSensor driver talks to the
// project's senxx::Emulator through a fake TwoWire bus. Proves the stock firmware
// detects, initialises and reads the emulated sensor.

#include "Arduino.h"
#include "Wire.h"
#include "gps/RTC.h"
#include "harness_log.h"
#include "modules/Telemetry/Sensor/SEN5XSensor.h"
#include "modules/Telemetry/Sensor/SEN6XSensor.h"

#include <emulator.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

std::pair<uint8_t, TwoWire *> nodeTelemetrySensorsMap[_meshtastic_TelemetrySensorType_MAX + 1];

// ---- fake clock, RTC and log ---------------------------------------------------------

static uint32_t fakeMillis = 1000;
uint32_t millis() { return fakeMillis; }
void delay(uint32_t ms) { fakeMillis += ms; }

static RTCQuality rtcQuality = RTCQualityNone;
RTCQuality getRTCQuality() { return rtcQuality; }
uint32_t getValidTime(RTCQuality minQuality, bool)
{
    return rtcQuality >= minQuality ? 1790000000u + fakeMillis / 1000 : 0;
}

static int errorCount = 0;
static bool verbose = false;
static bool expectingErrors = false;
// The harness builds without a filesystem, so the driver's state persistence reports this.
static const char *EXPECTED_ERROR = "Filesystem not implemented";

void harnessLog(const char *level, const char *fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    bool isError = std::strcmp(level, "ERROR") == 0 && !std::strstr(buf, EXPECTED_ERROR) && !expectingErrors;
    if (isError) {
        errorCount++;
    }
    if (verbose || isError) {
        std::fprintf(stderr, "  [%s] %s\n", level, buf);
    }
}

// ---- fake bus ------------------------------------------------------------------------

static senxx::Emulator *device = nullptr;
static uint8_t txAddress = 0;
static std::vector<uint8_t> txBuffer;
static std::vector<uint8_t> rxBuffer;
static size_t rxPos = 0;

void TwoWire::beginTransmission(uint8_t address)
{
    txAddress = address;
    txBuffer.clear();
}

size_t TwoWire::write(uint8_t value)
{
    txBuffer.push_back(value);
    return 1;
}

size_t TwoWire::write(const uint8_t *data, size_t len)
{
    txBuffer.insert(txBuffer.end(), data, data + len);
    return len;
}

uint8_t TwoWire::endTransmission(bool)
{
    if (!device || txAddress != device->address()) {
        return 2; // address NACK
    }
    device->onWrite(txBuffer.data(), txBuffer.size());
    return 0;
}

size_t TwoWire::requestFrom(uint8_t address, size_t len)
{
    rxBuffer.clear();
    rxPos = 0;
    if (!device || address != device->address()) {
        return 0;
    }
    // A real slave clocks out whatever is staged, then idles high (0xFF).
    for (size_t i = 0; i < len; i++) {
        rxBuffer.push_back(i < device->responseLength() ? device->response()[i] : 0xFF);
    }
    return len;
}

int TwoWire::read() { return rxPos < rxBuffer.size() ? rxBuffer[rxPos++] : -1; }
int TwoWire::available() { return static_cast<int>(rxBuffer.size() - rxPos); }

// ---- test plumbing ---------------------------------------------------------------------

static int failures = 0;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            std::fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                     \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

static senxx::Measurement reference()
{
    senxx::Measurement m;
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

// One telemetry cycle the way AirQualityTelemetryModule drives a sleep-capable sensor:
// wake if not active, wait out pendingForReadyMs(), read, then sleep.
static bool readCycle(TelemetrySensor &sensor, meshtastic_Telemetry &out)
{
    out = meshtastic_Telemetry_init_zero;
    if (!sensor.isActive()) {
        delay(sensor.wakeUp());
    }
    for (int guard = 0; guard < 10; guard++) {
        int32_t pending = sensor.pendingForReadyMs();
        if (pending <= 0) {
            break;
        }
        delay(static_cast<uint32_t>(pending));
    }
    delay(1000); // the driver insists on >= 1 s between data polls
    bool ok = sensor.getMetrics(&out);
    sensor.sleep();
    delay(30 * 60 * 1000); // telemetry interval
    return ok;
}

static void startTest(const char *name)
{
    std::printf("%s\n", name);
    errorCount = 0;
    fakeMillis = 1000;
    rtcQuality = RTCQualityNone;
}

static void endTest()
{
    CHECK(errorCount == 0);
}

static ScanI2C::FoundDevice foundAt(uint8_t address)
{
    return ScanI2C::FoundDevice(ScanI2C::DeviceType::SEN6X, ScanI2C::DeviceAddress(ScanI2C::I2CPort::WIRE, address));
}

// ---- tests -----------------------------------------------------------------------------

static void test_sen66_probe_and_init()
{
    startTest("SEN66: probe, init and 100 read cycles");
    senxx::Emulator emu(senxx::Model::SEN66);
    emu.setMeasurement(reference());
    device = &emu;
    TwoWire wire;

    SEN6XSensor sensor;
    CHECK(sensor.probe(&wire, 0x6B, ScanI2C::I2CPort::WIRE));
    ScanI2C::FoundDevice dev = foundAt(0x6B);
    CHECK(sensor.initDevice(&wire, &dev));

    int good = 0;
    for (int cycle = 0; cycle < 100; cycle++) {
        meshtastic_Telemetry t;
        if (!readCycle(sensor, t)) {
            continue;
        }
        const meshtastic_AirQualityMetrics &aq = t.variant.air_quality_metrics;
        bool match = aq.has_pm10_standard && aq.pm10_standard == 0 && aq.has_pm25_standard && aq.pm25_standard == 6 &&
                     !aq.has_pm40_standard && aq.has_pm100_standard && aq.pm100_standard == 2 && aq.has_co2 &&
                     aq.co2 == 482 && aq.has_pm_temperature && std::fabs(aq.pm_temperature - 25.25f) < 0.01f &&
                     aq.has_pm_humidity && std::fabs(aq.pm_humidity - 47.28f) < 0.01f && aq.has_pm_voc_idx &&
                     std::fabs(aq.pm_voc_idx - 71.0f) < 0.01f && aq.has_pm_nox_idx &&
                     std::fabs(aq.pm_nox_idx - 1.0f) < 0.01f && !aq.has_particles_05um && !aq.has_particles_100um &&
                     aq.has_pm_status_flags && aq.pm_status_flags == 0;
        if (match) {
            good++;
        }
    }
    std::printf("  %d/100 cycles returned the expected AirQualityMetrics\n", good);
    CHECK(good == 100);
    endTest();
}

static void test_sen66_unknown_values_are_omitted()
{
    startTest("SEN66: unknown values are omitted, not zero");
    senxx::Emulator emu(senxx::Model::SEN66);
    device = &emu;
    TwoWire wire;
    SEN6XSensor sensor;
    ScanI2C::FoundDevice dev = foundAt(0x6B);
    CHECK(sensor.probe(&wire, 0x6B, ScanI2C::I2CPort::WIRE));
    CHECK(sensor.initDevice(&wire, &dev));

    meshtastic_Telemetry t;
    CHECK(readCycle(sensor, t));
    const meshtastic_AirQualityMetrics &aq = t.variant.air_quality_metrics;
    CHECK(!aq.has_pm10_standard && !aq.has_pm25_standard && !aq.has_pm100_standard);
    CHECK(!aq.has_co2 && !aq.has_pm_temperature && !aq.has_pm_humidity);
    CHECK(!aq.has_pm_voc_idx && !aq.has_pm_nox_idx);
    endTest();
}

static void test_sen66_survives_c3_reboot_mid_cycle()
{
    startTest("SEN66: C3 reboot mid-cycle costs at most one cycle");
    senxx::Emulator emu(senxx::Model::SEN66);
    emu.setMeasurement(reference());
    device = &emu;
    TwoWire wire;
    SEN6XSensor sensor;
    ScanI2C::FoundDevice dev = foundAt(0x6B);
    CHECK(sensor.probe(&wire, 0x6B, ScanI2C::I2CPort::WIRE));
    CHECK(sensor.initDevice(&wire, &dev));

    meshtastic_Telemetry t;
    CHECK(readCycle(sensor, t));

    // The C3 reboots: a fresh emulator in its power-on state, data arriving again later.
    senxx::Emulator rebooted(senxx::Model::SEN66);
    rebooted.setMeasurement(reference());
    device = &rebooted;
    errorCount = 0;

    int good = 0;
    for (int cycle = 0; cycle < 3; cycle++) {
        if (readCycle(sensor, t) && t.variant.air_quality_metrics.has_co2) {
            good++;
        }
    }
    CHECK(good >= 2);
    endTest();
}

static void test_sen66_with_valid_clock()
{
    startTest("SEN66: valid RTC (cleaning schedule and VOC state paths)");
    senxx::Emulator emu(senxx::Model::SEN66);
    emu.setMeasurement(reference());
    device = &emu;
    TwoWire wire;
    SEN6XSensor sensor;
    ScanI2C::FoundDevice dev = foundAt(0x6B);
    rtcQuality = RTCQualityNTP;
    CHECK(sensor.probe(&wire, 0x6B, ScanI2C::I2CPort::WIRE));
    CHECK(sensor.initDevice(&wire, &dev));
    int good = 0;
    for (int cycle = 0; cycle < 10; cycle++) {
        meshtastic_Telemetry t;
        if (readCycle(sensor, t) && t.variant.air_quality_metrics.has_co2) {
            good++;
        }
    }
    CHECK(good == 10);
    endTest();
}

static void test_wrong_address_not_detected()
{
    startTest("SEN66 emulator is not detected as SEN5X at 0x69");
    senxx::Emulator emu(senxx::Model::SEN66);
    device = &emu;
    TwoWire wire;
    SEN5XSensor sensor;
    expectingErrors = true; // the failed probe logs I2C errors by design
    CHECK(!sensor.probe(&wire, 0x69, ScanI2C::I2CPort::WIRE));
    expectingErrors = false;
    endTest();
}

static void test_sen55_fallback()
{
    startTest("SEN55 fallback: probe, init and read");
    senxx::Emulator emu(senxx::Model::SEN55);
    emu.setMeasurement(reference());
    device = &emu;
    TwoWire wire;
    SEN5XSensor sensor;
    ScanI2C::FoundDevice dev = foundAt(0x69);
    CHECK(sensor.probe(&wire, 0x69, ScanI2C::I2CPort::WIRE));
    CHECK(sensor.initDevice(&wire, &dev));
    int good = 0;
    for (int cycle = 0; cycle < 10; cycle++) {
        meshtastic_Telemetry t;
        if (!readCycle(sensor, t)) {
            continue;
        }
        const meshtastic_AirQualityMetrics &aq = t.variant.air_quality_metrics;
        if (aq.has_pm25_standard && aq.pm25_standard == 6 && aq.has_pm_temperature && !aq.has_co2) {
            good++;
        }
    }
    CHECK(good == 10);
    endTest();
}

int main(int argc, char **argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    verbose = argc > 1 && std::string(argv[1]) == "-v";
    test_sen66_probe_and_init();
    test_sen66_unknown_values_are_omitted();
    test_sen66_survives_c3_reboot_mid_cycle();
    test_sen66_with_valid_clock();
    test_wrong_address_not_detected();
    test_sen55_fallback();
    if (failures) {
        std::printf("FAILED: %d check(s)\n", failures);
        return 1;
    }
    std::printf("PASSED\n");
    return 0;
}
