#pragma once
// M17 CRC-16: polynomial 0x5935, initial value 0xFFFF, no reflection, no
// final XOR (spec, "CRC"). Bit-wise; the inputs are at most a few dozen bytes.

#include <cstddef>
#include <cstdint>

namespace m17 {

uint16_t crc16(const uint8_t *data, size_t len);

}  // namespace m17
