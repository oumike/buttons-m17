#include "ui/leds.h"

#include <Arduino.h>

#include "app/status.h"
#include "board/board_io.h"

void ledsTask(void *) {
    bool lastOn = false;
    for (uint32_t tick = 0;; ++tick) {   // 100 ms ticks
        bool on = false;
        switch (statusGet().wifi) {
            case WifiState::Connected: on = true; break;
            case WifiState::Connecting: on = (tick % 10) < 5; break;
            case WifiState::Portal:
            case WifiState::PortalFallback: on = (tick % 2) == 0; break;
            case WifiState::Off: on = false; break;
        }
        if (on != lastOn) {
            boardSetStatusLed(on);
            lastOn = on;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
