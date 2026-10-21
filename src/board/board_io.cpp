// PCA9555 bring-up for the Wio Tracker L2. Sequence and boot states follow
// camillia-mt's hal/wio_tracker_l2_io.cpp, except that this firmware does not
// use the GNSS (powered off) and does not drive the LCD until milestone 7.

#include "board/board_io.h"

#include <Arduino.h>
#include <Wire.h>

#include <mutex>

#include "board_config.h"

#if defined(HAS_EXPANDER) && HAS_EXPANDER

namespace {

// PCA9555 register map (identical on TCA9555/XL9555).
constexpr uint8_t REG_IN0 = 0x00;
constexpr uint8_t REG_OUT0 = 0x02;
constexpr uint8_t REG_CFG0 = 0x06;   // 1 = input

std::mutex s_mu;                     // Wire is shared with codecs/touch later
uint16_t s_out = 0xFFFF;
uint16_t s_cfg = 0xFFFF;
bool s_ready = false;

bool writePair(uint8_t reg, uint16_t v) {
    Wire.beginTransmission(EXPANDER_ADDR);
    Wire.write(reg);
    Wire.write(static_cast<uint8_t>(v & 0xFF));
    Wire.write(static_cast<uint8_t>(v >> 8));
    return Wire.endTransmission() == 0;
}

bool readPair(uint8_t reg, uint16_t &v) {
    Wire.beginTransmission(EXPANDER_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(static_cast<int>(EXPANDER_ADDR), 2) != 2) return false;
    const uint8_t lo = Wire.read();
    const uint8_t hi = Wire.read();
    v = static_cast<uint16_t>(lo | (hi << 8));
    return true;
}

void stage(uint8_t bit, bool high) {
    s_cfg &= static_cast<uint16_t>(~(1u << bit));
    if (high) s_out |= static_cast<uint16_t>(1u << bit);
    else s_out &= static_cast<uint16_t>(~(1u << bit));
}

bool commit() {
    return writePair(REG_OUT0, s_out) && writePair(REG_CFG0, s_cfg);
}

bool setBit(uint8_t bit, bool high) {
    std::lock_guard<std::mutex> lk(s_mu);
    if (!s_ready) return false;
    const uint16_t o = s_out, c = s_cfg;
    stage(bit, high);
    if (commit()) return true;
    s_out = o;
    s_cfg = c;
    return false;
}

}  // namespace

bool boardIoBegin() {
    std::lock_guard<std::mutex> lk(s_mu);
    s_ready = false;
    pinMode(EXPANDER_INT, INPUT_PULLUP);
    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL, BOARD_I2C_FREQ);

    if (!readPair(REG_OUT0, s_out) || !readPair(REG_CFG0, s_cfg)) {
        Serial.printf("[board] expander 0x%02X not answering\n", EXPANDER_ADDR);
        return false;
    }

    // Inputs.
    s_cfg |= (1u << EXP_BIT_WAKE_BUTTON) | (1u << EXP_BIT_I2C_INT) | (1u << EXP_BIT_SD_DETECT);

    // Safe state: everything this milestone does not use is off or held.
    stage(EXP_BIT_TOUCH_INT, false);
    stage(EXP_BIT_LCD_CS, true);
    stage(EXP_BIT_LCD_POWER, false);       // TODO(m8): display bring-up
    stage(EXP_BIT_LCD_RST, false);
    stage(EXP_BIT_GROVE_POWER, false);
    stage(EXP_BIT_TOUCH_RST, false);
    stage(EXP_BIT_GNSS_RST, true);         // held in reset ...
    stage(EXP_BIT_GNSS_POWER, false);      // ... and unpowered: GNSS unused
    stage(EXP_BIT_USER_LED, false);
    stage(EXP_BIT_USB_OTG_EN, false);
    stage(EXP_BIT_AUDIO_PA_POWER, false);
    stage(EXP_BIT_SD_POWER, false);
    stage(EXP_BIT_BATT_SENSE_EN, false);

    if (!commit()) {
        Serial.println("[board] expander safe-state write failed");
        return false;
    }
    s_ready = true;
    Serial.printf("[board] expander ready out=0x%04X cfg=0x%04X\n", s_out, s_cfg);
    return true;
}

bool boardIoReady() { return s_ready; }

std::mutex &boardI2cMutex() { return s_mu; }

void boardI2cScan() {
    std::lock_guard<std::mutex> lk(s_mu);
    String found;
    for (uint8_t a = 1; a < 0x7F; ++a) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) found += String(" 0x") + String(a, HEX);
    }
    Serial.printf("[board] I2C devices:%s\n", found.length() ? found.c_str() : " none");
}

bool boardSetStatusLed(bool on) { return setBit(EXP_BIT_USER_LED, on); }
bool boardSetAudioPaPower(bool on) { return setBit(EXP_BIT_AUDIO_PA_POWER, on); }
bool boardSetUsbOtgPower(bool on) { return setBit(EXP_BIT_USB_OTG_EN, on); }

bool boardSetSdPower(bool on) { return setBit(EXP_BIT_SD_POWER, on); }

namespace {
bool readInputBit(uint8_t bit, bool &low) {
    std::lock_guard<std::mutex> lk(s_mu);
    low = false;
    if (!s_ready) return false;
    uint16_t in = 0xFFFF;
    if (!readPair(REG_IN0, in)) return false;
    low = (in & (1u << bit)) == 0;
    return true;
}
}  // namespace

bool boardReadWakeButton(bool &pressed) { return readInputBit(EXP_BIT_WAKE_BUTTON, pressed); }

// TODO(hw): card-detect polarity is unconfirmed (camillia-mt only configures
// the bit as an input). Assumed active-low, the usual switch wiring; the SD
// module mounts regardless and only uses this for the status line.
bool boardReadSdDetect(bool &present) { return readInputBit(EXP_BIT_SD_DETECT, present); }

#endif
