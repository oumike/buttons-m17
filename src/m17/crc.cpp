#include "m17/crc.h"

namespace m17 {

uint16_t crc16(const uint8_t *data, size_t len) {
    constexpr uint16_t POLY = 0x5935;
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ POLY)
                                 : static_cast<uint16_t>(crc << 1);
    }
    return crc;
}

}  // namespace m17
