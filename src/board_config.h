#pragma once
// ════════════════════════════════════════════════════════════════════════════
// board_config.h — every pin, bus and baud rate in the firmware lives here.
//
// One block per board, selected by the DEVICE_* flag its PlatformIO env sets.
// Nothing outside this file may name a GPIO number.
// ════════════════════════════════════════════════════════════════════════════

#if defined(DEVICE_WIO_TRACKER_L2)
// ── Seeed Wio Tracker L2 ────────────────────────────────────────────────────
// ESP32-S3, 16 MB flash, 8 MB octal PSRAM. Values transcribed from
// camillia-mt src/hal/hw_wio_tracker_l2.h (itself from Seeed's vendor variant),
// not probed. Differences from the generic parts list in the build prompt:
//
//   * Mic and speaker are an on-board ES7243E ADC + ES8311 DAC/PA sharing ONE
//     I2S bus (full duplex), not an INMP441 + MAX98357A on two ports.
//   * There are no discrete status LEDs: one user LED on the I/O expander.
//     Wi-Fi / linked / TX / RX are shown on the TFT instead (milestone 7).
//   * The display is an NV3031B 240x320 QSPI TFT, not an SSD1306.
//   * A PCA9555 I/O expander gates almost every rail. Bring-up order is
//     I2C -> expander -> everything else.
#define BOARD_NAME               "Seeed Wio Tracker L2"

// Shared I2C bus: expander, touch, backlight, battery ADC, both audio codecs.
#define BOARD_I2C_SDA             47
#define BOARD_I2C_SCL             48
#define BOARD_I2C_FREQ        100000

// PCA9555 I/O expander.
#define EXPANDER_ADDR           0x21
#define EXPANDER_INT              45   // active-low, shared
#define EXP_BIT_WAKE_BUTTON        0   // in, active-low
#define EXP_BIT_I2C_INT            1   // in
#define EXP_BIT_SD_DETECT          2   // in
#define EXP_BIT_TOUCH_INT          3
#define EXP_BIT_LCD_CS             4
#define EXP_BIT_LCD_POWER          5
#define EXP_BIT_LCD_RST            6
#define EXP_BIT_GROVE_POWER        7
#define EXP_BIT_TOUCH_RST          8
#define EXP_BIT_GNSS_RST           9   // active-high reset
#define EXP_BIT_USER_LED          10
#define EXP_BIT_USB_OTG_EN        11
#define EXP_BIT_AUDIO_PA_POWER    12
#define EXP_BIT_GNSS_POWER        13
#define EXP_BIT_SD_POWER          14
#define EXP_BIT_BATT_SENSE_EN     15

// PTT: GPIO0 is the only button wired straight to the MCU (it is also the
// boot strap, so it must not be held during reset unless you want the ROM
// bootloader). Debounced in software. The top Wake button (expander bit 0)
// is the alternative if GPIO0 proves awkward.
#define PTT_PIN                    0
#define PTT_ACTIVE_LOW             1
#define PTT_DEBOUNCE_MS           25

// Hold the Wake button (expander bit 0) this long during boot to wipe
// settings. Not PTT: GPIO0 held through reset enters the ROM bootloader.
#define FACTORY_RESET_HOLD_MS   5000

// Audio — ES8311 DAC/PA + ES7243E mic ADC on one I2S bus.
#define I2S_MCLK                  10
#define I2S_BCK                   11
#define I2S_WS                    12
#define I2S_DOUT                  16   // to ES8311
#define I2S_DIN                   15   // from ES7243E
#define AUDIO_ES8311_ADDR       0x18
// From TactilityProject/Tactility's Wio Tracker L2 device tree (camillia-mt
// never opens the mic); also confirmed by the boot-time I2C scan log.
#define AUDIO_ES7243E_ADDR      0x14
// Both codecs run as I2S slaves at 16 kHz with MCLK = 256 * fs (4.096 MHz),
// the ES7243E's native rate; dsp.h resamples to Codec2's 8 kHz.
#define AUDIO_BUS_RATE         16000
#define AUDIO_AMP_SETTLE_MS      250

// microSD — 1-bit SD_MMC on dedicated pins; power is expander bit 14 and
// card-detect expander bit 2. Used for settings backups (and later, fixtures
// and logs).
#define HAS_SD_MMC                 1
#define SDMMC_CLK                  2
#define SDMMC_CMD                  3
#define SDMMC_D0                   1

// GNSS (L76K-class) on UART1. Not used by the M17 client yet, so it is
// held in reset and unpowered by boardIoBegin(). Candidate later uses: clock
// for last-heard timestamps, and D-PRS position in slow data.
#define GPS_UART_NUM               1
#define GPS_RX                    18
#define GPS_TX                    17
#define GPS_BAUD                9600

// Display — NV3031B 240x320 on QSPI, LP5814 I2C backlight, GT911 touch
// (milestone 7). Pins kept here so the display module never names a GPIO.
#define TFT_QSPI_SCK              42
#define TFT_QSPI_IO0              41
#define TFT_QSPI_IO1              40
#define TFT_QSPI_IO2              39
#define TFT_QSPI_IO3              38
#define TFT_CS                    46
#define LP5814_ADDR             0x2c
#define TOUCH_GT911_ADDR        0x5D

#define HAS_EXPANDER               1
#define HAS_STATUS_LEDS            0   // only the single expander user LED

#else
#error "No board selected: set -DDEVICE_<BOARD>=1 in the PlatformIO env."
#endif
