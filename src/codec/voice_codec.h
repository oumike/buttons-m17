#pragma once
// Codec2 3200 wrapper: 160 samples (20 ms at 8 kHz) <-> 8 bytes. M17 voice
// streams carry two of these frames per 40 ms packet (m17::PAYLOAD_LEN).
//
// One instance holds one codec state; Codec2 states are stateful, so the
// encoder and decoder each get their own. Not thread-safe per instance.

#include <cstddef>
#include <cstdint>

struct CODEC2;   // codec2.h

namespace voice {

constexpr size_t SAMPLES_PER_FRAME = 160;
constexpr size_t BYTES_PER_FRAME = 8;

class Codec2Voice {
public:
    Codec2Voice() = default;
    ~Codec2Voice();
    Codec2Voice(const Codec2Voice &) = delete;
    Codec2Voice &operator=(const Codec2Voice &) = delete;

    bool begin();   // false if allocation failed or the frame sizes differ
    void encode(const int16_t *pcm, uint8_t *bits);
    void decode(const uint8_t *bits, int16_t *pcm);

    // Worst and running-average call times in microseconds.
    uint32_t encodeMaxUs() const { return encMax_; }
    uint32_t decodeMaxUs() const { return decMax_; }
    uint32_t encodeAvgUs() const { return encN_ ? encSum_ / encN_ : 0; }
    uint32_t decodeAvgUs() const { return decN_ ? decSum_ / decN_ : 0; }

private:
    ::CODEC2 *c2_ = nullptr;
    uint32_t encMax_ = 0, decMax_ = 0;
    uint64_t encSum_ = 0, decSum_ = 0;
    uint32_t encN_ = 0, decN_ = 0;
};

}  // namespace voice
