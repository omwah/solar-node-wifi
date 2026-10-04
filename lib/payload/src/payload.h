#pragma once

#include "measurement.h"

#include <cstddef>
#include <cstdint>

namespace payload
{

constexpr size_t MAX_PAYLOAD_BYTES = 4096;

struct ParseConfig {
    uint32_t maxAgeS = 1800;    // older values are served as unknown
    bool thFallbackAq = false;  // use AirGradient T/RH if the Tempest value is missing or stale
};

enum class ParseStatus { Ok, TooLarge, Malformed };

// Everything the firmware takes from one telemetry message (docs/mqtt_payload.md).
struct Telemetry {
    senxx::Measurement measurement; // NaN where unknown, stale or unusable
    float irradiance = NAN;         // W/m², reported on the state topic only
    bool debug = false;
    uint32_t debugUntil = 0;        // epoch s; 0 = not given
    uint32_t publishedAt = 0;       // top-level ts, diagnostic only
    uint16_t unknownUnits = 0;      // fields dropped for a missing or unrecognised unit
    uint16_t staleValues = 0;       // fields dropped for age
};

// nowEpoch comes from SNTP; 0 means the clock is not set, and every value is then
// treated as stale, since freshness cannot be judged.
ParseStatus parse(const char *json, size_t len, uint32_t nowEpoch, const ParseConfig &config, Telemetry &out);

} // namespace payload
