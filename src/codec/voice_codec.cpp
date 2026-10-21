#include "codec/voice_codec.h"

#include <codec2.h>
#include <esp_timer.h>

#include <algorithm>

namespace voice {

Codec2Voice::~Codec2Voice() {
    if (c2_) codec2_destroy(c2_);
}

bool Codec2Voice::begin() {
    if (!c2_) c2_ = codec2_create(CODEC2_MODE_3200);
    if (!c2_) return false;
    return codec2_samples_per_frame(c2_) == static_cast<int>(SAMPLES_PER_FRAME) &&
           codec2_bytes_per_frame(c2_) == static_cast<int>(BYTES_PER_FRAME);
}

void Codec2Voice::encode(const int16_t *pcm, uint8_t *bits) {
    const int64_t t0 = esp_timer_get_time();
    // codec2_encode() takes a non-const pointer but does not write to it.
    codec2_encode(c2_, bits, const_cast<short *>(pcm));
    const uint32_t us = static_cast<uint32_t>(esp_timer_get_time() - t0);
    encMax_ = std::max(encMax_, us);
    encSum_ += us;
    ++encN_;
}

void Codec2Voice::decode(const uint8_t *bits, int16_t *pcm) {
    const int64_t t0 = esp_timer_get_time();
    codec2_decode(c2_, pcm, bits);
    const uint32_t us = static_cast<uint32_t>(esp_timer_get_time() - t0);
    decMax_ = std::max(decMax_, us);
    decSum_ += us;
    ++decN_;
}

}  // namespace voice
