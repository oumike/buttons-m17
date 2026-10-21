#include "audio/dsp.h"

#include <cmath>
#include <cstdlib>

namespace audio {

namespace {
constexpr float PI_F = 3.14159265358979f;
constexpr float CUTOFF_HZ = 3600.0f;

struct Coeffs {
    float h[LowPass16k::TAPS];
    Coeffs() {
        const float fc = CUTOFF_HZ / BUS_RATE;
        const int m = static_cast<int>(LowPass16k::TAPS) - 1;
        float sum = 0.0f;
        for (int n = 0; n <= m; ++n) {
            const float x = n - m / 2.0f;
            const float sinc = (x == 0.0f) ? 2.0f * fc : std::sin(2.0f * PI_F * fc * x) / (PI_F * x);
            const float w = 0.42f - 0.5f * std::cos(2.0f * PI_F * n / m) +
                            0.08f * std::cos(4.0f * PI_F * n / m);
            h[n] = sinc * w;
            sum += h[n];
        }
        for (float &v : h) v /= sum;   // exact unity DC gain
    }
};
}  // namespace

int16_t sat16(int32_t v) {
    if (v > INT16_MAX) return INT16_MAX;
    if (v < INT16_MIN) return INT16_MIN;
    return static_cast<int16_t>(v);
}

int16_t sat16f(float v) { return sat16(static_cast<int32_t>(std::lround(v))); }

const float *LowPass16k::coefficients() {
    static const Coeffs c;
    return c.h;
}

void LowPass16k::reset() {
    for (float &v : hist_) v = 0.0f;
    pos_ = 0;
}

void LowPass16k::push(float x) {
    hist_[pos_] = x;
    hist_[pos_ + TAPS] = x;
    pos_ = (pos_ + 1) % TAPS;
}

float LowPass16k::output() const {
    // hist_[pos_ .. pos_+TAPS) is oldest..newest. The filter is symmetric, so
    // the coefficient order does not matter.
    const float *h = coefficients();
    const float *x = hist_ + pos_;
    float acc = 0.0f;
    for (size_t i = 0; i < TAPS; ++i) acc += h[i] * x[i];
    return acc;
}

void Decimator2::process(const int16_t *in, size_t nOut, int16_t *out) {
    for (size_t i = 0; i < nOut; ++i) {
        lp_.push(in[2 * i]);
        lp_.push(in[2 * i + 1]);
        out[i] = sat16f(lp_.output());
    }
}

void Interpolator2::process(const int16_t *in, size_t nIn, int16_t *out) {
    // Zero-stuffing halves the energy per sample; gain 2 restores the level.
    for (size_t i = 0; i < nIn; ++i) {
        lp_.push(2.0f * in[i]);
        out[2 * i] = sat16f(lp_.output());
        lp_.push(0.0f);
        out[2 * i + 1] = sat16f(lp_.output());
    }
}

void DcBlocker::process(int16_t *buf, size_t n) {
    constexpr float R = 0.985f;
    for (size_t i = 0; i < n; ++i) {
        const float x = buf[i];
        y1_ = x - x1_ + R * y1_;
        x1_ = x;
        buf[i] = sat16f(y1_);
    }
}

float dbToLinear(float db) { return std::pow(10.0f, db / 20.0f); }

float volumeToLinear(uint8_t percent) {
    if (percent == 0) return 0.0f;
    if (percent > 100) percent = 100;
    return dbToLinear(VOLUME_MAX_DB - 0.3f * (100 - percent));
}

Limiter::Limiter(float thresholdDbfs, float releaseCoef)
    : threshold_(32767.0f * dbToLinear(thresholdDbfs)), release_(releaseCoef) {}

void Limiter::process(int16_t *buf, size_t n, float gain) {
    for (size_t i = 0; i < n; ++i) {
        const float x = buf[i] * gain;
        const float a = std::fabs(x);
        env_ = a > env_ ? a : env_ * release_;
        buf[i] = sat16f(env_ > threshold_ ? x * (threshold_ / env_) : x);
    }
}

void applyGain(int16_t *buf, size_t n, float gain) {
    if (gain == 1.0f) return;
    for (size_t i = 0; i < n; ++i) buf[i] = sat16f(buf[i] * gain);
}

float peakDbfs(const int16_t *buf, size_t n) {
    int32_t peak = 0;
    for (size_t i = 0; i < n; ++i) {
        const int32_t a = std::abs(static_cast<int32_t>(buf[i]));
        if (a > peak) peak = a;
    }
    if (peak == 0) return -96.0f;
    return 20.0f * std::log10(peak / 32768.0f);
}

ToneGenerator::ToneGenerator(float hz, int sampleRate, int16_t amplitude)
    : step_(2.0f * PI_F * hz / sampleRate), amp_(amplitude) {}

void ToneGenerator::fill(int16_t *buf, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        buf[i] = sat16f(amp_ * std::sin(phase_));
        phase_ += step_;
        if (phase_ >= 2.0f * PI_F) phase_ -= 2.0f * PI_F;
    }
}

}  // namespace audio
