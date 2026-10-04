#include "encode.h"

#include <cmath>

namespace senxx
{

uint16_t encodeUnsigned(float value, float scale)
{
    if (std::isnan(value)) {
        return UINT_UNKNOWN;
    }
    float scaled = std::round(value * scale);
    if (scaled <= 0.0f) {
        return 0;
    }
    if (scaled >= static_cast<float>(UINT_UNKNOWN - 1)) {
        return UINT_UNKNOWN - 1;
    }
    return static_cast<uint16_t>(scaled);
}

int16_t encodeSigned(float value, float scale)
{
    if (std::isnan(value)) {
        return INT_UNKNOWN;
    }
    float scaled = std::round(value * scale);
    if (scaled <= -32768.0f) {
        return -32768;
    }
    if (scaled >= static_cast<float>(INT_UNKNOWN - 1)) {
        return INT_UNKNOWN - 1;
    }
    return static_cast<int16_t>(scaled);
}

} // namespace senxx
