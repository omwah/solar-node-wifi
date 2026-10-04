#pragma once

#include "Wire.h"
#include "detect/ScanI2C.h"

#include <cstdint>

// Host shim: the fake bus has no clock to change.
class ReClockI2C
{
  public:
    void setup(TwoWire *, ScanI2C::I2CPort) {}
    uint32_t setClock(uint32_t) { return 0; }
    void restoreClock(uint32_t) {}
};

class ReClockI2CGuard
{
  public:
    ReClockI2CGuard(ReClockI2C &, uint32_t) {}
};
