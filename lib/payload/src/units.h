#pragma once

namespace payload
{

enum class Quantity { Temperature, Humidity, MassConcentration, Ppm, Irradiance, Index };

// Converts value from the unit HA reported to the unit the emulator serves:
// °C, %RH, µg/m³, ppm, W/m², or unitless. Returns NaN for a unit it does not
// recognise (or a missing unit where one is required), so a guess is never served.
float toCanonical(Quantity quantity, float value, const char *unit);

} // namespace payload
