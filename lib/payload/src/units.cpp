#include "units.h"

#include <cmath>
#include <cstring>

namespace payload
{

namespace
{

bool is(const char *unit, const char *candidate)
{
    return std::strcmp(unit, candidate) == 0;
}

} // namespace

float toCanonical(Quantity quantity, float value, const char *unit)
{
    if (std::isnan(value)) {
        return NAN;
    }
    if (quantity == Quantity::Index) {
        return value; // VOC/NOx indices are unitless; whatever HA reports is ignored
    }
    if (!unit) {
        return NAN;
    }

    switch (quantity) {
    case Quantity::Temperature:
        if (is(unit, "°C")) {
            return value;
        }
        if (is(unit, "°F")) {
            return (value - 32.0f) * 5.0f / 9.0f;
        }
        if (is(unit, "K")) {
            return value - 273.15f;
        }
        return NAN;
    case Quantity::Humidity:
        return is(unit, "%") ? value : NAN;
    case Quantity::MassConcentration:
        // Micro sign (U+00B5), Greek mu (U+03BC), or ASCII
        return (is(unit, "µg/m³") || is(unit, "μg/m³") || is(unit, "ug/m3")) ? value : NAN;
    case Quantity::Ppm:
        return is(unit, "ppm") ? value : NAN;
    case Quantity::Irradiance:
        return (is(unit, "W/m²") || is(unit, "W/m2")) ? value : NAN;
    case Quantity::Index:
        break;
    }
    return NAN;
}

} // namespace payload
