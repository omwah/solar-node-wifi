#pragma once

#include <cstddef>
#include <cstdint>

// Fake Arduino TwoWire backed by the harness bus (see harness.cpp).
class TwoWire
{
  public:
    void beginTransmission(uint8_t address);
    size_t write(uint8_t value);
    size_t write(const uint8_t *data, size_t len);
    uint8_t endTransmission(bool sendStop = true);
    size_t requestFrom(uint8_t address, size_t len);
    int read();
    int available();
    void setClock(uint32_t) {}
};
