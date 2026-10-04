#pragma once

#include <cmath>

namespace senxx
{

// One snapshot of values to serve, in physical units. NaN means "unknown":
// the emulator sends Sensirion's "value unknown" code and the node omits the field.
struct Measurement {
    float pm1 = NAN;  // µg/m³
    float pm25 = NAN; // µg/m³
    float pm4 = NAN;  // µg/m³
    float pm10 = NAN; // µg/m³

    float humidity = NAN;    // %RH
    float temperature = NAN; // °C
    float vocIndex = NAN;    // index, 1..500
    float noxIndex = NAN;    // index, 1..500
    float co2 = NAN;         // ppm (SEN66 only)

    // Number concentrations, cumulative from 0.3 µm (#/cm³). Must be non-decreasing
    // in bin order, or all unknown: the driver subtracts adjacent bins as unsigned ints.
    float nc05 = NAN;
    float nc1 = NAN;
    float nc25 = NAN;
    float nc4 = NAN;
    float nc10 = NAN;
    float typicalSize = NAN; // µm (SEN5x only)
};

} // namespace senxx
