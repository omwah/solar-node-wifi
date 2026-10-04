#include "emulator.h"

#include "crc.h"
#include "encode.h"

#include <cstring>

namespace senxx
{

namespace
{

// Commands shared by SEN5x and SEN6x
constexpr uint16_t CMD_START_MEASUREMENT = 0x0021;
constexpr uint16_t CMD_STOP_MEASUREMENT = 0x0104;
constexpr uint16_t CMD_READ_DATA_READY = 0x0202;
constexpr uint16_t CMD_START_FAN_CLEANING = 0x5607;
constexpr uint16_t CMD_RW_VOC_STATE = 0x6181;
constexpr uint16_t CMD_GET_PRODUCT_NAME = 0xD014;
constexpr uint16_t CMD_GET_FIRMWARE_VERSION = 0xD100;
constexpr uint16_t CMD_RESET = 0xD304;

// SEN5x
constexpr uint16_t CMD_SEN5X_START_RHT_GAS = 0x0037;
constexpr uint16_t CMD_SEN5X_READ_VALUES = 0x03C4;
constexpr uint16_t CMD_SEN5X_READ_PM_VALUES = 0x0413;

// SEN66
constexpr uint16_t CMD_SEN66_READ_VALUES = 0x0300;
constexpr uint16_t CMD_SEN6X_READ_NUMBER_CONC = 0x0316;
constexpr uint16_t CMD_SEN6X_READ_DEVICE_STATUS = 0xD206;
constexpr uint16_t CMD_SEN6X_FORCED_CO2_RECAL = 0x6707;
constexpr uint16_t CMD_SEN6X_CO2_ASC = 0x6711;

constexpr uint8_t SEN55_ADDRESS = 0x69;
constexpr uint8_t SEN66_ADDRESS = 0x6B;

// Reported firmware version; the driver requires major >= 2.
constexpr uint8_t FW_MAJOR = 3;
constexpr uint8_t FW_MINOR = 0;

bool wordsValid(const uint8_t *bytes, size_t len)
{
    if (len % 3 != 0) {
        return false;
    }
    for (size_t i = 0; i < len; i += 3) {
        if (crc8(&bytes[i], 2) != bytes[i + 2]) {
            return false;
        }
    }
    return true;
}

} // namespace

Emulator::Emulator(Model model) : model_(model) {}

uint8_t Emulator::address() const
{
    return model_ == Model::SEN66 ? SEN66_ADDRESS : SEN55_ADDRESS;
}

void Emulator::setMeasurement(const Measurement &m)
{
    int next = 1 - active_.load(std::memory_order_relaxed);
    buffers_[next] = m;
    active_.store(next, std::memory_order_release);
}

void Emulator::onWrite(const uint8_t *data, size_t len)
{
    transactions_++;
    responseLen_ = 0;

    if (len == 0) {
        // Address-only probe from the boot scan: just ACK.
        return;
    }
    if (len == 1) {
        // Single-byte register read from the scan's IMU/charger checks
        // (registers 0x00, 0x75, 0x0A, 0x14, 0x0F). 0x00 matches none of the IDs they test for.
        static const uint8_t zero = 0x00;
        stageBytes(&zero, 1);
        return;
    }

    uint16_t cmd = static_cast<uint16_t>((data[0] << 8) | data[1]);
    const uint8_t *args = data + 2;
    size_t argBytes = len - 2;
    if (argBytes > 0 && !wordsValid(args, argBytes)) {
        // A real sensor ignores a command with a bad argument CRC.
        return;
    }
    handleCommand(cmd, args, argBytes);
}

void Emulator::handleCommand(uint16_t cmd, const uint8_t *args, size_t argBytes)
{
    const Measurement &m = buffers_[active_.load(std::memory_order_acquire)];

    switch (cmd) {
    case CMD_GET_PRODUCT_NAME:
        stageProductName();
        return;
    case CMD_GET_FIRMWARE_VERSION:
        stageVersion();
        return;
    case CMD_RESET:
    case CMD_STOP_MEASUREMENT:
        state_ = SensorState::Idle;
        return;
    case CMD_START_MEASUREMENT:
        state_ = SensorState::Measuring;
        return;
    case CMD_START_FAN_CLEANING:
        return;
    case CMD_READ_DATA_READY: {
        // Always ready, in any state: after a C3 reboot the node's driver still believes the
        // sensor is measuring and must not lose more than one cycle.
        const uint16_t ready = 0x0001;
        stageWords(&ready, 1);
        return;
    }
    case CMD_RW_VOC_STATE:
        if (argBytes == 12) {
            for (size_t w = 0; w < 4; w++) {
                vocState_[w * 2] = args[w * 3];
                vocState_[w * 2 + 1] = args[w * 3 + 1];
            }
        } else if (argBytes == 0) {
            stageBytes(vocState_, sizeof(vocState_));
        }
        return;
    default:
        break;
    }

    if (model_ == Model::SEN55) {
        switch (cmd) {
        case CMD_SEN5X_START_RHT_GAS:
            state_ = SensorState::RhtGasOnly;
            return;
        case CMD_SEN5X_READ_VALUES:
            stageMeasuredValues(m);
            return;
        case CMD_SEN5X_READ_PM_VALUES:
            stagePmAndNumberConcentrations(m);
            return;
        default:
            return;
        }
    }

    switch (cmd) {
    case CMD_SEN66_READ_VALUES:
        stageMeasuredValues(m);
        return;
    case CMD_SEN6X_READ_NUMBER_CONC:
        stageNumberConcentrations(m);
        return;
    case CMD_SEN6X_READ_DEVICE_STATUS: {
        const uint16_t status[2] = {0, 0};
        stageWords(status, 2);
        return;
    }
    case CMD_SEN6X_FORCED_CO2_RECAL: {
        // 0x8000 = zero correction; the emulator has nothing to recalibrate.
        const uint16_t correction = 0x8000;
        stageWords(&correction, 1);
        return;
    }
    case CMD_SEN6X_CO2_ASC:
        if (argBytes == 0) {
            const uint16_t enabled = 0x0001;
            stageWords(&enabled, 1);
        }
        return;
    default:
        // Other SEN6x settings (temperature offset, altitude, pressure, CO2 factory reset)
        // are accepted without effect.
        return;
    }
}

void Emulator::stageWords(const uint16_t *words, size_t count)
{
    responseLen_ = 0;
    for (size_t i = 0; i < count && responseLen_ + 3 <= MAX_RESPONSE; i++) {
        response_[responseLen_++] = static_cast<uint8_t>(words[i] >> 8);
        response_[responseLen_++] = static_cast<uint8_t>(words[i] & 0xFF);
        response_[responseLen_] = crc8(&response_[responseLen_ - 2], 2);
        responseLen_++;
    }
}

void Emulator::stageBytes(const uint8_t *bytes, size_t count)
{
    if (count == 1) {
        response_[0] = bytes[0];
        responseLen_ = 1;
        return;
    }
    uint16_t words[MAX_RESPONSE / 3];
    size_t n = 0;
    for (size_t i = 0; i + 1 < count && n < MAX_RESPONSE / 3; i += 2) {
        words[n++] = static_cast<uint16_t>((bytes[i] << 8) | bytes[i + 1]);
    }
    stageWords(words, n);
}

void Emulator::stageProductName()
{
    uint8_t name[32] = {};
    const char *text = model_ == Model::SEN66 ? "SEN66" : "SEN55";
    std::memcpy(name, text, std::strlen(text));
    stageBytes(name, sizeof(name));
}

void Emulator::stageVersion()
{
    // Layout the driver parses: fw major, fw minor, fw debug, hw major, hw minor,
    // protocol major, protocol minor, padding.
    const uint8_t version[8] = {FW_MAJOR, FW_MINOR, 0, 1, 0, 1, 0, 0};
    stageBytes(version, sizeof(version));
}

void Emulator::stageMeasuredValues(const Measurement &m)
{
    uint16_t words[9];
    size_t n = 0;
    words[n++] = encodeUnsigned(m.pm1, 10);
    words[n++] = encodeUnsigned(m.pm25, 10);
    words[n++] = encodeUnsigned(m.pm4, 10);
    words[n++] = encodeUnsigned(m.pm10, 10);
    words[n++] = static_cast<uint16_t>(encodeSigned(m.humidity, 100));
    words[n++] = static_cast<uint16_t>(encodeSigned(m.temperature, 200));
    words[n++] = static_cast<uint16_t>(encodeSigned(m.vocIndex, 10));
    words[n++] = static_cast<uint16_t>(encodeSigned(m.noxIndex, 10));
    if (model_ == Model::SEN66) {
        words[n++] = encodeUnsigned(m.co2, 1);
    }
    stageWords(words, n);
}

void Emulator::stagePmAndNumberConcentrations(const Measurement &m)
{
    const uint16_t words[10] = {
        encodeUnsigned(m.pm1, 10),  encodeUnsigned(m.pm25, 10), encodeUnsigned(m.pm4, 10),
        encodeUnsigned(m.pm10, 10), encodeUnsigned(m.nc05, 10), encodeUnsigned(m.nc1, 10),
        encodeUnsigned(m.nc25, 10), encodeUnsigned(m.nc4, 10),  encodeUnsigned(m.nc10, 10),
        encodeUnsigned(m.typicalSize, 1000),
    };
    stageWords(words, 10);
}

void Emulator::stageNumberConcentrations(const Measurement &m)
{
    const uint16_t words[5] = {
        encodeUnsigned(m.nc05, 10), encodeUnsigned(m.nc1, 10), encodeUnsigned(m.nc25, 10),
        encodeUnsigned(m.nc4, 10),  encodeUnsigned(m.nc10, 10),
    };
    stageWords(words, 5);
}

} // namespace senxx
