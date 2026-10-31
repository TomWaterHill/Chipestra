// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Sunsoft 5B sound chip (Sunsoft5bSoundDeviceAdapter) native tests.

#include "sunsoft_5b_adapter_tests.h"

#include "devices/sound/sunsoft_5b/sunsoft_5b_sound_device_adapter.h"
#include "test_support.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using Chip = Sunsoft5bSoundDeviceAdapter;
using Channels = std::array<std::vector<float>, Chip::kChannelCount>;

const char* GetEnvVar(const char* name) {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    return std::getenv(name);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
}

void Poke(Chip& chip, std::uint8_t reg, std::uint8_t value) {
    chip.WriteAddress(reg);
    chip.WriteData(value);
}

// `samples` samples (16 CPU cycles each).
Channels Run(Chip& chip, std::uint64_t samples) {
    Channels out;
    chip.Render(samples * 16, out);
    return out;
}

double Sum(const Channels& c, std::size_t i) { return static_cast<double>(c[0][i]) + c[1][i] + c[2][i]; }

// The chip's table value for three fixed levels.
float Fixed(int a, int b, int c) {
    const int level[3] = {a, b, c};
    const bool env[3] = {false, false, false};
    return Chip::MixedOutput(level, env);
}

}  // namespace

void Sunsoft5bRegistersAndAddressLatch() {
    std::printf("Sunsoft5bRegistersAndAddressLatch\n");
    Chip chip;
    chip.WriteData(0x55);  // no register selected yet: ignored
    Check(chip.RegisterForTest(0) == 0, "a data write before the first address write is ignored");
    chip.WriteAddress(0x00);
    chip.WriteData(0x34);
    chip.WriteAddress(0x01);
    chip.WriteData(0xFA);
    Check(chip.RegisterForTest(0) == 0x34 && chip.RegisterForTest(1) == 0xFA, "the address selects the register the data goes to");
    Check(chip.TonePeriodForTest(0) == 0xA34, "a tone period is the fine byte and the low four bits of the coarse byte (0xA34)");
    chip.WriteAddress(0xF3);  // only the low four bits reach the chip
    chip.WriteData(0x07);
    Check(chip.RegisterForTest(3) == 0x07 && chip.TonePeriodForTest(1) == 0x700, "the address takes the low four bits only (0xF3 selects register 3)");
    Poke(chip, 5, 0x21);
    Poke(chip, 4, 0x43);
    Check(chip.TonePeriodForTest(2) == 0x143, "channel C period");
    Poke(chip, 11, 0x12);
    Poke(chip, 12, 0x03);
    Check(chip.RegisterForTest(11) == 0x12 && chip.RegisterForTest(12) == 0x03, "envelope period registers are stored");
    Poke(chip, 13, 0x0C);
    Check(chip.EnvelopeStepForTest() == 31 && chip.EnvelopeVolumeForTest() == 0, "an attack shape starts at volume 0 (step 31 xor 0x1F)");
    Poke(chip, 13, 0x00);
    Check(chip.EnvelopeStepForTest() == 31 && chip.EnvelopeVolumeForTest() == 31, "a decay shape starts at volume 31");
    chip.Reset();
    Check(chip.RegisterForTest(0) == 0 && chip.RegisterForTest(1) == 0 && chip.TonePeriodForTest(0) == 0, "Reset clears the registers");
    Check(chip.EnvelopeVolumeForTest() == 31 && chip.NoiseRngForTest() == 1, "Reset leaves the decay shape written to register 13 and the noise register at 1");
    chip.WriteData(0x01);
    Check(chip.RegisterForTest(0) == 0 && chip.RegisterForTest(13) == 0, "after Reset no register is selected");
    Check(Chip::kOutputSampleRateHz == 111861 && Chip::kCpuCyclesPerSample == 16, "the chip makes a sample every 16 CPU cycles (111860.75 Hz)");
}

void Sunsoft5bToneAndNoiseTiming() {
    std::printf("Sunsoft5bToneAndNoiseTiming\n");
    {
        Chip chip;
        Poke(chip, 0, 3);       // channel A period 3
        Poke(chip, 7, 0x38);    // tones on, noise off for all
        Poke(chip, 8, 15);      // fixed volume 15
        Channels c = Run(chip, 12);
        // The tone output starts low and flips when the counter reaches the period: samples 3, 6, 9, 12.
        const float low = Fixed(0, 0, 0), high = Fixed(15, 0, 0);
        bool ok = true;
        for (std::size_t i = 0; i < 12; ++i) {
            const bool expectHigh = ((i + 1) / 3) % 2 == 1;
            ok = ok && std::fabs(Sum(c, i) - (expectHigh ? high : low)) < 1e-6;
        }
        Check(ok, "a tone of period 3 flips every 3 samples (low for samples 1-2, high for 3-5, low for 6-8, ...)");
        Check(high > low, "the fixed-volume 15 output is larger than volume 0");
    }
    {
        Chip chip;
        Poke(chip, 0, 0);  // period 0 counts as 1: flips every sample
        Poke(chip, 7, 0x38);
        Poke(chip, 8, 15);
        Channels c = Run(chip, 6);
        bool alternates = true;
        for (std::size_t i = 1; i < 6; ++i) alternates = alternates && c[0][i] != c[0][i - 1];
        Check(alternates, "period 0 behaves as period 1 (the output flips every sample)");
    }
    {
        Chip chip;
        Poke(chip, 6, 1);  // noise period 1: the prescaler flips every sample and the LFSR moves every second sample
        Run(chip, 1);
        Check(chip.NoiseRngForTest() == 1, "after one sample the LFSR has not moved");
        Run(chip, 1);
        Check(chip.NoiseRngForTest() == 0x10000, "after two samples it has moved once: (1 >> 1) | ((bit0 xor bit3) << 16) = 0x10000");
        Run(chip, 2);
        Check(chip.NoiseRngForTest() == 0x8000, "after four samples: 0x8000");
        Chip slow;
        Poke(slow, 6, 3);
        Run(slow, 5);
        Check(slow.NoiseRngForTest() == 1, "noise period 3: the LFSR has not moved after 5 samples");
        Run(slow, 1);
        Check(slow.NoiseRngForTest() == 0x10000, "... and moves on the 6th (every 2 x period samples)");
    }
    {
        Chip chip;
        Poke(chip, 7, 0x00);  // both noise and tone gate the channel
        Poke(chip, 8, 15);
        Poke(chip, 0, 0xFF);
        Poke(chip, 1, 0x0F);  // a long tone period: the tone output stays low
        Channels c = Run(chip, 4);
        bool silent = true;
        for (std::size_t i = 0; i < 4; ++i) silent = silent && std::fabs(Sum(c, i) - Fixed(0, 0, 0)) < 1e-6;
        Check(silent, "tone low and noise gating: the channel is at level 0 (the chip output of volume 0)");
        Chip open;
        Poke(open, 7, 0x3F);  // neither tone nor noise gates the channel: output is 1, modulated by the volume
        Poke(open, 8, 9);
        Channels d = Run(open, 3);
        Check(std::fabs(Sum(d, 2) - Fixed(9, 0, 0)) < 1e-6, "tone and noise both disabled: the channel passes its volume");
    }
}

void Sunsoft5bEnvelopeSteps() {
    std::printf("Sunsoft5bEnvelopeSteps\n");
    {
        Chip chip;
        Poke(chip, 11, 1);  // period 1: one step every sample
        Poke(chip, 13, 0x0C);  // continue, attack, no alternate, no hold: a sawtooth of 32 steps
        Run(chip, 1);
        Check(chip.EnvelopeVolumeForTest() == 1, "attack: the volume is 1 after the first step");
        Run(chip, 30);
        Check(chip.EnvelopeVolumeForTest() == 31, "31 after 31 steps");
        Run(chip, 1);
        Check(chip.EnvelopeVolumeForTest() == 0, "and the sawtooth starts again at 0 (32 steps, not 16)");
    }
    {
        Chip chip;
        Poke(chip, 11, 1);
        Poke(chip, 13, 0x00);  // decay once, then hold at 0
        Run(chip, 31);
        Check(chip.EnvelopeVolumeForTest() == 0, "decay: 0 after 31 steps");
        Run(chip, 40);
        Check(chip.EnvelopeVolumeForTest() == 0, "and it holds there");
    }
    {
        Chip chip;
        Poke(chip, 11, 4);  // period 4: a step every 4 samples (the YM envelope is not slowed down like the AY's)
        Poke(chip, 13, 0x0C);
        Run(chip, 3);
        Check(chip.EnvelopeVolumeForTest() == 0, "period 4: no step in the first 3 samples");
        Run(chip, 1);
        Check(chip.EnvelopeVolumeForTest() == 1, "period 4: the first step is on the 4th sample");
        Run(chip, 8);
        Check(chip.EnvelopeVolumeForTest() == 3, "period 4: two more steps in the next 8 samples");
    }
    {
        Chip chip;
        Poke(chip, 11, 1);
        Poke(chip, 13, 0x0E);  // continue, attack, alternate: a triangle
        Poke(chip, 7, 0x3F);
        Poke(chip, 8, 0x10);  // channel A uses the envelope
        Channels c = Run(chip, 40);
        const bool rising = c[0][5] > c[0][2];
        const bool falling = c[0][36] < c[0][33];
        Check(rising && falling, "a triangle envelope rises and then falls");
    }
}

void Sunsoft5bMixTable() {
    std::printf("Sunsoft5bMixTable\n");
    const int zero[3] = {0, 0, 0};
    const bool envOn[3] = {true, true, true};
    const int top[3] = {31, 31, 31};
    const float smallest = Chip::MixedOutput(zero, envOn);
    const float largest = Chip::MixedOutput(top, envOn);
    Check(smallest == 0.0f, "the smallest output of the table (all three channels at envelope level 0) is 0");
    Check(largest == 3.0f, "the largest (all three at envelope level 31) is 3.0 (MAME's normalized table times its factor 3)");
    Check(Fixed(0, 0, 0) > 0.0f && Fixed(0, 0, 0) < 0.01f, "all channels at fixed volume 0 are a small constant, not 0 (the chip is not a zero-is-off type)");
    bool monotonic = true;
    for (int level = 1; level < 16; ++level) monotonic = monotonic && Fixed(level, 0, 0) > Fixed(level - 1, 0, 0);
    Check(monotonic, "the output rises with a channel's fixed volume");
    Check(Fixed(15, 15, 15) > Fixed(15, 15, 0) && Fixed(15, 15, 0) > Fixed(15, 0, 0), "and with the number of channels");
    Check(Fixed(15, 0, 0) == Fixed(0, 15, 0) && Fixed(0, 15, 0) == Fixed(0, 0, 15), "the three channels are interchangeable");
    // The formula for fixed volume 15 on one channel, worked out with the chip's resistances (pull-up 630, pull-down 801,
    // 1 kOhm load, channel resistance of level 15 = 251, of level 0 = 73770).
    auto voltage = [](double a, double b, double c) {
        double rt = 3.0 / 630.0 + 3.0 / 801.0 + 1.0 / 1000.0 + 1.0 / a + 1.0 / b + 1.0 / c;
        double rw = 3.0 / 630.0 + 1.0 / a + 1.0 / b + 1.0 / c;
        return rw / rt;
    };
    const double vMin = voltage(103350, 103350, 103350), vMax = voltage(251, 251, 251);
    const double expected = 3.0 * (voltage(251, 73770, 73770) - vMin) / (vMax - vMin);
    Check(std::fabs(Fixed(15, 0, 0) - expected) < 1e-6, "Fixed(15,0,0) equals the resistor-network formula, normalized between the table's smallest and largest voltage");
    const int one[3] = {10, 0, 0};
    const bool envFirst[3] = {true, false, false};
    Check(Chip::MixedOutput(one, envFirst) != Fixed(10, 0, 0), "a channel on the envelope uses the 32-level resistances, not the 16-level ones");
}

void Sunsoft5bChannelsAddUpAndMuteTakesAChannelOut() {
    std::printf("Sunsoft5bChannelsAddUpAndMuteTakesAChannelOut\n");
    auto setup = [](Chip& chip) {
        Poke(chip, 0, 5);
        Poke(chip, 2, 7);
        Poke(chip, 4, 11);
        Poke(chip, 6, 4);
        Poke(chip, 7, 0x00);
        Poke(chip, 8, 12);
        Poke(chip, 9, 0x10);
        Poke(chip, 10, 7);
        Poke(chip, 11, 3);
        Poke(chip, 13, 0x0C);
    };
    Chip chip;
    setup(chip);
    Channels c = Run(chip, 400);
    // Compare with the table value of the levels the chip has at each sample: the sum of the three channels is the
    // output, and each channel has its conductance's share of it.
    Chip ref;
    setup(ref);
    bool sumsOk = true, sharesOk = true, someSignal = false;
    for (std::size_t i = 0; i < 400; ++i) {
        const double total = Sum(c, i);
        sumsOk = sumsOk && total >= 0.0 && total <= 3.0;
        if (total > Fixed(0, 0, 0) + 1e-3) someSignal = true;
        // channel B is on the envelope with a level of 0..31, so its share is positive and the shares are in 0..total
        for (int ch = 0; ch < 3; ++ch) sharesOk = sharesOk && c[ch][i] >= 0.0f && c[ch][i] <= total + 1e-6;
    }
    Check(sumsOk && someSignal, "the three channel streams add up to a chip output between 0 and 3 that is not silent");
    Check(sharesOk, "every channel's share is between 0 and the total");
    {
        // channels at fixed levels with known conductances: the shares are in proportion
        Chip fixedChip;
        Poke(fixedChip, 7, 0x3F);
        Poke(fixedChip, 8, 15);
        Poke(fixedChip, 9, 15);
        Poke(fixedChip, 10, 0);
        Channels f = Run(fixedChip, 2);
        Check(std::fabs(f[0][1] - f[1][1]) < 1e-6f && f[2][1] < f[0][1], "equal levels give equal shares and a quieter channel a smaller one");
        Check(std::fabs(Sum(f, 1) - Fixed(15, 15, 0)) < 1e-6, "and they add up to the table value");
    }
    // Mute channel B.
    Chip muted;
    setup(muted);
    muted.SetMutedChannels(0x02);
    Channels m = Run(muted, 400);
    Chip level0;
    setup(level0);
    bool unmutedSumOk = true, mutedShareOk = true;
    Chip probe;
    setup(probe);
    for (std::size_t i = 0; i < 400; ++i) {
        // the unmuted channels carry the output of the chip with channel B at volume 0 and off the envelope; with B
        // taken out, the output is the table value for A and C and B at 0
        const double a = m[0][i], b = m[1][i], cc = m[2][i];
        (void)b;
        unmutedSumOk = unmutedSumOk && (a + cc) >= 0.0 && (a + cc) <= 3.0;
        mutedShareOk = mutedShareOk && c[1][i] == m[1][i];
    }
    Check(unmutedSumOk, "with a channel muted, the other two channels still make the output of the chip without it");
    Check(mutedShareOk, "the muted channel's own buffer keeps the share it has when it is not muted");
    {
        Chip a, b;
        for (Chip* chip : {&a, &b}) {
            Poke(*chip, 7, 0x3F);
            Poke(*chip, 8, 15);
            Poke(*chip, 9, 15);
            Poke(*chip, 10, 15);
        }
        b.SetMutedChannels(0x04);
        Channels fa = Run(a, 2), fb = Run(b, 2);
        // with C out of the mix the chip is at the table value of A and B alone (C at level 0); A and B take their
        // conductance's share of it and C's own buffer is the share it has when unmuted
        const double gOn = 1.0 / 251.0, gOff = 1.0 / 73770.0;
        const double abShare = (2 * gOn) / (2 * gOn + gOff);
        Check(std::fabs(fb[0][1] + fb[1][1] - Fixed(15, 15, 0) * abShare) < 1e-6, "muting channel C: channels A and B carry the table value of A and B alone (C at level 0), in proportion to their conductance");
        Check(fa[2][1] == fb[2][1], "channel C's buffer is unchanged");
        Check(fb[0][1] > fa[0][1] * 0.5f, "muting makes the output a different mix, not simply silent");
    }
}

void Sunsoft5bSampleCountChunksAndDeterminism() {
    std::printf("Sunsoft5bSampleCountChunksAndDeterminism\n");
    Chip chip;
    Channels out;
    Check(chip.Render(15, out) == 1 && chip.GeneratedSampleCountForTest() == 1, "15 cycles make 1 sample (the first sample is at cycle 0, before the current time)");
    Check(chip.Render(1, out) == 0, "16 cycles are still 1 sample");
    Check(chip.Render(1, out) == 1, "17 cycles make 2");
    Check(chip.Render(31, out) == 1 && chip.GeneratedSampleCountForTest() == 3, "48 cycles make 3 samples (ceil(48 / 16))");
    Check(out[0].size() == 3 && out[1].size() == 3 && out[2].size() == 3, "every channel gets every sample");
    Check(chip.Render(0, out) == 0, "no cycles, no samples");

    auto program = [](Chip& c) {
        Poke(c, 0, 9);
        Poke(c, 2, 13);
        Poke(c, 6, 5);
        Poke(c, 7, 0x10);
        Poke(c, 8, 0x1F);
        Poke(c, 9, 12);
        Poke(c, 10, 6);
        Poke(c, 11, 2);
        Poke(c, 13, 0x0E);
    };
    Chip one, many, again;
    program(one);
    program(many);
    program(again);
    Channels a, b, c;
    one.Render(16 * 5000 + 7, a);
    std::uint64_t left = 16 * 5000 + 7;
    std::uint64_t step = 1;
    while (left > 0) {
        const std::uint64_t n = std::min(left, step);
        many.Render(n, b);
        left -= n;
        step = step * 3 % 97 + 1;
    }
    again.Render(16 * 5000 + 7, c);
    bool same = a[0].size() == b[0].size();
    bool deterministic = a[0].size() == c[0].size();
    for (std::size_t ch = 0; ch < 3 && same; ++ch)
        for (std::size_t i = 0; i < a[ch].size(); ++i) {
            if (a[ch][i] != b[ch][i]) same = false;
            if (a[ch][i] != c[ch][i]) deterministic = false;
        }
    Check(same, "the same cycles in uneven chunks make exactly the same samples as one call");
    Check(deterministic, "two chips with the same input make the same output");
}

namespace {

struct Write {
    std::uint64_t cycle;
    char kind;
    std::uint8_t value;
};

bool ReadWrites(const std::filesystem::path& path, std::vector<Write>& out) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        unsigned long long cycle = 0;
        char kind = 0;
        unsigned value = 0;
        std::istringstream fields(line);
        fields >> cycle >> kind >> value;
        if (!fields.fail()) out.push_back({cycle, kind, static_cast<std::uint8_t>(value)});
    }
    return true;
}

bool ReadFloats(const std::filesystem::path& path, std::vector<float>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    in.seekg(0);
    out.resize(static_cast<std::size_t>(size) / sizeof(float));
    in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size() * sizeof(float)));
    return true;
}

}  // namespace

void Sunsoft5bReplayMatchesTheRecordedStreams() {
    std::printf("Sunsoft5bReplayMatchesTheRecordedStreams\n");
    const char* referenceRoot = GetEnvVar("CHIPESTRA_REFERENCE_DIR");
    if (referenceRoot == nullptr || referenceRoot[0] == '\0') {
        std::printf("  SKIP: CHIPESTRA_REFERENCE_DIR is not set - the recordings of the original emulator are not available\n");
        return;
    }
    // One recording per sound request of a real game (songs, jingles, effects), each from power-on: every write to
    // the chip with its CPU cycle, and the original emulator's own 5B stream (one float per sample, 111860.75 Hz).
    const std::filesystem::path dir = std::filesystem::path(referenceRoot) / "nes" / "gimmick" / "s5b";
    const char* const requests[] = {"06", "3D", "00", "2C", "43", "18", "59", "6E", "10", "0D", "2E", "5E", "3A", "63", "FF"};
    std::size_t compared = 0;
    for (const char* name : requests) {
        std::vector<Write> writes;
        std::vector<float> mame;
        if (!ReadWrites(dir / (std::string(name) + "_writes.txt"), writes) || !ReadFloats(dir / (std::string(name) + "_stream.f32"), mame)) {
            std::printf("  SKIP: the recording %s is not in CHIPESTRA_REFERENCE_DIR\\nes\\gimmick\\s5b\n", name);
            continue;
        }
        Chip chip;
        Channels out;
        // The original's stream runs at an integer rate (the CPU clock / 16 cut down to 111860 Hz), so the samples made
        // before a write at CPU cycle c are ceil(c * 111860 / 1789772).
        auto samplesBefore = [&](std::uint64_t c) { return Chip::SamplesBeforeCycle(c); };
        for (const Write& w : writes) {
            chip.RenderToSampleCount(samplesBefore(w.cycle), out);
            if (w.kind == 'D') chip.WriteData(w.value);
            else chip.WriteAddress(w.value);
        }
        chip.RenderToSampleCount(mame.size(), out);
        std::size_t differing = 0, nonzero = 0;
        double worst = 0.0;
        for (std::size_t i = 0; i < mame.size(); ++i) {
            if (mame[i] > 0.01f) ++nonzero;
            const double diff = std::fabs(Sum(out, i) - static_cast<double>(mame[i]));
            worst = std::max(worst, diff);
            if (diff > 3e-7) ++differing;
        }
        char label[200];
        std::snprintf(label, sizeof(label), "request %s: all %zu samples (%zu above 0.01) equal the original's stream (largest difference %.2e)", name, mame.size(), nonzero, worst);
        Check(differing == 0, label);
        ++compared;
    }
    if (compared > 0) std::printf("  %zu recordings compared\n", compared);
}
