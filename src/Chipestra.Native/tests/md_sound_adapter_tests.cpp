// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Mega Drive sound chips (Ym2612DeviceAdapter, Sn76489SoundDeviceAdapter) native tests.

#include "md_sound_adapter_tests.h"

#include "devices/sound/sn76489/sn76489_sound_device_adapter.h"
#include "devices/sound/ym2612/ym2612_device_adapter.h"
#include "md_reference_log.h"
#include "test_support.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using Ym = Ym2612DeviceAdapter;
using Psg = Sn76489SoundDeviceAdapter;

void YmReg(Ym& chip, std::uint8_t reg, std::uint8_t value, bool upper = false) {
    chip.Write(upper ? 2 : 0, reg);
    chip.Write(upper ? 3 : 1, value);
}

// Channel 1, operator 4 (slot 3) as the only audible one: algorithm 7 so every operator reaches the output.
void YmSetUpTone(Ym& chip, bool upper, unsigned channelInBank, std::uint8_t pan) {
    const std::uint8_t base = static_cast<std::uint8_t>(channelInBank);
    for (std::uint8_t op = 0; op < 4; ++op) {
        const std::uint8_t offset = static_cast<std::uint8_t>(base + op * 4);
        YmReg(chip, static_cast<std::uint8_t>(0x30 + offset), 0x01, upper);  // DT/MUL
        YmReg(chip, static_cast<std::uint8_t>(0x40 + offset), 0x10, upper);  // TL
        YmReg(chip, static_cast<std::uint8_t>(0x50 + offset), 0x1f, upper);  // KS/AR
        YmReg(chip, static_cast<std::uint8_t>(0x60 + offset), 0x00, upper);  // DR
        YmReg(chip, static_cast<std::uint8_t>(0x70 + offset), 0x00, upper);
        YmReg(chip, static_cast<std::uint8_t>(0x80 + offset), 0x0f, upper);  // SL/RR
    }
    YmReg(chip, static_cast<std::uint8_t>(0xb0 + base), 0x07, upper);        // algorithm 7
    YmReg(chip, static_cast<std::uint8_t>(0xb4 + base), pan, upper);
    YmReg(chip, static_cast<std::uint8_t>(0xa4 + base), 0x22, upper);        // block/F-number high
    YmReg(chip, static_cast<std::uint8_t>(0xa0 + base), 0x69, upper);
}

void YmKeyOn(Ym& chip, std::uint8_t channelSelect) {
    YmReg(chip, 0x28, static_cast<std::uint8_t>(0xf0 | channelSelect));
}

}  // namespace

void Ym2612RegistersTimersAndDac() {
    std::printf("Ym2612RegistersTimersAndDac\n");
    {
        Ym chip;
        // Timer A with the shortest period, loaded and flagged: the status flag rises after some chip clocks.
        YmReg(chip, 0x24, 0xff);
        YmReg(chip, 0x25, 0x03);
        YmReg(chip, 0x27, 0x15);
        Check((chip.ReadStatus() & 0x01) == 0, "Timer A has not overflowed before any clock has passed");
        std::vector<float> l, r;
        chip.RenderStereo(Ym::kInputClocksPerSample * 400, l, r);
        Check((chip.ReadStatus() & 0x01) != 0, "Timer A overflows and sets its status flag");
        YmReg(chip, 0x27, 0x15 | 0x10);
        Check((chip.ReadStatus() & 0x01) == 0, "writing the flag-reset bit clears the Timer A flag");
    }
    {
        // DAC: enabled with both outputs on, the DAC channel carries the written level; disabled it carries none.
        Ym chip;
        YmReg(chip, 0xb6, 0xc0, true);
        YmReg(chip, 0x2b, 0x80);
        YmReg(chip, 0x2a, 0xff);
        std::vector<std::vector<float>> ch(Ym::kChannelCount);
        chip.Render(Ym::kInputClocksPerSample * 8, ch);
        const float high = ch[Ym::kDacChannel].back();
        YmReg(chip, 0x2a, 0x00);
        for (auto& c : ch) c.clear();
        chip.Render(Ym::kInputClocksPerSample * 8, ch);
        const float low = ch[Ym::kDacChannel].back();
        Check(high > 0.0f && low < 0.0f && high - low > 0.1f, "the DAC level follows the 0x2A writes (high is above low)");
        YmReg(chip, 0x2b, 0x00);
        for (auto& c : ch) c.clear();
        chip.Render(Ym::kInputClocksPerSample * 8, ch);
        Check(std::fabs(ch[Ym::kDacChannel].back()) < 1e-3f, "with the DAC disabled its channel is silent");
        Check(chip.ChannelOutputsLeft(Ym::kDacChannel) && chip.ChannelOutputsRight(Ym::kDacChannel),
              "the DAC pan bits are read from register 0xB6");
    }
    {
        Ym chip;
        YmReg(chip, 0xb4, 0x80);
        YmReg(chip, 0xb5, 0x40);
        Check(chip.ChannelOutputsLeft(0) && !chip.ChannelOutputsRight(0), "FM 1 pan: left only");
        Check(!chip.ChannelOutputsLeft(1) && chip.ChannelOutputsRight(1), "FM 2 pan: right only");
    }
}

void Ym2612ChannelsAddUpToTheChipOutput() {
    std::printf("Ym2612ChannelsAddUpToTheChipOutput\n");
    Ym a;
    Ym b;
    for (Ym* chip : {&a, &b}) {
        YmSetUpTone(*chip, false, 0, 0xc0);
        YmSetUpTone(*chip, true, 1, 0xc0);
        YmReg(*chip, 0x2b, 0x80);
        YmReg(*chip, 0xb6, 0xc0, true);
        YmReg(*chip, 0x2a, 0xc0);
        YmKeyOn(*chip, 0);
        YmKeyOn(*chip, 5);
    }
    std::vector<std::vector<float>> ch(Ym::kChannelCount);
    std::vector<float> l, r;
    a.Render(Ym::kInputClocksPerSample * 3000, ch);
    b.RenderStereo(Ym::kInputClocksPerSample * 3000, l, r);
    double worst = 0.0;
    double peak = 0.0;
    for (std::size_t i = 0; i < l.size(); ++i) {
        double left = 0.0, right = 0.0;
        for (std::uint32_t c = 0; c < Ym::kChannelCount; ++c) {
            // Every voice here is on both buses, so a channel carries the same level on each.
            const bool lo = a.ChannelOutputsLeft(c);
            const bool ro = a.ChannelOutputsRight(c);
            if (lo) left += ch[c][i];
            if (ro) right += ch[c][i];
        }
        worst = std::max({worst, std::fabs(left - l[i]), std::fabs(right - r[i])});
        peak = std::max(peak, std::fabs(static_cast<double>(l[i])));
    }
    Check(peak > 0.01, "the test voices are audible");
    Check(worst < 4e-5, "the channels' left and right outputs add up to the chip's own stereo output (within one 16-bit step)");
}

void Ym2612ChunkingAndDeterminism() {
    std::printf("Ym2612ChunkingAndDeterminism\n");
    Ym one;
    Ym many;
    for (Ym* chip : {&one, &many}) {
        YmSetUpTone(*chip, false, 2, 0xc0);
        YmKeyOn(*chip, 2);
    }
    std::vector<float> a_l, a_r, b_l, b_r;
    const std::uint64_t total = Ym::kInputClocksPerSample * 2000 + 77;
    one.RenderStereo(total, a_l, a_r);
    std::uint64_t left = total;
    std::uint64_t step = 1;
    while (left > 0) {
        const std::uint64_t n = std::min(left, step);
        many.RenderStereo(n, b_l, b_r);
        left -= n;
        step = step * 5 % 211 + 1;
    }
    Check(a_l == b_l && a_r == b_r && a_l.size() == 2000, "uneven chunks make exactly the same samples as one call");
}

namespace {

// Number of YM samples the original has made when a Z80 write at cycle c reaches the chip: the stream is brought
// up to the write's time, and the sample that is being made at that moment is already counted.
std::uint64_t YmSamplesBefore(std::uint64_t cycle) {
    constexpr std::uint64_t kRate = 53267;   // the original's stream rate (integer)
    constexpr std::uint64_t kZ80 = 3579545;
    return cycle * kRate / kZ80 + 1;
}

}  // namespace

void Ym2612ReplayMatchesTheRecordedStreams() {
    std::printf("Ym2612ReplayMatchesTheRecordedStreams\n");
    std::vector<std::string> names;
    if (!MdReferenceNames(names)) {
        std::printf("  SKIP: CHIPESTRA_REFERENCE_DIR is not set - the recordings of the original emulator are not available\n");
        return;
    }
    std::size_t compared = 0;
    for (const std::string& name : names) {
        MdReferenceLog log;
        if (!MdReadReferenceLog(name, log)) continue;
        {
            Ym chip;
            std::vector<float> l, r;
            std::uint64_t made = 0;
            for (const MdWrite& w : log.writes) {
                if (w.kind == MdWrite::Reset) {
                    // The chip is reset with the Z80.
                    if (w.value == 0) chip.SoftReset();
                    continue;
                }
                if (w.kind != MdWrite::Ym) continue;
                const std::uint64_t target = YmSamplesBefore(w.cycle);
                if (target > made) {
                    chip.RenderStereo((target - made) * Ym::kInputClocksPerSample, l, r);
                    made = target;
                }
                chip.Write(w.port, w.value);
            }
            const std::uint64_t end = log.ym_start + log.ym.size() / 2;
            if (end > made) {
                chip.RenderStereo((end - made) * Ym::kInputClocksPerSample, l, r);
                made = end;
            }
            std::size_t differing = 0;
            std::size_t nonzero = 0;
            double worst = 0.0;
            for (std::size_t i = 0; i < log.ym.size() / 2; ++i) {
                const std::size_t k = static_cast<std::size_t>(log.ym_start) + i;
                const double dl = std::fabs(static_cast<double>(l[k]) - log.ym[2 * i]);
                const double dr = std::fabs(static_cast<double>(r[k]) - log.ym[2 * i + 1]);
                worst = std::max({worst, dl, dr});
                if (std::fabs(log.ym[2 * i]) > 1e-4f) ++nonzero;
                if (dl > 1e-7 || dr > 1e-7) ++differing;
            }
            char label[220];
            std::snprintf(label, sizeof(label), "%s: all %zu YM2612 samples (%zu audible) equal the original's stream (largest difference %.2e)",
                          name.c_str(), log.ym.size() / 2, nonzero, worst);
            Check(differing == 0, label);
            ++compared;
        }
    }
    if (compared > 0) std::printf("  %zu recordings compared\n", compared);
}

void Sn76489TonesVolumesAndNoise() {
    std::printf("Sn76489TonesVolumesAndNoise\n");
    {
        Psg chip;
        chip.Write(0x84);  // tone 1 period low = 4
        chip.Write(0x00);  // high bits 0 -> period 4
        chip.Write(0x90);  // tone 1 volume 0 (loudest)
        std::vector<std::vector<float>> ch(Psg::kChannelCount);
        chip.Render(2 * 8 * 4 * 40, ch);  // 40 half-periods
        // The output toggles every period (4) generator steps = 32 samples.
        std::size_t toggles = 0;
        std::size_t firstToggle = 0;
        for (std::size_t i = 1; i < ch[0].size(); ++i) {
            if (ch[0][i] != ch[0][i - 1]) {
                if (toggles == 0) firstToggle = i;
                ++toggles;
            }
        }
        Check(toggles >= 38 && toggles <= 41, "tone 1 with period 4 toggles every 32 samples");
        (void)firstToggle;
        float peak = 0.0f;
        for (float v : ch[0]) peak = std::max(peak, std::fabs(v));
        Check(std::fabs(peak - 8191.0f / 32768.0f) < 1e-6f, "volume 0 makes a level of a quarter of the range");
        Check(ch[1][100] == 0.0f && ch[2][100] == 0.0f && ch[3][100] == 0.0f, "the other channels stay silent");
    }
    {
        Psg chip;
        chip.Write(0x84);
        chip.Write(0x00);
        chip.Write(0x9f);  // volume 15 = off
        std::vector<std::vector<float>> ch(Psg::kChannelCount);
        chip.Render(2 * 8 * 100, ch);
        float peak = 0.0f;
        for (float v : ch[0]) peak = std::max(peak, std::fabs(v));
        Check(peak == 0.0f, "volume 15 is silent");
    }
    {
        // Volume steps are 2 dB.
        Psg chip;
        chip.Write(0x84);
        chip.Write(0x00);
        float level[16] = {};
        for (int v = 0; v < 16; ++v) {
            chip.Write(static_cast<std::uint8_t>(0x90 | v));
            std::vector<std::vector<float>> ch(Psg::kChannelCount);
            chip.Render(2 * 8 * 8, ch);
            float peak = 0.0f;
            for (float x : ch[0]) peak = std::max(peak, std::fabs(x));
            level[v] = peak;
        }
        const double ratio = static_cast<double>(level[1]) / level[0];
        Check(std::fabs(ratio - 1.0 / 1.258925412) < 2e-3 && level[15] == 0.0f, "volume steps are 2 dB down");
    }
    {
        // Noise: white noise with the shift rate fixed; the pattern repeats only after 65535 steps, so over a
        // short run it has both polarities, and rewriting register 6 restarts it.
        Psg chip;
        chip.Write(0xe4);  // noise: white, rate follows N/1024... (bit 2 = white)
        chip.Write(0xf0);  // noise volume 0
        std::vector<std::vector<float>> a(Psg::kChannelCount), b(Psg::kChannelCount);
        chip.Render(2 * 8 * 200, a);
        chip.Write(0xe4);
        chip.Render(2 * 8 * 200, b);
        Check(a[3].size() == b[3].size() && !a[3].empty(), "noise runs");
        bool restarted = true;
        for (std::size_t i = 0; i < 2 * 8 * 40 && i < a[3].size(); ++i) {
            // 16 clocks per step, 2 per sample: after the restart the shift register is back at its start state.
            if (a[3][i] != b[3][i]) restarted = false;
        }
        Check(restarted, "writing the noise register restarts the shift register");
    }
}

void Sn76489ChunkingAndDeterminism() {
    std::printf("Sn76489ChunkingAndDeterminism\n");
    Psg one;
    Psg many;
    for (Psg* chip : {&one, &many}) {
        chip->Write(0x84);
        chip->Write(0x05);
        chip->Write(0x90);
        chip->Write(0xe5);
        chip->Write(0xf2);
    }
    std::vector<std::vector<float>> a(Psg::kChannelCount), b(Psg::kChannelCount);
    const std::uint64_t total = 2 * 7001 + 1;
    one.Render(total, a);
    std::uint64_t left = total;
    std::uint64_t step = 1;
    while (left > 0) {
        const std::uint64_t n = std::min(left, step);
        many.Render(n, b);
        left -= n;
        step = step * 7 % 53 + 1;
    }
    Check(a == b && a[0].size() == 7001, "uneven chunks make exactly the same samples as one call");
}

namespace {

// Number of PSG stream samples the original has made before a Z80 write at cycle c reaches the chip.
std::uint64_t PsgSamplesBefore(std::uint64_t cycle) {
    constexpr std::uint64_t kRate = 1789772;  // the original's stream rate (clock / 2, integer)
    constexpr std::uint64_t kZ80 = 3579545;
    return (cycle * kRate + kZ80 - 1) / kZ80;
}

}  // namespace

void Sn76489ReplayMatchesTheRecordedStreams() {
    std::printf("Sn76489ReplayMatchesTheRecordedStreams\n");
    std::vector<std::string> names;
    if (!MdReferenceNames(names)) {
        std::printf("  SKIP: CHIPESTRA_REFERENCE_DIR is not set - the recordings of the original emulator are not available\n");
        return;
    }
    std::size_t compared = 0;
    for (const std::string& name : names) {
        MdReferenceLog log;
        if (!MdReadReferenceLog(name, log) || log.psg.empty()) continue;
        {
            Psg chip;
            std::vector<std::vector<float>> out(Psg::kChannelCount);
            std::uint64_t made = 0;
            for (const MdWrite& w : log.writes) {
                if (w.kind != MdWrite::Psg) continue;
                const std::uint64_t target = PsgSamplesBefore(w.cycle);
                if (target > made) {
                    chip.Render((target - made) * Psg::kInputClocksPerSample, out);
                    made = target;
                }
                chip.Write(w.value);
            }
            const std::uint64_t end = log.psg_start + log.psg.size();
            if (end > made) {
                chip.Render((end - made) * Psg::kInputClocksPerSample, out);
            }
            std::size_t differing = 0;
            std::size_t nonzero = 0;
            double worst = 0.0;
            for (std::size_t i = 0; i < log.psg.size(); ++i) {
                const std::size_t k = static_cast<std::size_t>(log.psg_start) + i;
                const double mine = static_cast<double>(out[0][k]) + out[1][k] + out[2][k] + out[3][k];
                const double diff = std::fabs(mine - log.psg[i]);
                worst = std::max(worst, diff);
                if (std::fabs(log.psg[i]) > 1e-6f) ++nonzero;
                if (diff > 1e-7) ++differing;
            }
            char label[220];
            std::snprintf(label, sizeof(label), "%s: all %zu PSG samples (%zu audible) equal the original's stream (largest difference %.2e)",
                          name.c_str(), log.psg.size(), nonzero, worst);
            Check(differing == 0, label);
            ++compared;
        }
    }
    if (compared > 0) std::printf("  %zu recordings compared\n", compared);
}
