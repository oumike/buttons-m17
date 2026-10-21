// Native tests for src/audio/dsp and src/app/ptt.

#include <unity.h>

#include <cmath>
#include <vector>

#include "app/ptt.h"
#include "audio/dsp.h"

using namespace audio;

void setUp() {}
void tearDown() {}

namespace {

std::vector<int16_t> tone(float hz, int rate, size_t n, int16_t amp) {
    std::vector<int16_t> v(n);
    ToneGenerator g(hz, rate, amp);
    g.fill(v.data(), n);
    return v;
}

// RMS of v[skip..], skipping the filter's start-up transient.
double rms(const std::vector<int16_t> &v, size_t skip) {
    double acc = 0;
    for (size_t i = skip; i < v.size(); ++i) acc += double(v[i]) * v[i];
    return std::sqrt(acc / double(v.size() - skip));
}

std::vector<int16_t> decimate(const std::vector<int16_t> &in) {
    Decimator2 d;
    std::vector<int16_t> out(in.size() / 2);
    // Feed in 20 ms blocks, as the audio task does, to exercise block joins.
    for (size_t i = 0; i + FRAME_16K <= in.size(); i += FRAME_16K)
        d.process(in.data() + i, FRAME_8K, out.data() + i / 2);
    return out;
}

std::vector<int16_t> interpolate(const std::vector<int16_t> &in) {
    Interpolator2 u;
    std::vector<int16_t> out(in.size() * 2);
    for (size_t i = 0; i + FRAME_8K <= in.size(); i += FRAME_8K)
        u.process(in.data() + i, FRAME_8K, out.data() + 2 * i);
    return out;
}

// A full-scale-`amp` sine has RMS amp/sqrt(2); allow `tol` relative error.
void expectToneRms(int amp, double tol, const std::vector<int16_t> &out, size_t skip) {
    const float want = float(amp / std::sqrt(2.0));
    TEST_ASSERT_FLOAT_WITHIN(want * float(tol), want, float(rms(out, skip)));
}

}  // namespace

void test_lowpass_unity_dc_and_symmetric() {
    const float *h = LowPass16k::coefficients();
    double sum = 0;
    for (size_t i = 0; i < LowPass16k::TAPS; ++i) {
        sum += h[i];
        TEST_ASSERT_EQUAL_FLOAT(h[i], h[LowPass16k::TAPS - 1 - i]);
    }
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f, float(sum));
}

void test_decimator_passes_speech_band() {
    for (float hz : {300.0f, 1000.0f, 2500.0f}) {
        const auto out = decimate(tone(hz, BUS_RATE, FRAME_16K * 25, 10000));
        expectToneRms(10000, 0.03, out, 100);
    }
}

void test_decimator_rejects_alias() {
    // 6 kHz at 16 kHz would alias to 2 kHz after 2:1; must be >= 50 dB down.
    const auto out = decimate(tone(6000, BUS_RATE, FRAME_16K * 25, 20000));
    TEST_ASSERT_LESS_THAN(20000 / std::sqrt(2.0) * 0.00316, rms(out, 100));
}

void test_interpolator_level_and_image() {
    const auto out = interpolate(tone(1000, CODEC_RATE, FRAME_8K * 25, 10000));
    expectToneRms(10000, 0.03, out, 200);

    // The 7 kHz image of a 1 kHz tone is what zero-stuffing leaves behind.
    // Correlate against it directly (single-bin DFT).
    double re = 0, im = 0;
    for (size_t n = 200; n < out.size(); ++n) {
        const double w = 2 * M_PI * 7000.0 * n / BUS_RATE;
        re += out[n] * std::cos(w);
        im += out[n] * std::sin(w);
    }
    const double amp = 2 * std::sqrt(re * re + im * im) / double(out.size() - 200);
    TEST_ASSERT_LESS_THAN(10000 * 0.00316, amp);
}

void test_round_trip_keeps_level() {
    const auto mid = decimate(tone(800, BUS_RATE, FRAME_16K * 25, 12000));
    const auto out = interpolate(mid);
    expectToneRms(12000, 0.05, out, 300);
}

void test_gain_saturates() {
    int16_t b[3] = {20000, -20000, 100};
    applyGain(b, 3, 2.0f);
    TEST_ASSERT_EQUAL_INT16(32767, b[0]);
    TEST_ASSERT_EQUAL_INT16(-32768, b[1]);
    TEST_ASSERT_EQUAL_INT16(200, b[2]);
}

void test_volume_curve() {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, volumeToLinear(0));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, dbToLinear(6.0f), volumeToLinear(100));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, dbToLinear(-3.0f), volumeToLinear(70));
    for (int p = 1; p < 100; ++p) TEST_ASSERT_TRUE(volumeToLinear(p) < volumeToLinear(p + 1));
}

void test_limiter() {
    // +6 dB on a near-full-scale tone stays under the -1 dBFS ceiling ...
    auto loud = tone(1000, BUS_RATE, FRAME_16K * 10, 30000);
    Limiter lim;
    lim.process(loud.data(), loud.size(), 2.0f);
    TEST_ASSERT_TRUE(peakDbfs(loud.data(), loud.size()) <= -0.99f);
    // ... while a quiet signal just gets the gain.
    auto quiet = tone(1000, BUS_RATE, FRAME_16K * 10, 2000);
    Limiter lim2;
    lim2.process(quiet.data(), quiet.size(), 2.0f);
    expectToneRms(4000, 0.01, quiet, 0);
}

void test_peak_dbfs() {
    int16_t z[4] = {};
    TEST_ASSERT_EQUAL_FLOAT(-96.0f, peakDbfs(z, 4));
    int16_t f[2] = {0, -32768};
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, peakDbfs(f, 2));
    int16_t h[1] = {16384};
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -6.02f, peakDbfs(h, 1));
}

void test_dc_blocker() {
    DcBlocker b;
    std::vector<int16_t> v(FRAME_8K * 10, 5000);
    b.process(v.data(), v.size());
    TEST_ASSERT_INT_WITHIN(5, 0, v.back());
    // A 1 kHz tone passes essentially unchanged.
    auto t = tone(1000, CODEC_RATE, FRAME_8K * 10, 10000);
    b.process(t.data(), t.size());
    expectToneRms(10000, 0.02, t, FRAME_8K);
}

void test_ptt_debounce_momentary() {
    PttLogic p(25);
    TEST_ASSERT_FALSE(p.update(true, 0));
    TEST_ASSERT_FALSE(p.update(false, 10));   // bounce
    TEST_ASSERT_FALSE(p.update(true, 12));
    TEST_ASSERT_FALSE(p.update(true, 36));    // 24 ms stable: not yet
    TEST_ASSERT_TRUE(p.update(true, 37));
    TEST_ASSERT_TRUE(p.update(false, 100));   // release bounces are held
    TEST_ASSERT_TRUE(p.update(true, 105));
    TEST_ASSERT_TRUE(p.update(false, 110));
    TEST_ASSERT_FALSE(p.update(false, 135));
}

void test_ptt_toggle_and_cancel() {
    PttLogic p(10, true);
    p.update(true, 0);
    TEST_ASSERT_TRUE(p.update(true, 10));
    TEST_ASSERT_TRUE(p.update(false, 50));
    TEST_ASSERT_TRUE(p.update(false, 60));    // latched after release
    p.update(true, 100);
    TEST_ASSERT_FALSE(p.update(true, 110));   // second press unlatches

    // Cancel while held must not re-key until released and pressed again.
    PttLogic m(10);
    m.update(true, 0);
    TEST_ASSERT_TRUE(m.update(true, 10));
    m.cancel();
    TEST_ASSERT_FALSE(m.update(true, 500));
    m.update(false, 510);
    TEST_ASSERT_FALSE(m.update(false, 520));
    m.update(true, 530);
    TEST_ASSERT_TRUE(m.update(true, 540));

    // Switching mode unkeys a latched toggle.
    p.update(false, 150);
    p.update(false, 160);
    p.update(true, 200);
    TEST_ASSERT_TRUE(p.update(true, 210));
    p.setToggle(false);
    TEST_ASSERT_FALSE(p.keyed());
    TEST_ASSERT_FALSE(p.update(true, 300));   // still held: needs a fresh press
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_lowpass_unity_dc_and_symmetric);
    RUN_TEST(test_decimator_passes_speech_band);
    RUN_TEST(test_decimator_rejects_alias);
    RUN_TEST(test_interpolator_level_and_image);
    RUN_TEST(test_round_trip_keeps_level);
    RUN_TEST(test_gain_saturates);
    RUN_TEST(test_volume_curve);
    RUN_TEST(test_limiter);
    RUN_TEST(test_peak_dbfs);
    RUN_TEST(test_dc_blocker);
    RUN_TEST(test_ptt_debounce_momentary);
    RUN_TEST(test_ptt_toggle_and_cancel);
    return UNITY_END();
}
