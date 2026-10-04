#pragma once

#include <cstdint>

namespace senxx
{

constexpr uint16_t UINT_UNKNOWN = 0xFFFF;
constexpr int16_t INT_UNKNOWN = 0x7FFF;

// Scale a physical value to Sensirion's fixed-point word. NaN -> the "unknown" code;
// out-of-range values are clamped just inside it so they can never alias "unknown".
uint16_t encodeUnsigned(float value, float scale);
int16_t encodeSigned(float value, float scale);

} // namespace senxx
