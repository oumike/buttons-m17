// ES8311 / ES7243E register sequences.
//
// Adapted from Espressif's esp_codec_dev (esp-adf components/esp_codec_dev/
// device/es8311/es8311.c and es7243e/es7243e.c), Apache-2.0, which is
// GPL-3.0 compatible. Values are theirs; the flattening into one fixed
// configuration (slave, MCLK pin, 16-bit I2S, 16 kHz, MCLK = 256 * fs) is ours.

#include "audio/codecs.h"

#include <Arduino.h>
#include <Wire.h>

#include "board/board_io.h"

namespace {

bool wr(uint8_t addr, uint8_t reg, uint8_t v) {
    std::lock_guard<std::mutex> lk(boardI2cMutex());
    for (int attempt = 0; attempt < 3; ++attempt) {
        Wire.beginTransmission(addr);
        Wire.write(reg);
        Wire.write(v);
        if (Wire.endTransmission() == 0) return true;
    }
    Serial.printf("[audio] I2C 0x%02X write reg 0x%02X failed\n", addr, reg);
    return false;
}

bool rd(uint8_t addr, uint8_t reg, uint8_t &v) {
    std::lock_guard<std::mutex> lk(boardI2cMutex());
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom(static_cast<int>(addr), 1) != 1) {
        Serial.printf("[audio] I2C 0x%02X read reg 0x%02X failed\n", addr, reg);
        return false;
    }
    v = Wire.read();
    return true;
}

// Read-modify-write: v = (v & keep) | set.
bool rmw(uint8_t addr, uint8_t reg, uint8_t keep, uint8_t set) {
    uint8_t v = 0;
    return rd(addr, reg, v) && wr(addr, reg, static_cast<uint8_t>((v & keep) | set));
}

struct RegVal {
    uint8_t reg, val;
};

bool writeAll(uint8_t addr, const RegVal *seq, size_t n) {
    bool ok = true;
    for (size_t i = 0; i < n; ++i) ok &= wr(addr, seq[i].reg, seq[i].val);
    return ok;
}

}  // namespace

// ── ES8311 ──────────────────────────────────────────────────────────────────

bool es8311ChipId(uint8_t addr, uint16_t &id) {
    uint8_t hi = 0, lo = 0;
    if (!rd(addr, 0xFD, hi) || !rd(addr, 0xFE, lo)) return false;
    id = static_cast<uint16_t>(hi << 8 | lo);
    return true;
}

bool es8311Init(uint8_t addr) {
    uint16_t id = 0;
    if (!es8311ChipId(addr, id)) return false;
    Serial.printf("[audio] ES8311 chip id 0x%04X%s\n", id, id == 0x8311 ? "" : " (expected 0x8311)");

    bool ok = true;
    // es8311_open(): power-on defaults. Reg 0x44 is written twice because
    // the first I2C write after power-up occasionally fails (Espressif note).
    ok &= wr(addr, 0x0D, 0xFA);
    ok &= wr(addr, 0x44, 0x08);
    ok &= wr(addr, 0x44, 0x08);
    static const RegVal open[] = {
        {0x01, 0x30}, {0x02, 0x00}, {0x03, 0x10}, {0x16, 0x24}, {0x04, 0x10},
        {0x05, 0x00}, {0x0B, 0x00}, {0x0C, 0x00}, {0x10, 0x1F}, {0x11, 0x7F},
        {0x00, 0x80},                 // slave mode (bit 6 clear)
        {0x01, 0x3F},                 // clocks on, MCLK from pin, not inverted
    };
    ok &= writeAll(addr, open, sizeof open / sizeof open[0]);
    ok &= rmw(addr, 0x06, static_cast<uint8_t>(~0x20), 0x00);   // SCLK not inverted
    static const RegVal open2[] = {
        {0x13, 0x10}, {0x1B, 0x0A}, {0x1C, 0x6A},
        {0x44, 0x08},                 // no DAC->ADC echo reference (ADC unused)
    };
    ok &= writeAll(addr, open2, sizeof open2 / sizeof open2[0]);

    // es8311_set_fs(): 16-bit, standard I2S.
    ok &= rmw(addr, 0x09, 0xFC, 0x0C);
    ok &= rmw(addr, 0x0A, 0xFC, 0x0C);

    // es8311_config_sample(): coeff_div row {4096000, 16000, pre_div 1,
    // pre_multi 1, adc_div 1, dac_div 1, fs_mode 0, lrck 0x00FF, bclk_div 4,
    // adc_osr 0x10, dac_osr 0x20}.
    ok &= rmw(addr, 0x02, 0x07, 0x00);
    ok &= wr(addr, 0x05, 0x00);
    ok &= rmw(addr, 0x03, 0x80, 0x10);
    ok &= rmw(addr, 0x04, 0x80, 0x20);
    ok &= rmw(addr, 0x07, 0xC0, 0x00);
    ok &= wr(addr, 0x08, 0xFF);
    ok &= rmw(addr, 0x06, 0xE0, 4 - 1);

    // es8311_start() in DAC mode.
    ok &= wr(addr, 0x00, 0x80);
    ok &= wr(addr, 0x01, 0x3F);
    ok &= rmw(addr, 0x09, 0xBF, 0x00);   // DAC serial port on
    ok &= rmw(addr, 0x0A, 0xBF, 0x00);
    static const RegVal start[] = {
        {0x17, 0xBF}, {0x0E, 0x02}, {0x12, 0x00}, {0x14, 0x1A}, {0x0D, 0x01},
        {0x15, 0x40}, {0x37, 0x08}, {0x45, 0x00},
        {0x32, 0xBF},                 // DAC volume 0 dB; volume is applied in software
    };
    ok &= writeAll(addr, start, sizeof start / sizeof start[0]);
    ok &= rmw(addr, 0x31, 0x9F, 0x00);   // unmute
    return ok;
}

// ── ES7243E ─────────────────────────────────────────────────────────────────

namespace {
// es7243e.c get_db_reg(): 3 dB steps up to 33 dB, then 34.5 / 36 / 37.5.
uint8_t pgaReg(int db) {
    if (db < 0) db = 0;
    const float d = db + 0.5f;
    if (d <= 33.0f) return static_cast<uint8_t>(static_cast<int>(d) / 3);
    if (d < 36.0f) return 12;
    if (d < 37.0f) return 13;
    return 14;
}
}  // namespace

bool es7243eSetGain(uint8_t addr, int pgaDb, float *appliedDb) {
    const uint8_t r = pgaReg(pgaDb);
    if (appliedDb) *appliedDb = r <= 11 ? 3.0f * r : 33.0f + 1.5f * (r - 11);
    const uint8_t v = static_cast<uint8_t>(0x10 | r);
    return wr(addr, 0x20, v) && wr(addr, 0x21, v);
}

bool es7243eInit(uint8_t addr, int pgaDb) {
    // es7243e_open() followed by es7243e_adc_enable(true). Slave mode,
    // LRCK = MCLK / 256.
    static const RegVal seq[] = {
        {0x01, 0x3A}, {0x00, 0x80}, {0xF9, 0x00}, {0x04, 0x02}, {0x04, 0x01},
        {0xF9, 0x01}, {0x00, 0x1E}, {0x01, 0x00},
        {0x02, 0x00}, {0x03, 0x20}, {0x04, 0x01}, {0x0D, 0x00}, {0x05, 0x00},
        {0x06, 0x03}, {0x07, 0x00}, {0x08, 0xFF},
        {0x09, 0xCA}, {0x0A, 0x85}, {0x0B, 0x00}, {0x0E, 0xBF}, {0x0F, 0x80},
        {0x14, 0x0C}, {0x15, 0x0C}, {0x17, 0x02}, {0x18, 0x26}, {0x19, 0x77},
        {0x1A, 0xF4}, {0x1B, 0x66}, {0x1C, 0x44}, {0x1E, 0x00}, {0x1F, 0x0C},
        {0x20, 0x1A}, {0x21, 0x1A},
        {0x00, 0x80}, {0x01, 0x3A}, {0x16, 0x3F}, {0x16, 0x00},
        // adc_enable(true)
        {0xF9, 0x00}, {0x04, 0x01}, {0x17, 0x01}, {0x20, 0x10}, {0x21, 0x10},
        {0x00, 0x80}, {0x01, 0x3A}, {0x16, 0x3F}, {0x16, 0x00},
    };
    // Probe first so an absent chip reports cleanly instead of 46 failures.
    uint8_t probe = 0;
    if (!rd(addr, 0x00, probe)) return false;
    return writeAll(addr, seq, sizeof seq / sizeof seq[0]) && es7243eSetGain(addr, pgaDb);
}
