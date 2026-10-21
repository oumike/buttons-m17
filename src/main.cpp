// buttons-m17 — internet-only M17 voice client.
// Task startup only; every subsystem lives in its own module.

#include <Arduino.h>

#include "app/status.h"
#include "audio/audio_engine.h"
#include "board/board_io.h"
#include "board_config.h"
#include "config/settings_store.h"
#include "net/wifi_manager.h"
#include "storage/sd_storage.h"
#include "ui/leds.h"
#include "ui/web_server.h"

#ifndef APP_VERSION
#define APP_VERSION "unknown"
#endif

namespace {

// Hold the Wake button for FACTORY_RESET_HOLD_MS while the board boots to
// wipe all settings (Wi-Fi, admin password, callsign...). The Wake button is
// used rather than the PTT on GPIO0 because GPIO0 held through reset enters
// the ROM bootloader.
void checkFactoryReset() {
    bool pressed = false;
    if (!boardReadWakeButton(pressed) || !pressed) return;
    Serial.println("[boot] Wake held: keep holding to factory-reset settings");
    const uint32_t start = millis();
    while (millis() - start < FACTORY_RESET_HOLD_MS) {
        boardSetStatusLed(((millis() - start) / 100) % 2);
        delay(50);
        if (!boardReadWakeButton(pressed) || !pressed) {
            boardSetStatusLed(false);
            Serial.println("[boot] released; factory reset cancelled");
            return;
        }
    }
    settingsFactoryReset();
    Serial.println("[boot] settings wiped; restarting");
    delay(200);
    ESP.restart();
}

void netTask(void *) {
    wifiBegin();
    webBegin();
    for (;;) {
        wifiLoop();
        webLoop();
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.printf("\n[boot] buttons-m17 %s on %s\n", APP_VERSION, BOARD_NAME);

    const bool ioOk = boardIoBegin();
    statusUpdate([&](AppStatus &s) { s.expanderOk = ioOk; });
    if (ioOk) {
        boardI2cScan();
        checkFactoryReset();
    }

    settingsBegin();
    const cfg::Settings s = settingsGet();
    if (!cfg::canTransmit(s))
        Serial.println("[boot] no callsign configured: transmit inhibited");

    storageBegin();
    audioBegin();

    // Wi-Fi + web on core 0 next to the Wi-Fi stack; audio (and Codec2 from
    // milestone 4) on core 1.
    xTaskCreatePinnedToCore(netTask, "net", 8192, nullptr, 3, nullptr, 0);
    xTaskCreatePinnedToCore(audioTask, "audio", 8192, nullptr, 10, nullptr, 1);
    xTaskCreatePinnedToCore(ledsTask, "leds", 2048, nullptr, 1, nullptr, 1);
}

void loop() { vTaskDelete(nullptr); }
