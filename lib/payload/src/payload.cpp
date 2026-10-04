#include "payload.h"

#include "units.h"

#include <ArduinoJson.h>
#include <cmath>

namespace payload
{

namespace
{

struct Field {
    const char *block;
    const char *keys[2]; // primary key, then an alias (or nullptr)
    Quantity quantity;
};

enum FieldId { PM1, PM25, PM4, PM10, VOC, NOX, CO2, ENV_T, ENV_RH, AQ_T, AQ_RH, IRRADIANCE, FIELD_COUNT };

// Exact keys only: suffix matching would let wet_bulb_temperature pass as temperature.
const Field FIELDS[FIELD_COUNT] = {
    {"aq", {"pm1", nullptr}, Quantity::MassConcentration},
    {"aq", {"pm2_5", "pm25"}, Quantity::MassConcentration},
    {"aq", {"pm4", nullptr}, Quantity::MassConcentration},
    {"aq", {"pm10", nullptr}, Quantity::MassConcentration},
    {"aq", {"voc_index", nullptr}, Quantity::Index},
    {"aq", {"nox_index", nullptr}, Quantity::Index},
    {"aq", {"carbon_dioxide", "co2"}, Quantity::Ppm},
    {"env", {"temperature", nullptr}, Quantity::Temperature},
    {"env", {"humidity", nullptr}, Quantity::Humidity},
    {"aq", {"temperature", nullptr}, Quantity::Temperature},
    {"aq", {"humidity", nullptr}, Quantity::Humidity},
    {"env", {"irradiance", "solar_radiation"}, Quantity::Irradiance},
};

// Reads one [value, unit, ts] triple. Returns NaN if absent, null, stale or in an unknown unit.
float readField(JsonObjectConst root, const Field &field, uint32_t nowEpoch, uint32_t maxAgeS, Telemetry &out)
{
    JsonObjectConst values = root[field.block]["values"];
    if (values.isNull()) {
        return NAN;
    }
    JsonArrayConst triple;
    for (const char *key : field.keys) {
        if (key && values[key].is<JsonArrayConst>()) {
            triple = values[key];
            break;
        }
    }
    if (triple.isNull() || !triple[0].is<float>()) {
        return NAN;
    }

    uint32_t ts = triple[2].is<uint32_t>() ? triple[2].as<uint32_t>() : 0;
    if (nowEpoch == 0 || ts == 0 || (nowEpoch > ts && nowEpoch - ts > maxAgeS)) {
        out.staleValues++;
        return NAN;
    }

    const char *unit = triple[1].is<const char *>() ? triple[1].as<const char *>() : nullptr;
    float value = toCanonical(field.quantity, triple[0].as<float>(), unit);
    if (std::isnan(value)) {
        out.unknownUnits++;
    }
    return value;
}

} // namespace

ParseStatus parse(const char *json, size_t len, uint32_t nowEpoch, const ParseConfig &config, Telemetry &out)
{
    out = Telemetry{};
    if (len > MAX_PAYLOAD_BYTES) {
        return ParseStatus::TooLarge;
    }

    // Parse only the keys the firmware uses, so extra HA sensors cost no RAM.
    JsonDocument filter;
    for (const Field &f : FIELDS) {
        for (const char *key : f.keys) {
            if (key) {
                filter[f.block]["values"][key] = true;
            }
        }
    }
    filter["ts"] = true;
    filter["debug"] = true;
    filter["debug_until"] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json, len, DeserializationOption::Filter(filter));
    if (err || !doc.is<JsonObjectConst>()) {
        return ParseStatus::Malformed;
    }
    JsonObjectConst root = doc.as<JsonObjectConst>();

    float v[FIELD_COUNT];
    for (int i = 0; i < FIELD_COUNT; i++) {
        bool unused = (i == AQ_T || i == AQ_RH) && !config.thFallbackAq;
        v[i] = unused ? NAN : readField(root, FIELDS[i], nowEpoch, config.maxAgeS, out);
    }

    senxx::Measurement &m = out.measurement;
    m.pm1 = v[PM1];
    m.pm25 = v[PM25];
    m.pm4 = v[PM4];
    m.pm10 = v[PM10];
    m.vocIndex = v[VOC];
    m.noxIndex = v[NOX];
    m.co2 = v[CO2];
    m.temperature = v[ENV_T];
    m.humidity = v[ENV_RH];
    if (config.thFallbackAq) {
        if (std::isnan(m.temperature)) {
            m.temperature = v[AQ_T];
        }
        if (std::isnan(m.humidity)) {
            m.humidity = v[AQ_RH];
        }
    }
    out.irradiance = v[IRRADIANCE];

    out.publishedAt = root["ts"].is<uint32_t>() ? root["ts"].as<uint32_t>() : 0;
    out.debug = root["debug"].is<bool>() && root["debug"].as<bool>();
    out.debugUntil = root["debug_until"].is<uint32_t>() ? root["debug_until"].as<uint32_t>() : 0;
    return ParseStatus::Ok;
}

} // namespace payload
