#pragma once

#include <cstddef>
#include <cstdint>

namespace senxx
{

// Sensirion CRC-8: polynomial 0x31, initial value 0xFF, no reflection, no final XOR.
// Covers each 2-byte word on the wire.
uint8_t crc8(const uint8_t *data, size_t len);

} // namespace senxx
