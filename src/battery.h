#pragma once

namespace bridge
{

// Optional battery-sense wire on XIAO D1 (GPIO3) through a 1 MΩ / 1 MΩ divider
// (docs/PLAN.md §4.7.2). Returns NaN if the ADC can't be read.
bool batteryInit();
float batteryVolts(float calibration);

} // namespace bridge
