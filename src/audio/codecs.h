#pragma once
// Register-level bring-up for the Wio's two codecs. Both are I2S slaves and
// need MCLK running (i2s started) before init.

#include <cstdint>

// ES8311 used as DAC only (it also has an ADC, but the mic goes to the
// ES7243E). Reads the chip ID first; false if absent or any write fails.
bool es8311Init(uint8_t addr);
bool es8311ChipId(uint8_t addr, uint16_t &id);   // 0x8311 expected

// ES7243E mic ADC.
bool es7243eInit(uint8_t addr, int pgaDb);
// Sets the PGA to the nearest step to `pgaDb` (3 dB steps to 33,
// then 34.5 / 36 / 37.5 max). `appliedDb` receives the step actually used.
bool es7243eSetGain(uint8_t addr, int pgaDb, float *appliedDb = nullptr);
