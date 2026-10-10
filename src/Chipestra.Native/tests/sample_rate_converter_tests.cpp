// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// SampleRateConverter / ResamplingMixerFeed native tests. All input
// is synthetic (reference DC / sine / impulse signals). Expected results are
// derived from the resampling math, not read back from the implementation.

#include "sample_rate_converter_tests.h"

#include "devices/sega/xboard/xboard_machine.h"
#include "devices/sound/msm5205/msm5205_device_adapter.h"
#include "devices/sound/ym2151/ym2151_device_adapter.h"
#include "devices/sound/ym2203/ym2203_device_adapter.h"
#include "mixer.h"
#include "resampling_mixer_feed.h"
#include "sample_rate_converter.h"
#include "soxr_sample_rate_converter.h"
#include "test_support.h"
#include "chipestra_abi.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

std::vector<float> MakeSine(double freqHz, double sampleRateHz, std::size_t n, float amp) {
    std::vector<float> out(n);
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = static_cast<float>(amp * std::sin(2.0 * kPi * freqHz * static_cast<double>(i) / sampleRateHz));
    }
    return out;
}

std::size_t ExpectedOutputCount(std::size_t inCount, std::uint32_t inRate, std::uint32_t outRate) {
    return static_cast<std::size_t>(std::llround(static_cast<double>(inCount) *
                                                 static_cast<double>(outRate) / static_cast<double>(inRate)));
}

float PeakAbs(const std::vector<float>& v) {
    float peak = 0.0f;
    for (float x : v) {
        peak = std::max(peak, std::fabs(x));
    }
    return peak;
}

}  // namespace

void SrcPreservesDcLevel() {
    LinearSampleRateConverter src(62500, 192000);
    std::vector<float> in(1000, 0.5f);
    std::vector<float> out(ExpectedOutputCount(in.size(), 62500, 192000));
    src.Process(in.data(), in.size(), out.data(), out.size());

    bool all_dc = true;
    for (float x : out) {
        all_dc = all_dc && std::fabs(x - 0.5f) < 1e-5f;
    }
    Check(all_dc, "DC: a constant input resamples to the same constant (linear interp preserves DC exactly)");
}

void SrcPreservesSineAmplitude() {
    // 1 kHz sine, well below both Nyquist limits: linear interpolation should
    // barely touch its amplitude.
    const std::vector<float> in = MakeSine(1000.0, 62500.0, 2000, 0.8f);
    std::vector<float> out(ExpectedOutputCount(in.size(), 62500, 192000));

    LinearSampleRateConverter src(62500, 192000);
    src.Process(in.data(), in.size(), out.data(), out.size());

    const float peak = PeakAbs(out);
    // A convex (linear) interpolator can never exceed the input peak, and
    // for this frequency it loses well under 1%.
    Check(peak > 0.792f && peak <= 0.8005f, "gain: a low-frequency sine keeps its amplitude through resampling");
}

void SrcBlockBoundaryIsContinuous() {
    const std::vector<float> full = MakeSine(1500.0, 62500.0, 4000, 0.7f);

    // One-shot reference.
    std::vector<float> ref(ExpectedOutputCount(full.size(), 62500, 192000));
    {
        LinearSampleRateConverter src(62500, 192000);
        src.Process(full.data(), full.size(), ref.data(), ref.size());
    }

    // Same stream fed as two consecutive blocks through one converter.
    const std::size_t half = full.size() / 2;
    const std::size_t out0 = ExpectedOutputCount(half, 62500, 192000);
    const std::size_t out1 = ref.size() - out0;
    std::vector<float> joined(ref.size());
    {
        LinearSampleRateConverter src(62500, 192000);
        src.Process(full.data(), half, joined.data(), out0);
        src.Process(full.data() + half, full.size() - half, joined.data() + out0, out1);
    }

    float max_diff = 0.0f;
    for (std::size_t i = 0; i < ref.size(); ++i) {
        max_diff = std::max(max_diff, std::fabs(joined[i] - ref[i]));
    }
    Check(max_diff < 0.01f, "continuity: splitting the stream into two blocks reproduces the one-shot result");

    // And there is no click exactly at the seam: the step across the join is
    // no bigger than a normal sample-to-sample step of this signal.
    const float seam_step = std::fabs(joined[out0] - joined[out0 - 1]);
    float typical_step = 0.0f;
    for (std::size_t i = 1; i < out0; ++i) {
        typical_step = std::max(typical_step, std::fabs(joined[i] - joined[i - 1]));
    }
    Check(seam_step <= typical_step + 1e-4f, "continuity: no discontinuity at the block boundary");
}

void SrcIsDeterministic() {
    const std::vector<float> in = MakeSine(3000.0, 62500.0, 1500, 0.6f);
    const std::size_t outCount = ExpectedOutputCount(in.size(), 62500, 192000);

    std::vector<float> a(outCount);
    std::vector<float> b(outCount);
    LinearSampleRateConverter src_a(62500, 192000);
    LinearSampleRateConverter src_b(62500, 192000);
    src_a.Process(in.data(), in.size(), a.data(), a.size());
    src_b.Process(in.data(), in.size(), b.data(), b.size());

    Check(a == b, "determinism: two fresh converters produce bit-identical output for the same input");
}

void SrcHandlesNonIntegerRatioAndProducesRequestedCount() {
    struct Ratio {
        std::uint32_t in;
        std::uint32_t out;
    };
    const Ratio ratios[] = {{62500, 192000}, {44100, 192000}, {48000, 192000}, {192000, 48000}};

    bool ok = true;
    for (const Ratio& r : ratios) {
        const std::vector<float> in = MakeSine(1234.0, r.in, 1777, 0.5f);
        const std::size_t want = 999;  // deliberately unrelated to any clean ratio
        std::vector<float> out(want, -123.0f);

        LinearSampleRateConverter src(r.in, r.out);
        src.Process(in.data(), in.size(), out.data(), out.size());

        if (out.size() != want) {
            ok = false;
        }
        for (float x : out) {
            ok = ok && std::isfinite(x) && std::fabs(x) <= 0.51f;
        }
    }
    Check(ok, "non-integer ratio: every rate pair fills exactly the requested count with finite, bounded samples");
}

void SrcAttenuatesImagesOnUpsample() {
    // Unit impulse -> the linear interpolator's response must be compact (a
    // narrow triangle), i.e. no ringing/aliasing energy spread across the
    // block. A compact time response is exactly what suppresses spectral
    // images.
    std::vector<float> in(400, 0.0f);
    in[200] = 1.0f;
    std::vector<float> out(ExpectedOutputCount(in.size(), 62500, 192000));

    LinearSampleRateConverter src(62500, 192000);
    src.Process(in.data(), in.size(), out.data(), out.size());

    const std::size_t centre = ExpectedOutputCount(200, 62500, 192000);
    float energy_near = 0.0f;
    float energy_far = 0.0f;
    for (std::size_t i = 0; i < out.size(); ++i) {
        const std::size_t dist = (i > centre) ? (i - centre) : (centre - i);
        if (dist <= 6) {
            energy_near += std::fabs(out[i]);
        } else {
            energy_far += std::fabs(out[i]);
        }
    }
    Check(PeakAbs(out) <= 1.0f, "alias: upsampled impulse never overshoots the input amplitude");
    Check(energy_far < 0.01f * energy_near,
          "alias: the impulse response is compact - effectively no energy away from the impulse");

    // Downsampling below Nyquist: an above-output-Nyquist tone must be
    // attenuated (linear interp is a weak but real anti-alias filter), never
    // amplified.
    const std::vector<float> fast = MakeSine(40000.0, 192000.0, 4000, 1.0f);
    std::vector<float> slow(ExpectedOutputCount(fast.size(), 192000, 48000));
    LinearSampleRateConverter down(192000, 48000);
    down.Process(fast.data(), fast.size(), slow.data(), slow.size());
    Check(PeakAbs(slow) < 1.0f, "alias: downsampling attenuates (does not amplify) an above-Nyquist tone");
}

// SrcPreservesDcLevel/SrcPreservesSineAmplitude above both use 62500->192000
// (an UPSAMPLE - anti_alias_enabled_ is false there), and
// SrcAttenuatesImagesOnUpsample's own downsample sub-test only asserts "not
// amplified" (PeakAbs < 1.0f), which a uniformly-attenuated signal would also
// satisfy. These two tests catch a DC-gain error or a "flat gain instead of
// frequency-selective rolloff" error in the anti-alias pre-filter, using the
// real Gyruss/Gradius AY8910 ratio (223721Hz->192000Hz,
// LinearSampleRateConverter's header comment on anti_alias_enabled_/
// kNyquistFraction).
void SrcAntiAliasPreservesDcOnDownsample() {
    LinearSampleRateConverter src(223721, 192000);
    std::vector<float> in(2000, 0.5f);
    std::vector<float> out(ExpectedOutputCount(in.size(), 223721, 192000));
    src.Process(in.data(), in.size(), out.data(), out.size());

    // Skip the filter's own settling transient (a handful of samples at
    // this alpha - LinearSampleRateConverter's own anti_alias_alpha_
    // comment) before checking steady state.
    bool all_dc = true;
    for (std::size_t i = 50; i < out.size(); ++i) {
        all_dc = all_dc && std::fabs(out[i] - 0.5f) < 1e-3f;
    }
    Check(all_dc, "anti-alias DC gain: a constant input through a DOWNSAMPLING converter (223721->192000, the "
                  "Gyruss/Gradius AY8910 ratio) still resamples to the same constant - a broken cascade gain would "
                  "show up here as a flat scale error, not just a wrong cutoff shape");
}

void SrcAntiAliasFrequencyResponse() {
    // Real Gyruss/Gradius AY8910 ratio: 223721Hz -> 192000Hz. Cutoff is
    // 0.45*192000 = 86400Hz (LinearSampleRateConverter's own header
    // comment).
    constexpr std::uint32_t kInRate = 223721;
    constexpr std::uint32_t kOutRate = 192000;
    constexpr double kCutoffHz = 0.45 * 192000.0;  // 86400

    // Low frequency, well below cutoff, vs. high frequency near the INPUT's
    // own Nyquist (223721/2 ~= 111860Hz), comfortably above cutoff. Compared
    // RELATIVELY (highPeak meaningfully below lowPeak) rather than against an
    // absolute threshold: this ratio's mild 1.165x downsample puts the 86.4kHz
    // cutoff only a short distance below the 100kHz test tone, so the actual
    // 2-pole response there is a moderate ~0.71x. What matters is that the two
    // peaks are clearly different (a flat reduction at every frequency, low
    // included, must fail).
    const std::vector<float> lowIn = MakeSine(500.0, kInRate, 20000, 1.0f);
    std::vector<float> lowOut(ExpectedOutputCount(lowIn.size(), kInRate, kOutRate));
    {
        LinearSampleRateConverter src(kInRate, kOutRate);
        src.Process(lowIn.data(), lowIn.size(), lowOut.data(), lowOut.size());
    }
    const float lowPeak = PeakAbs(std::vector<float>(lowOut.begin() + 200, lowOut.end()));
    Check(lowPeak > 0.90f,
          "anti-alias frequency response: 500Hz (far below the 86.4kHz cutoff) passes through the "
          "223721->192000 downsample with near-unity gain (>0.90x of the 1.0 input amplitude)");

    const std::vector<float> highIn = MakeSine(100000.0, kInRate, 20000, 1.0f);
    std::vector<float> highOut(ExpectedOutputCount(highIn.size(), kInRate, kOutRate));
    {
        LinearSampleRateConverter src(kInRate, kOutRate);
        src.Process(highIn.data(), highIn.size(), highOut.data(), highOut.size());
    }
    const float highPeak = PeakAbs(std::vector<float>(highOut.begin() + 200, highOut.end()));
    Check(highPeak < 0.80f * lowPeak,
          "anti-alias frequency response: 100kHz (above the 86.4kHz cutoff, near the input's own Nyquist) is "
          "measurably MORE attenuated than the 500Hz case above (<80% of the low-frequency peak) - confirms "
          "frequency-selective rolloff, not a flat gain error that would attenuate both equally");

    // A steeper ratio (8x, cutoff unchanged at 0.45*outputRateHz) makes the
    // "strongly attenuated well above cutoff" claim unambiguous, isolating
    // the filter's own shape from how mild the real AY8910 ratio is (400kHz
    // is well above cutoff, comfortably below the 768kHz input's Nyquist).
    (void)kCutoffHz;
    {
        constexpr std::uint32_t kSteepInRate = 1'536'000;
        const std::vector<float> in = MakeSine(400000.0, kSteepInRate, 40000, 1.0f);
        std::vector<float> out(ExpectedOutputCount(in.size(), kSteepInRate, kOutRate));
        LinearSampleRateConverter src(kSteepInRate, kOutRate);
        src.Process(in.data(), in.size(), out.data(), out.size());
        const float peak = PeakAbs(std::vector<float>(out.begin() + 400, out.end()));
        Check(peak < 0.25f,
              "anti-alias frequency response: an 8x downsample (1536000->192000) strongly attenuates a 400kHz "
              "tone (well above the 86.4kHz cutoff) to <0.25x - confirms the filter has real stopband rejection, "
              "not just a mild rolloff");
    }
}

// libsoxr-based converter: same-shaped coverage as the Linear*/anti-alias
// tests above. SoxrSrcIsDeterministic's per-sample assertions are collapsed
// into one aggregate Check(), as SrcIsDeterministic above does, rather than
// one output line per sample.
void SoxrSrcPreservesDcLevel() {
    constexpr std::uint32_t kInRate = 62500;
    constexpr std::uint32_t kOutRate = 192000;

    const std::vector<float> in(20000, 0.5f);
    std::vector<float> out(ExpectedOutputCount(in.size(), kInRate, kOutRate));
    {
        SoxrSampleRateConverter src(kInRate, kOutRate);
        src.Process(in.data(), in.size(), out.data(), out.size());
    }

    // Averaged over the steady region (skipping soxr's own startup filter
    // delay) rather than an exact per-sample match: soxr's sinc-based filter
    // has a small amount of ripple settling in, unlike
    // LinearSampleRateConverter's exact DC preservation. 20000 input samples
    // (not the 1000 the Linear sibling test uses), since a short single call
    // can legitimately end short of a full steady-state fill (see
    // SoxrSampleRateConverter::Process()).
    const float dcOut = std::accumulate(out.begin() + 500, out.end(), 0.0f) / static_cast<float>(out.size() - 500);
    Check(std::fabs(dcOut - 0.5f) < 0.01f,
          "soxr DC gain: a constant input (62500->192000, upsample) resamples to the same constant on average");
}

void SoxrSrcAntiAliasFrequencyResponse() {
    // Real Gyruss/Gradius AY8910 ratio - directly comparable to
    // SrcAntiAliasFrequencyResponse's own measurement of the same ratio
    // with LinearSampleRateConverter.
    constexpr std::uint32_t kInRate = 223721;
    constexpr std::uint32_t kOutRate = 192000;

    const std::vector<float> lowIn = MakeSine(500.0, kInRate, 20000, 1.0f);
    std::vector<float> lowOut(ExpectedOutputCount(lowIn.size(), kInRate, kOutRate));
    {
        SoxrSampleRateConverter src(kInRate, kOutRate);
        src.Process(lowIn.data(), lowIn.size(), lowOut.data(), lowOut.size());
    }
    const float lowPeak = PeakAbs(std::vector<float>(lowOut.begin() + 200, lowOut.end()));
    Check(lowPeak > 0.90f,
          "soxr anti-alias frequency response: 500Hz (far below the Nyquist-adjacent stopband) passes through the "
          "223721->192000 downsample with near-unity gain (>0.90x)");

    const std::vector<float> highIn = MakeSine(100000.0, kInRate, 20000, 1.0f);
    std::vector<float> highOut(ExpectedOutputCount(highIn.size(), kInRate, kOutRate));
    {
        SoxrSampleRateConverter src(kInRate, kOutRate);
        src.Process(highIn.data(), highIn.size(), highOut.data(), highOut.size());
    }
    const float highPeak = PeakAbs(std::vector<float>(highOut.begin() + 200, highOut.end()));
    Check(highPeak < 0.30f * lowPeak,
          "soxr anti-alias frequency response: 100kHz (near the input's own Nyquist) is strongly attenuated "
          "relative to the 500Hz case (<0.30x) - a steeper, more Sinc-like rolloff than "
          "LinearSampleRateConverter's own 2-pole IIR (which only reached <0.80x on the same comparison)");
}

void SoxrSrcIsDeterministic() {
    constexpr std::uint32_t kInRate = 62500;
    constexpr std::uint32_t kOutRate = 192000;

    const std::vector<float> in = MakeSine(3000.0, kInRate, 1500, 1.0f);
    std::vector<float> out1(ExpectedOutputCount(in.size(), kInRate, kOutRate));
    std::vector<float> out2(ExpectedOutputCount(in.size(), kInRate, kOutRate));

    {
        SoxrSampleRateConverter src1(kInRate, kOutRate);
        src1.Process(in.data(), in.size(), out1.data(), out1.size());
    }
    {
        SoxrSampleRateConverter src2(kInRate, kOutRate);
        src2.Process(in.data(), in.size(), out2.data(), out2.size());
    }

    bool matches = out1.size() == out2.size();
    for (std::size_t i = 0; matches && i < out1.size(); ++i) {
        matches = std::fabs(out1[i] - out2[i]) < 1e-5f;
    }
    Check(matches, "soxr determinism: two fresh converters constructed and fed identically produce the "
                    "same output (approximate float tolerance rather than exact equality - this class's own "
                    "constructor pins num_threads=1 specifically so this holds, since soxr's default runtime_spec "
                    "would otherwise follow OMP_NUM_THREADS and could vary run-to-run)");
}

// Real playback calls Process() many times in a row with SMALL per-block
// counts on the SAME long-lived converter instance
// (ResamplingMixerFeed::ConverterFor() caches one per channel). This test
// reproduces that calling pattern to confirm the one-time startup shortfall
// (padded with silence, see SoxrSampleRateConverter::Process()) really is
// one-time, not a periodic per-block glitch.
void SoxrSrcSteadyStateAcrossManySmallBlocksNeedsNoRepeatedPadding() {
    // Same 62500Hz->192000Hz ratio as the other Soxr tests above, and the same
    // per-block canonical output size (256, Mixer::kBlockSizeFrames) production
    // requests. 62500*256/192000 = 83.33(3), so a FIXED 83-samples-in-per-block
    // would starve the converter by ~0.33 samples per block and eventually show
    // up as a periodic gap. Real per-chip adapters (e.g. Ay8910DeviceAdapter::
    // Render()'s partial_input_clocks_) carry the fractional remainder forward
    // and alternate 83/84, so this test does the same.
    // kSettleBlocks=250: soxr's internal DFT algorithm needs a bounded,
    // multi-stage startup settling window (roughly 260ms at this rate; the
    // large initial burst arrives around block 11, with two smaller residual
    // corrections around blocks 69 and 197). After that no further shortfalls
    // occur.
    constexpr std::uint32_t kInRate = 62500;
    constexpr std::uint32_t kOutRate = 192000;
    constexpr std::size_t kOutputBlock = 256;
    constexpr int kTotalBlocks = 500;   // comfortably past the 197-block settle window found above
    constexpr int kSettleBlocks = 250;  // skip this many blocks before checking for gaps

    SoxrSampleRateConverter src(kInRate, kOutRate);
    std::vector<float> in;
    std::vector<float> out(kOutputBlock);
    double carry = 0.0;

    bool sawGapAfterSettle = false;
    for (int block = 0; block < kTotalBlocks; ++block) {
        carry += static_cast<double>(kOutputBlock) * kInRate / kOutRate;
        const auto inputCount = static_cast<std::size_t>(carry);
        carry -= static_cast<double>(inputCount);
        in.assign(inputCount, 0.5f);

        src.Process(in.data(), in.size(), out.data(), out.size());
        if (block < kSettleBlocks) continue;
        for (float sample : out) {
            // A "gap" here is the zero-padding fallback firing - any sample
            // meaningfully away from the expected 0.5 constant.
            if (std::fabs(sample - 0.5f) > 0.02f) {
                sawGapAfterSettle = true;
                break;
            }
        }
        if (sawGapAfterSettle) break;
    }

    Check(!sawGapAfterSettle,
          "soxr steady-state (many small blocks, real per-block sizing): after an initial settle period, repeated "
          "small Process() calls on the SAME long-lived converter never need the zero-padding fallback again - "
          "confirms the startup shortfall SoxrSrcPreservesDcLevel needed a large single call to avoid is really a "
          "ONE-TIME transient (self-corrects as fifo_ catches up across calls), not a periodic glitch that would "
          "recur every render block in real playback");
}

void ResamplingMixerFeedPassthroughAndResample() {
    std::vector<LogicalChannel> channels(2);
    channels[0].Id = 0;
    channels[0].NativeSampleRate = 0;  // unknown -> passthrough
    channels[1].Id = 1;
    channels[1].NativeSampleRate = Ym2151DeviceAdapter::kOutputSampleRateHz;  // 62500

    const std::size_t canonicalFrames = Mixer::kBlockSizeFrames;  // 256

    LogicalChannelSink native;
    native.RegisterChannel(0);
    native.RegisterChannel(1);

    // ch0: exactly one canonical block of a constant, already at canonical rate.
    std::vector<float> ch0(canonicalFrames, 0.5f);
    native.WriteSamples(0, ch0.data(), ch0.size());
    // ch1: the matching amount of 62500Hz input (~83 samples) of a constant.
    const std::size_t nativeCount =
        static_cast<std::size_t>(static_cast<double>(canonicalFrames) * 62500.0 / 192000.0) + 1;
    std::vector<float> ch1(nativeCount, 0.25f);
    native.WriteSamples(1, ch1.data(), ch1.size());

    ResamplingMixerFeed feed;

    // Warm-up: SoxrSampleRateConverter needs a real settling period before its
    // output is accurate (see SoxrSrcSteadyStateAcrossManySmallBlocksNeedsNoRepeatedPadding).
    // Real playback already warms up well before a user hears anything; this
    // loop reproduces that so the assertions below test steady-state accuracy,
    // not the converter's first cold sample.
    for (int warm = 0; warm < 40; ++warm) {
        native.ClearAll();
        std::vector<float> ch0Warm(canonicalFrames, 0.5f);
        native.WriteSamples(0, ch0Warm.data(), ch0Warm.size());
        const std::size_t nativeCountWarm =
            static_cast<std::size_t>(static_cast<double>(canonicalFrames) * 62500.0 / 192000.0) + 1;
        std::vector<float> ch1Warm(nativeCountWarm, 0.25f);
        native.WriteSamples(1, ch1Warm.data(), ch1Warm.size());
        feed.Resample(native, channels, canonicalFrames);
    }
    native.ClearAll();
    std::vector<float> ch0Fresh(canonicalFrames, 0.5f);
    native.WriteSamples(0, ch0Fresh.data(), ch0Fresh.size());
    const std::size_t nativeCountFresh =
        static_cast<std::size_t>(static_cast<double>(canonicalFrames) * 62500.0 / 192000.0) + 1;
    std::vector<float> ch1Fresh(nativeCountFresh, 0.25f);
    native.WriteSamples(1, ch1Fresh.data(), ch1Fresh.size());
    const LogicalChannelSink& canonical = feed.Resample(native, channels, canonicalFrames);

    Check(canonical.ChannelBuffer(0).Size() == canonicalFrames, "feed: passthrough channel fills exactly the block");
    Check(canonical.ChannelBuffer(1).Size() == canonicalFrames, "feed: resampled channel fills exactly the block");

    bool passthrough_ok = true;
    for (std::size_t i = 0; i < canonicalFrames; ++i) {
        passthrough_ok = passthrough_ok && std::fabs(canonical.ChannelBuffer(0)[i] - 0.5f) < 1e-6f;
    }
    Check(passthrough_ok, "feed: a NativeSampleRate==0 channel is copied through unchanged");

    bool resample_ok = true;
    for (std::size_t i = 0; i < canonicalFrames; ++i) {
        resample_ok = resample_ok && std::fabs(canonical.ChannelBuffer(1)[i] - 0.25f) < 1e-4f;
    }
    Check(resample_ok, "feed: a 62500Hz constant channel resamples to the same constant at 192kHz");

    // A second block reuses the per-channel converter state and still fills
    // exactly one block.
    native.ClearAll();
    std::vector<float> ch1b(nativeCount, 0.25f);
    native.WriteSamples(1, ch1b.data(), ch1b.size());
    const LogicalChannelSink& canonical2 = feed.Resample(native, channels, canonicalFrames);
    Check(canonical2.ChannelBuffer(1).Size() == canonicalFrames, "feed: a second block also fills exactly the block");
}

void XBoardMachineYm2151ReachesMixerThroughResampler() {
    XBoardMachine machine(std::vector<std::uint8_t>(0xF000, 0x00));
    machine.Initialize();

    // Loud sustained note on FM channel 0 (algorithm 7: all four operators
    // sum straight out).
    Ym2151DeviceAdapter& ym = machine.Ym2151ForTest();
    auto w = [&](std::uint8_t reg, std::uint8_t val) {
        ym.WriteAddress(reg);
        ym.WriteData(val);
    };
    w(0x20, 0xC7);
    w(0x28, 0x4A);
    for (std::uint8_t op : {std::uint8_t(0), std::uint8_t(8), std::uint8_t(16), std::uint8_t(24)}) {
        w(0x40 + op, 0x01);
        w(0x60 + op, 0x00);
        w(0x80 + op, 0x1F);
        w(0xE0 + op, 0x00);
    }
    w(0x08, 0x78);

    const std::size_t canonicalFrames = Mixer::kBlockSizeFrames * 8;  // 2048

    LogicalChannelSink native;
    ResamplingMixerFeed feed;

    // Warm-up, as in ResamplingMixerFeedPassthroughAndResample.
    for (int warm = 0; warm < 5; ++warm) {
        native.ClearAll();
        machine.Render(EmuDuration{canonicalFrames}, &native);
        feed.Resample(native, machine.LogicalChannels(), canonicalFrames);
    }

    native.ClearAll();
    machine.Render(EmuDuration{canonicalFrames}, &native);

    const ChannelId ch0 = machine.LogicalChannels()[0].Id;
    const std::size_t nativeSamples = native.ChannelBuffer(ch0).Size();
    // The YM2151 only produced ~1/3 of `canonicalFrames` native samples...
    Check(nativeSamples > 0 && nativeSamples < canonicalFrames,
          "e2e: XBoardMachine wrote native-rate (62500Hz) YM samples, fewer than the canonical block");

    const LogicalChannelSink& canonical = feed.Resample(native, machine.LogicalChannels(), canonicalFrames);

    // ...but the resampler produces a full canonical block of real samples,
    // not `nativeSamples` real ones plus silence padding.
    Check(canonical.ChannelBuffer(ch0).Size() == canonicalFrames,
          "e2e: the resampler fills the whole canonical block");
    std::size_t nonzero = 0;
    for (std::size_t i = 0; i < canonicalFrames; ++i) {
        if (std::fabs(canonical.ChannelBuffer(ch0)[i]) > 1e-6f) {
            ++nonzero;
        }
    }
    Check(nonzero > canonicalFrames * 9 / 10,
          "e2e: almost the entire canonical block carries real (resampled) audio, not zero padding");

    Mixer mixer;
    std::vector<StereoFrame> out;
    mixer.MixBlock(canonical, machine.LogicalChannels(), canonicalFrames, out);
    float mixPeak = 0.0f;
    for (const StereoFrame& f : out) {
        mixPeak = std::max({mixPeak, std::fabs(f.left), std::fabs(f.right)});
    }
    Check(mixPeak > 0.01f, "e2e: YM2151 audio reaches the Mixer's stereo output through the resampler");
}

void ResamplingMixerFeedCompensatesGroupDelayAcrossDifferentNativeRates() {
    // The YM2203/MSM5205 pairing: YM2203 (a modest 1.736x DOWNSAMPLE, measured
    // soxr_delay() ~1.9ms) vs MSM5205 (an extreme 24x UPSAMPLE, measured
    // ~63-79ms depending on feed chunking) - two Stage 1 SoxrSampleRateConverter
    // instances with very different own group delays. Without
    // ResamplingMixerFeed's leading-silence compensation, a synchronized
    // onset at the SAME native-time instant on both channels would reach the
    // canonical (Mixer-facing) output tens of milliseconds apart. This test
    // pins that the compensation closes that gap to a small fraction of it.
    std::vector<LogicalChannel> channels(2);
    channels[0].Id = 0;
    channels[0].NativeSampleRate = Ym2203DeviceAdapter::kOutputSampleRateHz;    // 333333, downsample
    channels[1].Id = 1;
    channels[1].NativeSampleRate = Msm5205DeviceAdapter::kOutputSampleRateHz;  // 8000, upsample

    const std::size_t canonicalFrames = Mixer::kBlockSizeFrames;  // 256

    // Bresenham-style carry tracking for how many native samples to feed
    // each block, NOT a fixed rounded-per-block count (a fixed per-block
    // chunk's rounding error compounds into a large artifactual backlog over
    // many blocks; see SoxrSampleRateConverter::SteadyStateGroupDelaySamples()).
    // ClockConverter-driven Render() paces native-rate output: no drift
    // between the requested and actually-fed sample count, however many
    // blocks run.
    struct FeedPacer {
        std::uint32_t rate;
        std::uint64_t totalFedSoFar = 0;
        std::size_t NextBlockCount(std::size_t blockIndexFromOne) {
            const std::uint64_t targetTotal = static_cast<std::uint64_t>(std::llround(
                static_cast<double>(blockIndexFromOne) * static_cast<double>(Mixer::kBlockSizeFrames) *
                static_cast<double>(rate) / 192000.0));
            const std::size_t count = static_cast<std::size_t>(targetTotal - totalFedSoFar);
            totalFedSoFar = targetTotal;
            return count;
        }
    };
    FeedPacer pacer0{channels[0].NativeSampleRate};
    FeedPacer pacer1{channels[1].NativeSampleRate};

    ResamplingMixerFeed feed;
    LogicalChannelSink native;
    native.RegisterChannel(0);
    native.RegisterChannel(1);

    // Silence-warm both channels well past MSM5205's own worst-case measured
    // group delay plus soxr's own startup transient, so the leading-silence
    // compensation this test is actually checking has long since been fully
    // drained before the onset below - what's left to measure is purely each
    // channel's steady-state FILTER delay from that point on, matching how a
    // real track's silent lead-in / boot warm-up already behaves in
    // production.
    constexpr int kSilenceWarmupBlocks = 2250;
    std::size_t blockIndex = 0;
    for (int i = 0; i < kSilenceWarmupBlocks; ++i) {
        ++blockIndex;
        native.ClearAll();
        std::vector<float> silence0(pacer0.NextBlockCount(blockIndex), 0.0f);
        std::vector<float> silence1(pacer1.NextBlockCount(blockIndex), 0.0f);
        native.WriteSamples(0, silence0.data(), silence0.size());
        native.WriteSamples(1, silence1.data(), silence1.size());
        feed.Resample(native, channels, canonicalFrames);
    }

    // Now feed a clean step onset (0 -> 1.0) on BOTH channels, starting at
    // the same native-time instant, for many subsequent blocks, and collect
    // each channel's canonical output across that whole span.
    constexpr int kToneBlocks = 150;
    std::vector<float> collected0;
    std::vector<float> collected1;
    collected0.reserve(kToneBlocks * canonicalFrames);
    collected1.reserve(kToneBlocks * canonicalFrames);
    for (int i = 0; i < kToneBlocks; ++i) {
        ++blockIndex;
        native.ClearAll();
        std::vector<float> tone0(pacer0.NextBlockCount(blockIndex), 1.0f);
        std::vector<float> tone1(pacer1.NextBlockCount(blockIndex), 1.0f);
        native.WriteSamples(0, tone0.data(), tone0.size());
        native.WriteSamples(1, tone1.data(), tone1.size());
        const LogicalChannelSink& canonical = feed.Resample(native, channels, canonicalFrames);
        const LogicalChannelBuffer& buf0 = canonical.ChannelBuffer(0);
        const LogicalChannelBuffer& buf1 = canonical.ChannelBuffer(1);
        collected0.insert(collected0.end(), buf0.Data(), buf0.Data() + buf0.Size());
        collected1.insert(collected1.end(), buf1.Data(), buf1.Data() + buf1.Size());
    }

    // Onset = first index where the signal has clearly and durably risen
    // (>0.5, matching the 0->1.0 step) - a plain threshold crossing is
    // robust enough here since both channels are fed the exact same clean
    // step, so any pre-ringing/ramp is a fixed property of soxr's own filter
    // shape, not something that differs between the two channels' true onset.
    auto findOnset = [](const std::vector<float>& v) -> std::size_t {
        for (std::size_t i = 0; i < v.size(); ++i) {
            if (v[i] > 0.5f) {
                return i;
            }
        }
        return v.size();
    };
    const std::size_t onset0 = findOnset(collected0);
    const std::size_t onset1 = findOnset(collected1);

    Check(onset0 < collected0.size() && onset1 < collected1.size(),
          "group delay compensation: both channels eventually cross the step threshold");

    const std::size_t rawDifference =
        onset0 > onset1 ? onset0 - onset1 : onset1 - onset0;
    // Uncompensated, this pairing's group delay difference is in the
    // ~12000-15000 output-sample (~63-79ms) range. With compensation, the
    // measured residual gap is ~900-950 samples (~4.8-5.0ms) - NOT exactly 0,
    // since the two channels' filters have different shapes/lengths, so a
    // fixed >0.5 step-response threshold crossing doesn't land at exactly the
    // same point in each filter's transition. A 2048-sample (~10.7ms)
    // tolerance is comfortably above the observed residual, still more than
    // an order of magnitude tighter than the uncompensated gap, and well
    // under typical audio cross-channel sync perceptibility (~20-40ms).
    Check(rawDifference < 2048,
          "group delay compensation: YM2203 (downsample) and MSM5205 (upsample) onsets land within ~11ms of each "
          "other in the canonical stream (residual filter-shape difference), not the ~63-79ms apart they would "
          "without ResamplingMixerFeed's leading-silence compensation");
}
