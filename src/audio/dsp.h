#pragma once
// Audio DSP helpers: 16 kHz <-> 8 kHz resampling, gain, levels, test tone.
// Pure C++ with no hardware calls, so it builds and tests on the host.
//
// The Wio's ES7243E/ES8311 pair runs natively at 16 kHz (MCLK = 256 * fs),
// while Codec2 3200 wants 8 kHz, 160 samples per 20 ms frame. Rather than
// running the codecs at 8 kHz, where the ES7243E's filters are untested, the
// I2S bus runs at 16 kHz and these filters convert 2:1 either way.

#include <cstddef>
#include <cstdint>

namespace audio {

constexpr int CODEC_RATE = 8000;
constexpr int BUS_RATE = 16000;
constexpr size_t FRAME_8K = 160;    // 20 ms, one Codec2 3200 frame
constexpr size_t FRAME_16K = 320;   // the same 20 ms on the I2S bus

int16_t sat16(int32_t v);
int16_t sat16f(float v);

// Linear-phase FIR low-pass at 16 kHz (windowed sinc, Blackman), cut-off
// 3.6 kHz: flat through the speech band, ≥50 dB down above 4.4 kHz so 2:1
// decimation does not alias. Shared by both resamplers.
class LowPass16k {
public:
    static constexpr size_t TAPS = 63;
    void reset();
    void push(float x);
    float output() const;
    static const float *coefficients();

private:
    float hist_[2 * TAPS] = {};   // doubled so the window is always contiguous
    size_t pos_ = 0;
};

// 16 kHz -> 8 kHz. `in` holds 2*n samples, `out` receives n.
class Decimator2 {
public:
    void reset() { lp_.reset(); }
    void process(const int16_t *in, size_t nOut, int16_t *out);

private:
    LowPass16k lp_;
};

// 8 kHz -> 16 kHz. `in` holds n samples, `out` receives 2*n.
class Interpolator2 {
public:
    void reset() { lp_.reset(); }
    void process(const int16_t *in, size_t nIn, int16_t *out);

private:
    LowPass16k lp_;
};

// One-pole DC blocker (corner ~20 Hz at 8 kHz) for the mic path.
class DcBlocker {
public:
    void process(int16_t *buf, size_t n);

private:
    float x1_ = 0.0f, y1_ = 0.0f;
};

float dbToLinear(float db);

// Speaker volume 0..100 % to a linear factor: 0 mutes, 100 is +6 dB, and
// each step below is 0.3 dB (70 % = -3 dB, 50 % = -9 dB). Values above unity
// rely on the Limiter that follows it in the speaker path.
constexpr float VOLUME_MAX_DB = 6.0f;
float volumeToLinear(uint8_t percent);

// Peak limiter: instant attack, ~50 ms release at 16 kHz. Applies `gain`
// then keeps |output| <= threshold, so turned-up audio compresses instead of
// clipping.
class Limiter {
public:
    explicit Limiter(float thresholdDbfs = -1.0f, float releaseCoef = 0.99875f);
    void process(int16_t *buf, size_t n, float gain);

private:
    float threshold_, release_, env_ = 0.0f;
};

// Multiply in place, saturating at int16 limits.
void applyGain(int16_t *buf, size_t n, float gain);

// Peak level in dBFS (0 = full scale); -96 for silence.
float peakDbfs(const int16_t *buf, size_t n);

class ToneGenerator {
public:
    ToneGenerator(float hz, int sampleRate, int16_t amplitude);
    void fill(int16_t *buf, size_t n);

private:
    float step_, phase_ = 0.0f;
    int16_t amp_;
};

}  // namespace audio
