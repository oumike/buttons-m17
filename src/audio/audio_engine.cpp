#include "audio/audio_engine.h"

#include <Arduino.h>
#include <driver/i2s.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <atomic>
#include <cstring>

#include "app/ptt.h"
#include "app/status.h"
#include "audio/codecs.h"
#include "audio/dsp.h"
#include "board/board_io.h"
#include "board_config.h"
#include "config/settings_store.h"

namespace {

using namespace audio;

constexpr i2s_port_t PORT = I2S_NUM_0;
constexpr uint32_t ECHO_MAX_MS = 10000;
constexpr size_t ECHO_MAX_SAMPLES = CODEC_RATE * ECHO_MAX_MS / 1000;
constexpr uint32_t ECHO_MIN_MS = 300;        // shorter is a stray tap: ignore
constexpr uint32_t TONE_MS = 2000;
constexpr uint32_t PA_IDLE_OFF_MS = 2000;
constexpr uint32_t BLOCK_MS = 20;

enum class Mode { Idle, Recording, Playback, Tone };

bool s_ok = false;
std::atomic<bool> s_toneRequest{false};

void setAudioStatus(const std::string &s) {
    statusUpdate([&](AppStatus &st) { st.audio = s; });
}

bool i2sStart() {
    i2s_config_t c;
    memset(&c, 0, sizeof c);
    c.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX);
    c.sample_rate = AUDIO_BUS_RATE;
    c.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    c.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    c.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    c.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    c.dma_buf_count = 6;
    c.dma_buf_len = FRAME_8K;          // frames per DMA buffer: 10 ms at 16 kHz
    c.use_apll = false;
    c.tx_desc_auto_clear = true;       // underrun plays silence, not a loop
    c.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    c.bits_per_chan = I2S_BITS_PER_CHAN_DEFAULT;

    i2s_pin_config_t p;
    memset(&p, 0, sizeof p);
    p.mck_io_num = I2S_MCLK;
    p.bck_io_num = I2S_BCK;
    p.ws_io_num = I2S_WS;
    p.data_out_num = I2S_DOUT;
    p.data_in_num = I2S_DIN;

    if (i2s_driver_install(PORT, &c, 0, nullptr) != ESP_OK) return false;
    if (i2s_set_pin(PORT, &p) != ESP_OK) return false;
    i2s_zero_dma_buffer(PORT);
    return true;
}

}  // namespace

bool audioBegin() {
    if (!i2sStart()) {
        Serial.println("[audio] I2S driver install failed");
        setAudioStatus("I2S FAILED");
        return false;
    }
    delay(20);   // let MCLK run before the codecs' state machines start

    std::string st;
    const bool dac = es8311Init(AUDIO_ES8311_ADDR);
    const bool adc = es7243eInit(AUDIO_ES7243E_ADDR, settingsGet().micGainDb);
    Serial.printf("[audio] ES8311 @0x%02X %s, ES7243E @0x%02X %s, I2S %d Hz\n",
                  AUDIO_ES8311_ADDR, dac ? "ok" : "FAILED", AUDIO_ES7243E_ADDR,
                  adc ? "ok" : "FAILED", AUDIO_BUS_RATE);
    if (dac && adc) st = "ok";
    else st = std::string(dac ? "" : "ES8311 (speaker) FAILED ") + (adc ? "" : "ES7243E (mic) FAILED");
    setAudioStatus(st);

    pinMode(PTT_PIN, INPUT_PULLUP);
    s_ok = dac || adc;   // run the loop if either half works, for diagnosis
    return dac && adc;
}

void audioRequestTone() { s_toneRequest = true; }

void audioTask(void *) {
    if (!s_ok) vTaskDelete(nullptr);

    auto *echo = static_cast<int16_t *>(
        heap_caps_malloc(ECHO_MAX_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM));
    if (!echo) Serial.println("[audio] no PSRAM for echo buffer; echo test disabled");

    static int16_t rx[FRAME_16K * 2], tx[FRAME_16K * 2];
    int16_t slot[2][FRAME_16K], mic8[FRAME_8K], out8[FRAME_8K], out16[FRAME_16K];

    Decimator2 dec;
    Interpolator2 interp;
    DcBlocker dcBlock;
    ToneGenerator tone(1000.0f, CODEC_RATE, 16384);   // -6 dBFS
    Limiter limiter;
    PttLogic ptt(PTT_DEBOUNCE_MS);

    Mode mode = Mode::Idle;
    size_t echoLen = 0, echoPos = 0;
    uint32_t modeBlocks = 0;
    float energy[2] = {0, 0};
    float peakDb[2] = {-96, -96};
    int micSlot = 0;
    float micGain = 1.0f, volume = 1.0f;
    int appliedMicDb = -1;
    bool paOn = false;
    uint32_t paSettleBlocks = 0, idleBlocks = 0, errors = 0;

    for (uint32_t block = 0;; ++block) {
        if (block % 50 == 0) {   // once a second: pick up settings changes
            const cfg::Settings s = settingsGet();
            if (s.micGainDb != appliedMicDb) {
                // Hardware PGA first (better noise), the remainder digitally.
                float hw = 0.0f;
                es7243eSetGain(AUDIO_ES7243E_ADDR, s.micGainDb, &hw);
                micGain = dbToLinear(s.micGainDb - hw);
                appliedMicDb = s.micGainDb;
                Serial.printf("[audio] mic gain %d dB (PGA %.1f + digital %.1f)\n",
                              s.micGainDb, hw, s.micGainDb - hw);
            }
            volume = volumeToLinear(s.volume);
            ptt.setToggle(s.pttMode == cfg::PttMode::Toggle);
        }

        size_t got = 0;
        if (i2s_read(PORT, rx, sizeof rx, &got, pdMS_TO_TICKS(100)) != ESP_OK || got != sizeof rx) {
            ++errors;
            memset(rx, 0, sizeof rx);
        }

        // De-interleave and find the mic. Which I2S slot the ES7243E puts
        // the mic on is not documented, so track both and use the livelier.
        for (size_t i = 0; i < FRAME_16K; ++i) {
            slot[0][i] = rx[2 * i];
            slot[1][i] = rx[2 * i + 1];
        }
        for (int c = 0; c < 2; ++c) {
            float e = 0;
            for (size_t i = 0; i < FRAME_16K; i += 4) e += float(slot[c][i]) * slot[c][i];
            energy[c] = 0.95f * energy[c] + 0.05f * e;
            const float p = peakDbfs(slot[c], FRAME_16K);
            peakDb[c] = (block % 10 == 0) ? p : std::max(peakDb[c], p);
        }
        if (mode != Mode::Recording) micSlot = energy[1] > energy[0] ? 1 : 0;

        dec.process(slot[micSlot], FRAME_8K, mic8);
        dcBlock.process(mic8, FRAME_8K);
        applyGain(mic8, FRAME_8K, micGain);

        const bool raw = digitalRead(PTT_PIN) == (PTT_ACTIVE_LOW ? LOW : HIGH);
        const bool keyed = ptt.update(raw, millis());

        // ── mode transitions ──
        ++modeBlocks;
        if (mode == Mode::Idle) {
            if (keyed && echo) {
                mode = Mode::Recording;
                modeBlocks = 0;
                echoLen = 0;
                Serial.printf("[audio] echo: recording (mic slot %d)\n", micSlot);
            } else if (s_toneRequest.exchange(false)) {
                mode = Mode::Tone;
                modeBlocks = 0;
                Serial.println("[audio] test tone 1 kHz");
            }
        } else if (mode == Mode::Recording) {
            s_toneRequest = false;   // never tone over a recording
            if (echoLen + FRAME_8K <= ECHO_MAX_SAMPLES) {
                memcpy(echo + echoLen, mic8, sizeof mic8);
                echoLen += FRAME_8K;
            }
            if (!keyed) {
                const uint32_t ms = echoLen * 1000 / CODEC_RATE;
                Serial.printf("[audio] echo: %u ms recorded, peak %.1f dBFS\n",
                              static_cast<unsigned>(ms), peakDbfs(echo, echoLen));
                mode = ms >= ECHO_MIN_MS ? Mode::Playback : Mode::Idle;
                modeBlocks = 0;
                echoPos = 0;
            }
        }

        // ── speaker amp power: hold output silent until it has settled ──
        const bool sound = mode == Mode::Playback || mode == Mode::Tone;
        if (sound) {
            idleBlocks = 0;
            if (!paOn) {
                if (!boardSetAudioPaPower(true)) Serial.println("[audio] PA power on FAILED");
                paOn = true;
                paSettleBlocks = AUDIO_AMP_SETTLE_MS / BLOCK_MS;
            }
        } else if (paOn && ++idleBlocks * BLOCK_MS >= PA_IDLE_OFF_MS) {
            boardSetAudioPaPower(false);
            paOn = false;
        }

        // ── output content ──
        memset(out8, 0, sizeof out8);
        if (paSettleBlocks) {
            --paSettleBlocks;
        } else if (mode == Mode::Playback) {
            memcpy(out8, echo + echoPos, sizeof out8);
            echoPos += FRAME_8K;
            if (echoPos >= echoLen) {
                Serial.println("[audio] echo: done");
                mode = Mode::Idle;
            }
        } else if (mode == Mode::Tone) {
            tone.fill(out8, FRAME_8K);
            if (modeBlocks * BLOCK_MS >= TONE_MS + AUDIO_AMP_SETTLE_MS) mode = Mode::Idle;
        }

        interp.process(out8, FRAME_8K, out16);
        limiter.process(out16, FRAME_16K, volume);
        for (size_t i = 0; i < FRAME_16K; ++i) tx[2 * i] = tx[2 * i + 1] = out16[i];
        size_t wrote = 0;
        i2s_write(PORT, tx, sizeof tx, &wrote, pdMS_TO_TICKS(100));
        if (wrote != sizeof tx) ++errors;

        if (block % 10 == 9) {
            statusUpdate([&](AppStatus &st) {
                st.micPeakDb[0] = static_cast<int>(peakDb[0]);
                st.micPeakDb[1] = static_cast<int>(peakDb[1]);
                st.micSlot = micSlot;
                st.i2sErrors = errors;
            });
        }
    }
}
