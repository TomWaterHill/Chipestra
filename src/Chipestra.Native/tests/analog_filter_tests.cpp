// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Analog output-stage filter tests. The expected responses are derived from the analog transfer
// functions (first-order and second-order Butterworth low-pass), not read back from the implementation.

#include "analog_filter_tests.h"

#include "devices/sega/megadrive/megadrive68k_machine.h"
#include "devices/sega/megadrive/megadrive_machine.h"
#include "mixer.h"
#include "resampling_mixer_feed.h"
#include "test_support.h"
#include "chipestra_abi.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kRate = 192000.0;  // the canonical mix rate

// Runs `signal` (one channel, canonical rate) through a ResamplingMixerFeed block by block.
std::vector<float> RunThroughFeed(ResamplingMixerFeed& feed, const LogicalChannel& channel, const std::vector<float>& signal) {
    std::vector<LogicalChannel> channels{channel};
    LogicalChannelSink sink;
    sink.RegisterChannel(channel.Id);
    std::vector<float> out;
    out.reserve(signal.size());
    const std::size_t block = Mixer::kBlockSizeFrames;
    for (std::size_t pos = 0; pos + block <= signal.size(); pos += block) {
        sink.ClearAll();
        sink.WriteSamples(channel.Id, signal.data() + pos, block);
        const LogicalChannelSink& canonical = feed.Resample(sink, channels, block);
        const LogicalChannelBuffer& buffer = canonical.ChannelBuffer(channel.Id);
        out.insert(out.end(), buffer.Data(), buffer.Data() + buffer.Size());
    }
    return out;
}

LogicalChannel MakeChannel(std::uint32_t id, AnalogFilterGroupId group) {
    LogicalChannel c;
    c.Id = ChannelId(id);
    c.NativeSampleRate = Mixer::kCanonicalSampleRateHz;
    c.AnalogFilterGroup = group;
    return c;
}

std::vector<float> Sine(double hz, std::size_t n, double amp = 0.5) {
    std::vector<float> s(n);
    for (std::size_t i = 0; i < n; ++i) s[i] = static_cast<float>(amp * std::sin(2.0 * kPi * hz * static_cast<double>(i) / kRate));
    return s;
}

// Gain (output amplitude / input amplitude) in the settled second half of a one-channel run.
double MeasuredGain(std::uint32_t mode, AnalogFilterGroupId group, double hz) {
    ResamplingMixerFeed feed;
    feed.SetAnalogFilterMode(mode);
    // Let the crossfade finish first (a constant-zero lead-in), then measure on a long sine.
    const std::size_t lead = ResamplingMixerFeed::kAnalogFilterFadeFrames + 2 * Mixer::kBlockSizeFrames;
    const std::size_t total = lead + 40960;
    std::vector<float> in = Sine(hz, total);
    const std::vector<float> out = RunThroughFeed(feed, MakeChannel(0, group), in);
    // RMS over the last 25600 samples (an integral number of cycles is not needed at this length).
    double sumIn = 0.0, sumOut = 0.0;
    for (std::size_t i = out.size() - 25600; i < out.size(); ++i) {
        sumIn += static_cast<double>(in[i]) * in[i];
        sumOut += static_cast<double>(out[i]) * out[i];
    }
    return std::sqrt(sumOut / sumIn);
}

double ToDb(double gain) { return 20.0 * std::log10(gain); }
double FirstOrderDb(double hz, double fc) { return -10.0 * std::log10(1.0 + (hz / fc) * (hz / fc)); }
double Butterworth2Db(double hz, double fc) { return -10.0 * std::log10(1.0 + std::pow(hz / fc, 4.0)); }

}  // namespace

void AnalogFilterOffIsAnExactPassThrough() {
    ResamplingMixerFeed feed;  // default mode: Off
    const std::vector<float> in = Sine(5000.0, 20480);
    for (AnalogFilterGroupId group : {AnalogFilterGroupId::Fm, AnalogFilterGroupId::Psg}) {
        ResamplingMixerFeed f;
        const std::vector<float> out = RunThroughFeed(f, MakeChannel(0, group), in);
        Check(out.size() == in.size() && std::memcmp(out.data(), in.data(), in.size() * sizeof(float)) == 0,
              "analog filter Off: a marked channel's samples are bit-identical to the input");
    }
    // Back to Off after another mode: the output returns to the exact input once the crossfade is over.
    ResamplingMixerFeed f;
    f.SetAnalogFilterMode(CHIPESTRA_ANALOG_FILTER_MODEL2);
    (void)RunThroughFeed(f, MakeChannel(0, AnalogFilterGroupId::Fm), Sine(1000.0, 8192));
    f.SetAnalogFilterMode(CHIPESTRA_ANALOG_FILTER_OFF);
    const std::vector<float> long_in = Sine(5000.0, 4 * 3840 + 2048);
    const std::vector<float> out = RunThroughFeed(f, MakeChannel(0, AnalogFilterGroupId::Fm), long_in);
    const std::size_t tail = 2048;
    Check(std::memcmp(out.data() + out.size() - tail, long_in.data() + long_in.size() - tail, tail * sizeof(float)) == 0,
          "analog filter Model 2 -> Off: after the crossfade the output is bit-identical to the input again");
}

void AnalogFilterModel1FrequencyResponseMatchesTheFirstOrderDesign() {
    const double fc = analog_filter::kModel1OutputCutoffHz;
    Check(std::fabs(fc - 3380.0) < 1e-9, "analog filter Model 1 cutoff is 3380 Hz");
    for (AnalogFilterGroupId group : {AnalogFilterGroupId::Fm, AnalogFilterGroupId::Psg}) {
        const double atCutoff = ToDb(MeasuredGain(CHIPESTRA_ANALOG_FILTER_MODEL1, group, fc));
        Check(std::fabs(atCutoff - (-3.0103)) < 0.1, "analog filter Model 1: -3 dB at the cutoff (FM and PSG alike)");
    }
    // First-order slope: -20 dB per decade, so 10x the cutoff is -20 dB; the bilinear mapping stays within 1 dB.
    const double atTen = ToDb(MeasuredGain(CHIPESTRA_ANALOG_FILTER_MODEL1, AnalogFilterGroupId::Fm, 10.0 * fc));
    Check(std::fabs(atTen - FirstOrderDb(10.0 * fc, fc)) < 1.0, "analog filter Model 1: first-order slope above the cutoff");
    const double low = ToDb(MeasuredGain(CHIPESTRA_ANALOG_FILTER_MODEL1, AnalogFilterGroupId::Fm, 200.0));
    Check(std::fabs(low - FirstOrderDb(200.0, fc)) < 0.05, "analog filter Model 1: flat in the low range");
}

void AnalogFilterModel2FrequencyResponseMatchesTheTwoStageDesign() {
    const double fa = analog_filter::kModel2FmCutoffHz;
    const double fb = analog_filter::kModel2OutputCutoffHz;
    Check(std::fabs(fa - 6725.0) < 1e-9 && std::fabs(fb - 3789.0) < 1e-9, "analog filter Model 2 cutoffs are 6725 Hz (FM) and 3789 Hz (output)");
    // PSG sees only the first-order output stage: -3 dB at its cutoff.
    const double psgAtB = ToDb(MeasuredGain(CHIPESTRA_ANALOG_FILTER_MODEL2, AnalogFilterGroupId::Psg, fb));
    Check(std::fabs(psgAtB - (-3.0103)) < 0.1, "analog filter Model 2: PSG has only the first-order stage (-3 dB at 3789 Hz)");
    // FM sees both: the product of the second-order Butterworth and the first-order response.
    for (double hz : {500.0, 3000.0, 6725.0, 12000.0}) {
        const double expected = Butterworth2Db(hz, fa) + FirstOrderDb(hz, fb);
        const double measured = ToDb(MeasuredGain(CHIPESTRA_ANALOG_FILTER_MODEL2, AnalogFilterGroupId::Fm, hz));
        Check(std::fabs(measured - expected) < 0.3, "analog filter Model 2: FM response matches the two-stage design");
    }
    // The second-order stage is -3 dB at 6725 Hz of its own: compare FM and PSG at that frequency.
    const double fm = ToDb(MeasuredGain(CHIPESTRA_ANALOG_FILTER_MODEL2, AnalogFilterGroupId::Fm, fa));
    const double psg = ToDb(MeasuredGain(CHIPESTRA_ANALOG_FILTER_MODEL2, AnalogFilterGroupId::Psg, fa));
    Check(std::fabs((psg - fm) - 3.0103) < 0.15, "analog filter Model 2: the FM-only stage is -3 dB at 6725 Hz");
}

void AnalogFilterOnlyTouchesMarkedChannels() {
    for (std::uint32_t mode : {CHIPESTRA_ANALOG_FILTER_MODEL1, CHIPESTRA_ANALOG_FILTER_MODEL2}) {
        ResamplingMixerFeed feed;
        feed.SetAnalogFilterMode(mode);
        const std::vector<float> in = Sine(9000.0, 4 * 3840);
        const std::vector<float> out = RunThroughFeed(feed, MakeChannel(0, AnalogFilterGroupId::None), in);
        Check(out.size() == in.size() && std::memcmp(out.data(), in.data(), in.size() * sizeof(float)) == 0,
              "analog filter: a channel with no filter group is left bit-identical in Model 1 and Model 2");
    }
}

void AnalogFilterModeChangeCrossfadesWithoutAStep() {
    // 8 kHz is attenuated to about 0.39 by Model 1, so a hard switch would jump; the crossfade must not.
    const double hz = 8000.0;
    const double amp = 0.5;
    ResamplingMixerFeed feed;
    const std::size_t before = 4 * Mixer::kBlockSizeFrames;
    std::vector<float> in = Sine(hz, 40 * Mixer::kBlockSizeFrames, amp);
    std::vector<LogicalChannel> channels{MakeChannel(0, AnalogFilterGroupId::Fm)};
    LogicalChannelSink sink;
    sink.RegisterChannel(0);
    std::vector<float> out;
    for (std::size_t pos = 0; pos + Mixer::kBlockSizeFrames <= in.size(); pos += Mixer::kBlockSizeFrames) {
        if (pos == before) feed.SetAnalogFilterMode(CHIPESTRA_ANALOG_FILTER_MODEL1);
        sink.ClearAll();
        sink.WriteSamples(0, in.data() + pos, Mixer::kBlockSizeFrames);
        const LogicalChannelBuffer& b = feed.Resample(sink, channels, Mixer::kBlockSizeFrames).ChannelBuffer(0);
        out.insert(out.end(), b.Data(), b.Data() + b.Size());
    }
    // A pure sine of this amplitude changes by at most amp * 2*pi*f/fs per sample; allow a margin for the blend.
    const double maxStep = amp * 2.0 * kPi * hz / kRate;
    double worst = 0.0;
    for (std::size_t i = 1; i < out.size(); ++i) worst = std::max(worst, static_cast<double>(std::fabs(out[i] - out[i - 1])));
    Check(worst <= maxStep * 1.25, "analog filter: switching Off -> Model 1 while playing leaves no step in the output");
    // A second switch in the middle of a fade stays continuous too.
    ResamplingMixerFeed feed2;
    out.clear();
    for (std::size_t pos = 0; pos + Mixer::kBlockSizeFrames <= in.size(); pos += Mixer::kBlockSizeFrames) {
        if (pos == before) feed2.SetAnalogFilterMode(CHIPESTRA_ANALOG_FILTER_MODEL1);
        if (pos == before + 4 * Mixer::kBlockSizeFrames) feed2.SetAnalogFilterMode(CHIPESTRA_ANALOG_FILTER_MODEL2);
        sink.ClearAll();
        sink.WriteSamples(0, in.data() + pos, Mixer::kBlockSizeFrames);
        const LogicalChannelBuffer& b = feed2.Resample(sink, channels, Mixer::kBlockSizeFrames).ChannelBuffer(0);
        out.insert(out.end(), b.Data(), b.Data() + b.Size());
    }
    worst = 0.0;
    for (std::size_t i = 1; i < out.size(); ++i) worst = std::max(worst, static_cast<double>(std::fabs(out[i] - out[i - 1])));
    Check(worst <= maxStep * 1.25, "analog filter: changing the mode again in the middle of a crossfade leaves no step");
    // After the fade the output is the filtered signal: amplitude near the Model 2 FM response at 8 kHz.
    const double expectedDb = Butterworth2Db(hz, analog_filter::kModel2FmCutoffHz) + FirstOrderDb(hz, analog_filter::kModel2OutputCutoffHz);
    double peak = 0.0;
    for (std::size_t i = out.size() - 2048; i < out.size(); ++i) peak = std::max(peak, static_cast<double>(std::fabs(out[i])));
    Check(std::fabs(ToDb(peak / amp) - expectedDb) < 0.5, "analog filter: after the crossfade the settled level is the new mode's response");
}

void AnalogFilterAbiValidatesArguments() {
    Check(chipestra_set_analog_filter_mode(nullptr, CHIPESTRA_ANALOG_FILTER_MODEL1) == CHIPESTRA_E_INVALID_ARGUMENT,
          "chipestra_set_analog_filter_mode(null) == CHIPESTRA_E_INVALID_ARGUMENT");
    ChipestraHandle handle = nullptr;
    Check(chipestra_create_instance(&handle) == CHIPESTRA_OK, "setup: chipestra_create_instance() == CHIPESTRA_OK");
    if (handle == nullptr) return;
    Check(chipestra_set_analog_filter_mode(handle, 3) == CHIPESTRA_E_INVALID_ARGUMENT,
          "chipestra_set_analog_filter_mode(unknown mode) == CHIPESTRA_E_INVALID_ARGUMENT");
    Check(chipestra_set_analog_filter_mode(handle, CHIPESTRA_ANALOG_FILTER_MODEL1) == CHIPESTRA_OK,
          "chipestra_set_analog_filter_mode(Model 1) on a Created instance == CHIPESTRA_OK");
    Check(chipestra_set_analog_filter_mode(handle, CHIPESTRA_ANALOG_FILTER_MODEL2) == CHIPESTRA_OK,
          "chipestra_set_analog_filter_mode(Model 2) == CHIPESTRA_OK");
    Check(chipestra_set_analog_filter_mode(handle, CHIPESTRA_ANALOG_FILTER_OFF) == CHIPESTRA_OK,
          "chipestra_set_analog_filter_mode(Off) == CHIPESTRA_OK");
    chipestra_destroy_instance(handle);
}

void MegaDriveMachinesMarkTheirFmAndPsgChannelsForTheAnalogFilter() {
    auto check = [](const std::vector<LogicalChannel>& channels, const char* label) {
        std::size_t fm = 0, psg = 0, other = 0;
        bool order = true;
        for (std::size_t i = 0; i < channels.size(); ++i) {
            switch (channels[i].AnalogFilterGroup) {
                case AnalogFilterGroupId::Fm: ++fm; order = order && channels[i].DeviceName == "YM2612"; break;
                case AnalogFilterGroupId::Psg: ++psg; order = order && channels[i].DeviceName == "SN76489"; break;
                default: ++other; break;
            }
        }
        Check(fm == 7 && psg == 4 && other == 0 && order, label);
    };
    MegaDriveMachine z80Machine(std::vector<std::uint8_t>(0x1000), MegaDriveDriverConfig{});
    check(z80Machine.LogicalChannels(), "MegaDriveMachine: 7 YM2612 channels are Fm and 4 SN76489 channels are Psg");
    MegaDrive68kMachine m68kMachine(std::vector<std::uint8_t>(0x1000), MegaDrive68kDriverConfig{});
    check(m68kMachine.LogicalChannels(), "MegaDrive68kMachine: 7 YM2612 channels are Fm and 4 SN76489 channels are Psg");
}
