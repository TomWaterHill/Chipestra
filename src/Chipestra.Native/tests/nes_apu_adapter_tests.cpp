// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// NES APU (NesApuDeviceAdapter) native tests. All register sequences are hand-written. Expected values
// come from the documented behaviour of the chip (channel periods in CPU cycles, envelope and length
// counter rates, the 93-step and 32767-step noise sequences, the non-linear mixer formulas), worked out
// in this file, not read back from the adapter.
//
// Time units used below: one output sample = 4 CPU cycles; the frame period is 29830 CPU cycles
// (= 7457.5 samples), so the 240 Hz envelope step is 7457 "sync units" and the adapter counts them
// down by 4 per sample (about 1864 samples per envelope step).

#include "nes_apu_adapter_tests.h"

#include "devices/sound/nes_apu/nes_apu_device_adapter.h"
#include "test_support.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

namespace {

using Apu = NesApuDeviceAdapter;

struct Rig {
    Apu apu;
    std::array<std::vector<float>, Apu::kChannelCount> out;
    std::vector<std::pair<std::uint64_t, bool>> irq;  // (CPU cycle count, line state)
    std::vector<std::uint16_t> dmcReads;
    std::uint8_t dmcByte = 0xFF;
    std::uint64_t cycles = 0;

    explicit Rig(Apu::Variant variant = Apu::Variant::kRp2a03g) : apu(false, variant) {
        apu.SetIrqCallback([this](bool asserted) { irq.emplace_back(cycles, asserted); });
        apu.SetMemoryReadCallback([this](std::uint16_t address) -> std::uint8_t {
            dmcReads.push_back(address);
            return dmcByte;
        });
        irq.clear();  // drop the events of the constructor's own reset
    }

    void W(std::uint8_t reg, std::uint8_t value) { apu.Write(reg, value); }

    // One CPU cycle at a time, so IRQ events carry their exact cycle.
    void Cycles(std::uint64_t n) {
        for (std::uint64_t i = 0; i < n; ++i) {
            ++cycles;
            apu.Render(1, out);
        }
    }

    // n samples at once.
    void Samples(std::size_t n) {
        cycles += 4 * n;
        apu.Render(4 * n, out);
    }

    // Level of one channel after each of n further samples.
    std::vector<int> Levels(Apu::Channel ch, std::size_t n) {
        std::vector<int> v;
        v.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            Samples(1);
            v.push_back(apu.ChannelLevelForTest(ch));
        }
        return v;
    }

    float Mix(std::size_t i) const {
        float s = 0.0f;
        for (const auto& c : out) s += c[i];
        return s;
    }
};

std::vector<std::size_t> RisingEdges(const std::vector<int>& levels) {
    std::vector<std::size_t> e;
    for (std::size_t i = 1; i < levels.size(); ++i)
        if (levels[i] != 0 && levels[i - 1] == 0) e.push_back(i);
    return e;
}

int MaxIn(const std::vector<int>& v, std::size_t from, std::size_t count) {
    int m = 0;
    for (std::size_t i = from; i < from + count && i < v.size(); ++i) m = v[i] > m ? v[i] : m;
    return m;
}

// Pulse 1 with a constant volume and a halted length counter: $4000 = duty | halt | constant | volume.
void StartPulse1(Rig& r, int duty, int volume, std::uint16_t period) {
    r.W(0x15, 0x01);
    r.W(0x00, static_cast<std::uint8_t>((duty << 6) | 0x30 | volume));
    r.W(0x01, 0x00);
    r.W(0x02, static_cast<std::uint8_t>(period & 0xFF));
    r.W(0x03, static_cast<std::uint8_t>(0x08 | ((period >> 8) & 7)));  // length index 1 (254), high bits of the period
}

}  // namespace

void NesApuResetState() {
    std::printf("NesApuResetState\n");
    Rig r;
    Check(r.apu.ReadStatus() == 0, "after reset no channel has a length and no IRQ flag is set");
    Check(r.apu.FrameIrqEnabledForTest() && !r.apu.FrameIrqPendingForTest(), "after reset the frame IRQ is enabled and not yet raised");
    Check(r.apu.FrameCountdownForTest() == 29830, "after reset the frame timer is armed for one NTSC frame period (29830 cycles)");
    r.Samples(10);
    bool silent = true;
    for (const auto& c : r.out)
        for (float v : c) silent = silent && v == 0.0f;
    Check(silent, "a freshly reset chip renders silence");
    Check(r.out[0].size() == 10, "10 samples were rendered per channel");
}

void NesApuPulseFrequencyAndDutyCycles() {
    std::printf("NesApuPulseFrequencyAndDutyCycles\n");
    const int expectedHighOf8[4] = {1, 2, 4, 6};  // 12.5%, 25%, 50%, 25% negated
    int badEdge = 0, badDuty = 0;
    for (int duty = 0; duty < 4; ++duty) {
        Rig r;
        StartPulse1(r, duty, 15, 15);  // period register 15: the 16-step cycle lasts 16 * 16 = 256 CPU cycles = 64 samples
        r.Samples(8);                  // settle
        const std::vector<int> lv = r.Levels(Apu::kPulse1, 64 * 32);
        const std::vector<std::size_t> edges = RisingEdges(lv);
        if (edges.size() < 30) ++badEdge;
        for (std::size_t i = 1; i < edges.size(); ++i)
            if (edges[i] - edges[i - 1] != 64) ++badEdge;
        std::size_t high = 0;
        for (int v : lv) high += v != 0 ? 1 : 0;
        if (high != lv.size() * static_cast<std::size_t>(expectedHighOf8[duty]) / 8) ++badDuty;
        for (int v : lv)
            if (v != 0 && v != 15) ++badDuty;
    }
    Check(badEdge == 0, "pulse period register 15 gives a rising edge exactly every 64 samples (256 CPU cycles) for all four duties");
    Check(badDuty == 0, "the four duty settings are high for 1/8, 2/8, 4/8 and 6/8 of the period at the programmed volume");
}

void NesApuPulseEnvelopeDecayAndLoop() {
    std::printf("NesApuPulseEnvelopeDecayAndLoop\n");
    {
        Rig r;  // envelope on (constant-volume and hold bits clear), envelope period 0, duty 50%
        r.W(0x15, 0x01);
        r.W(0x00, 0x80);
        r.W(0x02, 15);
        r.W(0x03, 0x08);
        const std::vector<int> lv = r.Levels(Apu::kPulse1, 28200);
        // The first envelope step happens on the first sample; later ones every 7457/4 = 1864.25 samples.
        Check(MaxIn(lv, 200, 64) == 14, "envelope: the first step lowers the level to 14 right away");
        Check(MaxIn(lv, 5000, 64) == 12, "envelope: after three steps (about 5000 samples) the level is 12");
        Check(MaxIn(lv, 27000, 64) == 0, "envelope: after fifteen steps the channel is silent");
        bool mono = true;
        int prev = 15;
        for (std::size_t w = 0; w + 64 <= lv.size(); w += 64) {
            const int m = MaxIn(lv, w, 64);
            if (m > prev) mono = false;
            prev = m;
        }
        Check(mono, "envelope: the level never rises while the envelope decays");
    }
    {
        Rig r;  // hold/loop bit set: the envelope wraps around
        r.W(0x15, 0x01);
        r.W(0x00, 0xA0);
        r.W(0x02, 15);
        r.W(0x03, 0x08);
        const std::vector<int> lv = r.Levels(Apu::kPulse1, 28300);
        Check(MaxIn(lv, 27000, 64) == 0 && MaxIn(lv, 28100, 64) == 15, "looping envelope: silent after the fifteenth step, back at full level after the sixteenth");
    }
}

void NesApuPulseLengthCounterAndHalt() {
    std::printf("NesApuPulseLengthCounterAndHalt\n");
    {
        Rig r;  // constant volume, no halt, length index 3 (value 2 = 7457 samples)
        r.W(0x15, 0x01);
        r.W(0x00, 0x9F);
        r.W(0x02, 15);
        r.W(0x03, 0x18);
        Check((r.apu.ReadStatus() & 0x01) != 0, "writing $4003 loads the length counter (status bit 0 set)");
        const std::vector<int> lv = r.Levels(Apu::kPulse1, 8000);
        Check(MaxIn(lv, 7000, 400) == 15, "length counter: still sounding shortly before it runs out");
        Check(MaxIn(lv, 7458, 500) == 0, "length counter: silent once 7457 samples have passed");
        Check((r.apu.ReadStatus() & 0x01) == 0, "status bit 0 clears when the length counter reaches zero");
    }
    {
        Rig r;  // same with the halt bit: never expires
        r.W(0x15, 0x01);
        r.W(0x00, 0xBF);
        r.W(0x02, 15);
        r.W(0x03, 0x18);
        const std::vector<int> lv = r.Levels(Apu::kPulse1, 9000);
        Check(MaxIn(lv, 8000, 900) == 15 && (r.apu.ReadStatus() & 0x01) != 0, "halt bit: the length counter does not run down");
    }
    {
        Rig r;  // a channel that is disabled in $4015 ignores the length load
        r.W(0x00, 0x9F);
        r.W(0x02, 15);
        r.W(0x03, 0x18);
        Check((r.apu.ReadStatus() & 0x01) == 0, "a disabled pulse channel does not load its length counter");
        r.Samples(100);
        Check(r.apu.ChannelLevelForTest(Apu::kPulse1) == 0, "a disabled pulse channel is silent");
    }
}

void NesApuPulseSweep() {
    std::printf("NesApuPulseSweep\n");
    {
        // Sweep down (negate), shift 1, sweep period 0: the frequency value halves at the first sample and
        // then every 7457/2 = 3728.5 samples. The period register starts at 255 (frequency value 256).
        Rig r;
        r.W(0x15, 0x01);
        r.W(0x00, 0xBF);
        r.W(0x01, 0x89);
        r.W(0x02, 0xFF);
        r.W(0x03, 0x08);
        const std::vector<int> lv = r.Levels(Apu::kPulse1, 24000);
        auto spacing = [&](std::size_t from, std::size_t to) {
            std::vector<int> part(lv.begin() + static_cast<std::ptrdiff_t>(from), lv.begin() + static_cast<std::ptrdiff_t>(to));
            const auto e = RisingEdges(part);
            std::vector<std::size_t> d;
            for (std::size_t i = 1; i < e.size(); ++i) d.push_back(e[i] - e[i - 1]);
            return d;
        };
        const auto s1 = spacing(1500, 3500);   // value 128: 16 * 128 cycles = 512 samples per period
        const auto s2 = spacing(5000, 7000);   // value 64: 256 samples
        const auto s3 = spacing(8500, 10500);  // value 32: 128 samples
        bool ok1 = !s1.empty(), ok2 = !s2.empty(), ok3 = !s3.empty();
        for (auto d : s1) ok1 = ok1 && d == 512;
        for (auto d : s2) ok2 = ok2 && d == 256;
        for (auto d : s3) ok3 = ok3 && d == 128;
        Check(ok1 && ok2 && ok3, "sweep down halves the frequency value step by step: the period goes 512, 256, 128 samples");
        Check(MaxIn(lv, 19000, 300) == 15, "sweep down: still sounding while the value is 4");
        Check(MaxIn(lv, 23000, 900) == 0, "sweep down: the channel is silenced once the value falls below 4");
    }
    {
        // Sweep up past the limit for shift 1 (0x555): 1281 + 640 > 1365 at the first sweep step.
        Rig r;
        r.W(0x15, 0x01);
        r.W(0x00, 0xBF);
        r.W(0x01, 0x81);
        r.W(0x02, 0x00);
        r.W(0x03, 0x0D);
        const std::vector<int> lv = r.Levels(Apu::kPulse1, 2000);
        Check(MaxIn(lv, 0, 2000) == 0, "sweep up past the frequency limit silences the channel");
    }
    {
        Rig r;  // a frequency value below 4 is silent even without a sweep
        StartPulse1(r, 2, 15, 2);
        const std::vector<int> lv = r.Levels(Apu::kPulse1, 500);
        Check(MaxIn(lv, 0, 500) == 0, "a frequency value below 4 is silent");
    }
}

void NesApuTriangleWaveformAndLinearCounter() {
    std::printf("NesApuTriangleWaveformAndLinearCounter\n");
    {
        Rig r;  // halt/control set: both counters hold, so the channel plays on
        r.W(0x15, 0x04);
        r.W(0x08, 0xFF);
        r.W(0x0A, 7);     // frequency value 8: one of the 32 steps every 8 CPU cycles = every 2 samples
        r.W(0x0B, 0x08);
        const std::vector<int> lv = r.Levels(Apu::kTriangle, 200);
        auto model = [](int step) {  // the 32-step triangle: 15, 14, ... 0, 0, 1, ... 15
            const int s = step % 32;
            return s < 16 ? 15 - s : s - 16;
        };
        int bad = 0;
        for (std::size_t k = 1; k <= lv.size(); ++k) {
            const int step = static_cast<int>((k + 1) / 2);  // the step advances on odd samples
            if (lv[k - 1] != model(step)) ++bad;
        }
        Check(bad == 0, "triangle: the 32-step waveform runs down 15..0 and up 0..15, advancing every 8 CPU cycles");
    }
    {
        // Linear counter without the control bit: reload value 4, counted in 7457 sync units of 4 per sample.
        Rig r;
        r.W(0x15, 0x04);
        r.W(0x08, 0x04);
        r.W(0x0A, 7);
        r.W(0x0B, 0x08);
        const std::vector<int> lv = r.Levels(Apu::kTriangle, 9000);
        bool changingEarly = false, frozenLate = true;
        for (std::size_t i = 7000; i < 7100; ++i) changingEarly = changingEarly || lv[i] != lv[i - 1];
        for (std::size_t i = 8500; i < 9000; ++i) frozenLate = frozenLate && lv[i] == lv[i - 1];
        Check(changingEarly, "triangle with a running linear counter keeps moving");
        Check(frozenLate, "triangle stops moving once the linear counter has run down");
    }
    {
        Rig r;  // frequency value 1 is ultrasonic: the generator halts
        r.W(0x15, 0x04);
        r.W(0x08, 0xFF);
        r.W(0x0A, 0);
        r.W(0x0B, 0x08);
        const std::vector<int> lv = r.Levels(Apu::kTriangle, 200);
        Check(MaxIn(lv, 0, 200) == 0, "triangle with frequency value 1 produces nothing");
    }
}

void NesApuNoiseSequences() {
    std::printf("NesApuNoiseSequences\n");
    auto reference = [](bool shortMode, int n) {  // the 15-bit shift register with feedback from bit 0 xor bit 1 (or bit 6)
        std::uint16_t reg = 1;
        std::vector<int> v;
        for (int i = 0; i < n; ++i) {
            const unsigned tap = shortMode ? 6u : 1u;
            reg = static_cast<std::uint16_t>(reg | (((reg ^ (reg >> tap)) & 1u) << 15));
            reg = static_cast<std::uint16_t>(reg >> 1);
            v.push_back((reg & 1u) ? 0 : 15);
        }
        return v;
    };
    for (int mode = 0; mode < 2; ++mode) {
        Rig r;  // period index 0: the register is clocked once per sample
        r.W(0x15, 0x08);
        r.W(0x0C, 0x3F);
        r.W(0x0E, static_cast<std::uint8_t>(mode ? 0x80 : 0x00));
        r.W(0x0F, 0x08);
        const std::vector<int> got = r.Levels(Apu::kNoise, 3000);
        Check(got == reference(mode != 0, 3000), mode ? "noise short mode follows the 15-bit register with the bit-6 tap" : "noise long mode follows the 15-bit register with the bit-1 tap");
    }
    {
        Rig r;
        r.W(0x15, 0x08);
        r.W(0x0C, 0x3F);
        r.W(0x0E, 0x80);
        r.W(0x0F, 0x08);
        const std::vector<int> got = r.Levels(Apu::kNoise, 1200);
        auto periodic = [&](std::size_t p) {
            for (std::size_t i = 200; i < 1000; ++i)
                if (got[i] != got[i + p]) return false;
            return true;
        };
        bool notShorter = true;
        for (std::size_t p = 1; p < 93; ++p) notShorter = notShorter && !periodic(p);
        const bool period93 = periodic(93);
        Check(period93, "noise short mode repeats every 93 samples");
        Check(notShorter, "noise short mode has no shorter period");
    }
    {
        Rig r(Apu::Variant::kRp2a03);  // the plain RP2A03 ignores the mode bit
        r.W(0x15, 0x08);
        r.W(0x0C, 0x3F);
        r.W(0x0E, 0x80);
        r.W(0x0F, 0x08);
        const std::vector<int> got = r.Levels(Apu::kNoise, 3000);
        Check(got == reference(false, 3000), "the RP2A03 variant always runs the long sequence");
    }
    {
        Rig r;  // a lower noise rate clocks the register less often: index 4 = every 64 CPU cycles = 16 samples
        r.W(0x15, 0x08);
        r.W(0x0C, 0x3F);
        r.W(0x0E, 0x04);
        r.W(0x0F, 0x08);
        const std::vector<int> got = r.Levels(Apu::kNoise, 1600);
        const std::vector<int> ref = reference(false, 100);
        int bad = 0;
        for (std::size_t i = 0; i < 1600; ++i) {
            // the first update happens on the first sample, then one every 16 samples
            const std::size_t k = i / 16;
            if (got[i] != ref[k]) ++bad;
        }
        Check(bad == 0, "noise period index 4 clocks the register every 16 samples");
    }
    {
        Rig r;  // envelope mode: the noise level is bounded by the decaying envelope
        r.W(0x15, 0x08);
        r.W(0x0C, 0x00);
        r.W(0x0E, 0x00);
        r.W(0x0F, 0x08);
        const std::vector<int> got = r.Levels(Apu::kNoise, 3000);
        int maxEarly = 0, maxLate = 0;
        for (std::size_t i = 0; i < 1800; ++i) maxEarly = got[i] > maxEarly ? got[i] : maxEarly;
        for (std::size_t i = 1900; i < 3000; ++i) maxLate = got[i] > maxLate ? got[i] : maxLate;
        Check(maxEarly == 14 && maxLate == 13, "noise envelope: 14 during the first step, 13 during the second");
    }
}

void NesApuStatusRegisterAndChannelEnable() {
    std::printf("NesApuStatusRegisterAndChannelEnable\n");
    Rig r;
    r.W(0x15, 0x0F);
    r.W(0x03, 0x18);  // pulse 1 length 7457 samples
    r.W(0x07, 0x18);  // pulse 2
    r.W(0x0B, 0x18);  // triangle
    r.W(0x0F, 0x18);  // noise
    Check((r.apu.ReadStatus() & 0x0F) == 0x0F, "status bits 0-3 show the four channels with a length");
    r.W(0x15, 0x05);  // keep pulse 1 and the triangle
    Check((r.apu.ReadStatus() & 0x0F) == 0x05, "disabling a channel in $4015 clears its length at once");
    r.W(0x15, 0x00);
    Check((r.apu.ReadStatus() & 0x0F) == 0x00, "writing 0 to $4015 clears every length");
    r.W(0x0B, 0x18);
    Check((r.apu.ReadStatus() & 0x04) == 0, "a disabled triangle ignores its length write");
}

void NesApuFrameCounterAndIrq() {
    std::printf("NesApuFrameCounterAndIrq\n");
    {
        Rig r;
        r.Cycles(29829);
        Check(r.irq.empty() && !r.apu.FrameIrqPendingForTest(), "no frame IRQ before the end of the first frame period");
        r.Cycles(1);
        Check(r.irq.size() == 1 && r.irq[0].first == 29830 && r.irq[0].second, "the frame IRQ line is asserted exactly 29830 cycles after reset");
        r.Cycles(29830);
        Check(r.irq.size() == 2 && r.irq[1].first == 59660, "the frame timer repeats every 29830 cycles");
        const std::uint8_t st = r.apu.ReadStatus();
        Check((st & 0x40) != 0, "$4015 reports the frame IRQ flag");
        Check(!r.irq.back().second && !r.apu.FrameIrqPendingForTest(), "reading $4015 clears the flag and releases the IRQ line");
        Check((r.apu.ReadStatus() & 0x40) == 0, "a second read no longer shows the flag");
    }
    {
        Rig r;
        r.Cycles(100);
        r.W(0x17, 0x40);  // inhibit
        Check(!r.apu.FrameIrqEnabledForTest(), "$4017 bit 6 disables the frame IRQ");
        r.Cycles(100000);
        Check(r.irq.size() == 1 && !r.irq[0].second, "with the IRQ inhibited the timer never raises it (only the release at the write is seen)");
        r.W(0x17, 0x00);  // enable again: the timer restarts from this write
        const std::uint64_t at = r.cycles;
        r.Cycles(29829);
        const std::size_t before = r.irq.size();
        r.Cycles(1);
        Check(r.irq.size() == before + 1 && r.irq.back().first == at + 29830 && r.irq.back().second,
              "re-enabling restarts the frame timer: the IRQ comes 29830 cycles after the write");
    }
    {
        Rig r;
        r.W(0x17, 0x80);  // 5-step mode: no frame IRQ
        r.Cycles(30000);
        Check(!r.apu.FrameIrqPendingForTest(), "in 5-step mode the frame timer raises no IRQ");
    }
    {
        Rig r;
        r.Cycles(29830);
        r.W(0x15, 0x00);  // writing $4015 does not release a pending frame IRQ
        Check(r.apu.FrameIrqPendingForTest() && r.irq.size() == 1, "a $4015 write leaves a pending frame IRQ alone");
    }
}

void NesApuDmcReadsBytesAndRaisesIrq() {
    std::printf("NesApuDmcReadsBytesAndRaisesIrq\n");
    {
        Rig r;  // rate index 15 (54 cycles per bit), one byte long, sample at $C000
        r.dmcByte = 0xFF;
        r.W(0x10, 0x0F);
        r.W(0x11, 0x40);
        r.W(0x12, 0x00);
        r.W(0x13, 0x00);
        r.W(0x15, 0x10);
        Check((r.apu.ReadStatus() & 0x10) != 0, "enabling the DMC starts a sample (status bit 4)");
        r.Samples(200);
        Check(r.dmcReads == std::vector<std::uint16_t>{0xC000}, "a one-byte sample reads exactly one byte, from $C000");
        Check((r.apu.ReadStatus() & 0x10) == 0 && !r.apu.DmcEnabledForTest(), "the DMC stops when the sample is finished");
        Check(r.apu.ChannelLevelForTest(Apu::kDmc) == 80, "the DAC moved up by 2 for each of the eight 1 bits of the sample byte");
    }
    {
        Rig r;  // $4012 selects the start address in 64-byte units, $4013 the length in 16-byte units
        r.W(0x10, 0x0F);
        r.W(0x12, 0x01);
        r.W(0x13, 0x01);
        r.W(0x15, 0x10);
        r.Samples(2000);
        bool ok = r.dmcReads.size() == 17;
        for (std::size_t i = 0; ok && i < r.dmcReads.size(); ++i) ok = r.dmcReads[i] == 0xC040 + i;
        Check(ok, "length register 1 means 17 bytes, read from consecutive addresses starting at $C040");
    }
    {
        Rig r;  // IRQ at the end
        r.W(0x10, 0x8F);
        r.W(0x13, 0x00);
        r.W(0x15, 0x10);
        r.Samples(200);
        Check(r.apu.DmcIrqPendingForTest() && !r.irq.empty() && r.irq.back().second, "the DMC raises its IRQ when the sample ends");
        Check((r.apu.ReadStatus() & 0x80) != 0 && r.apu.DmcIrqPendingForTest(), "$4015 shows the DMC IRQ flag and a read does not clear it");
        r.W(0x10, 0x0F);  // clearing the IRQ-enable bit clears the flag
        Check(!r.apu.DmcIrqPendingForTest() && !r.irq.back().second, "clearing the DMC IRQ-enable bit releases the flag and the line");
    }
    {
        Rig r;  // loop flag: the sample restarts
        r.W(0x10, 0x4F);
        r.W(0x13, 0x00);
        r.W(0x15, 0x10);
        r.Samples(800);
        bool allSame = r.dmcReads.size() >= 4;
        for (std::uint16_t a : r.dmcReads) allSame = allSame && a == 0xC000;
        Check(allSame && r.apu.DmcEnabledForTest(), "a looping sample keeps re-reading from its start address");
    }
    {
        Rig r;  // $4011 sets the 7-bit DAC directly
        r.W(0x11, 0xFF);
        r.Samples(2);
        Check(r.apu.ChannelLevelForTest(Apu::kDmc) == 0x7F, "$4011 loads the 7-bit DAC level");
    }
}

void NesApuDmcShiftsBitsLeastSignificantBitFirst() {
    std::printf("NesApuDmcShiftsBitsLeastSignificantBitFirst\n");
    // One byte at the fastest rate; the levels the DAC goes through, as they change.
    auto run = [](std::uint8_t byte, std::uint8_t startLevel) {
        Rig r;
        r.dmcByte = byte;
        r.W(0x10, 0x0F);
        r.W(0x11, startLevel);
        r.W(0x12, 0x00);
        r.W(0x13, 0x00);
        r.W(0x15, 0x10);
        std::vector<int> changes;
        int previous = startLevel;
        for (int level : r.Levels(Apu::kDmc, 140)) {
            if (level != previous) changes.push_back(level);
            previous = level;
        }
        return changes;
    };
    Check(run(0x01, 0x40) == std::vector<int>({66, 64, 62, 60, 58, 56, 54, 52}), "bit 0 comes first: a byte of 0x01 goes up once, then down seven times");
    Check(run(0x80, 0x40) == std::vector<int>({62, 60, 58, 56, 54, 52, 50, 52}), "bit 7 comes last: a byte of 0x80 goes down seven times, then up");
    Check(run(0xFF, 0x7F).empty(), "a 1 bit leaves the counter alone at 127");
    Check(run(0xFF, 0x7C) == std::vector<int>({126}), "a 1 bit moves 124 up to 126 and no further");
    Check(run(0x00, 0x01).empty(), "a 0 bit leaves the counter alone at 1");
    Check(run(0x00, 0x03) == std::vector<int>({1}), "a 0 bit moves 3 down to 1 and no further");
}

void NesApuMixerMatchesTheNonLinearTables() {
    std::printf("NesApuMixerMatchesTheNonLinearTables\n");
    // Pulse duty 3 starts high and (with the slowest allowed period) stays high for far longer than the test;
    // the noise register is clocked only once per ~1000 samples (its first clock leaves the output high);
    // the triangle with the control bit set and the slowest period sits on its first step (level 14).
    auto expectedPulse = [](int sum) { return sum == 0 ? 0.0 : 95.88 / (8128.0 / sum + 100.0); };
    auto expectedTnd = [](int t, int n, int d) {
        const double x = t / 8227.0 + n / 12241.0 + d / 22638.0;
        return x == 0.0 ? 0.0 : 159.79 / (1.0 / x + 100.0);
    };
    struct Case {
        int a, b, n, d, t;
    };
    const Case cases[] = {{15, 0, 0, 0, 0}, {15, 15, 0, 0, 0}, {7, 3, 0, 0, 0}, {0, 0, 15, 0, 0}, {0, 0, 0, 127, 0},
                          {10, 4, 9, 40, 0}, {15, 15, 15, 127, 1}, {0, 0, 0, 0, 1}, {5, 0, 0, 20, 1}};
    int bad = 0;
    double worst = 0.0;
    for (const Case& c : cases) {
        Rig r;
        r.W(0x15, static_cast<std::uint8_t>((c.a ? 1 : 0) | (c.b ? 2 : 0) | (c.t ? 4 : 0) | (c.n ? 8 : 0)));
        if (c.a) {
            r.W(0x00, static_cast<std::uint8_t>(0xF0 | c.a));
            r.W(0x02, 0xFE);
            r.W(0x03, 0x0B);  // length index 1, period register 0x3FE
        }
        if (c.b) {
            r.W(0x04, static_cast<std::uint8_t>(0xF0 | c.b));
            r.W(0x06, 0xFE);
            r.W(0x07, 0x0B);
        }
        if (c.n) {
            r.W(0x0C, static_cast<std::uint8_t>(0x30 | c.n));
            r.W(0x0E, 0x0F);
            r.W(0x0F, 0x08);
        }
        if (c.t) {
            r.W(0x08, 0xFF);
            r.W(0x0A, 0xFF);
            r.W(0x0B, 0x0F);
        }
        r.W(0x11, static_cast<std::uint8_t>(c.d));
        r.Samples(60);
        const int tri = c.t ? 14 : 0;
        const double expected = expectedPulse(c.a + c.b) + expectedTnd(tri, c.n, c.d);
        for (std::size_t i = 20; i < 60; ++i) {
            const double got = r.Mix(i);
            worst = std::fmax(worst, std::fabs(got - expected));
            if (std::fabs(got - expected) > 2e-6) ++bad;
        }
    }
    Check(bad == 0, "the five outputs add up to the original's non-linear mixer value for pulse, triangle, noise and DMC levels");
    std::printf("    worst absolute difference from the formulas: %.3g\n", worst);
    {
        Rig r;  // the group's value is split by channel level, so each meter follows its own channel
        r.W(0x15, 0x03);
        r.W(0x00, 0xF9);
        r.W(0x02, 0xFE);
        r.W(0x03, 0x0B);
        r.W(0x04, 0xF3);
        r.W(0x06, 0xFE);
        r.W(0x07, 0x0B);
        r.Samples(40);
        const float p1 = r.out[Apu::kPulse1][30], p2 = r.out[Apu::kPulse2][30];
        Check(p1 > 0 && p2 > 0 && std::fabs(p1 / p2 - 3.0f) < 1e-3f, "two pulse channels at levels 9 and 3 share their mixed value 3:1");
    }
}

void NesApuPartialSamplesCarryOver() {
    std::printf("NesApuPartialSamplesCarryOver\n");
    Rig r;
    std::size_t total = 0;
    total += r.apu.Render(1, r.out);
    total += r.apu.Render(2, r.out);
    Check(total == 0, "three cycles are less than one sample");
    total += r.apu.Render(1, r.out);
    Check(total == 1, "the fourth cycle completes the first sample");
    total += r.apu.Render(10, r.out);
    Check(total == 3, "ten more cycles complete two more samples and leave two cycles pending");
    total += r.apu.Render(2, r.out);
    Check(total == 4 && r.out[0].size() == 4, "the two pending cycles plus two more complete another");
    Check(r.apu.Render(0, r.out) == 0, "rendering zero cycles does nothing");
}

void NesApuDeterministicAcrossRuns() {
    std::printf("NesApuDeterministicAcrossRuns\n");
    auto run = [](std::vector<float>& flat, std::vector<std::pair<std::uint64_t, bool>>& irqs) {
        Rig r;
        std::uint32_t seed = 12345;
        auto next = [&seed]() {
            seed = seed * 1664525u + 1013904223u;
            return seed >> 16;
        };
        for (int i = 0; i < 3000; ++i) {
            const std::uint32_t v = next();
            const std::uint8_t regs[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x0A, 0x0B, 0x0C, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x15, 0x17};
            const std::uint8_t reg = regs[v % sizeof(regs)];
            r.W(reg, static_cast<std::uint8_t>(next()));
            r.apu.Render(100 + next() % 700, r.out);
            if (i % 50 == 0) r.apu.ReadStatus();
        }
        for (const auto& c : r.out) flat.insert(flat.end(), c.begin(), c.end());
        irqs = r.irq;
    };
    std::vector<float> a, b;
    std::vector<std::pair<std::uint64_t, bool>> ia, ib;
    run(a, ia);
    run(b, ib);
    Check(a.size() > 1000 && a == b && ia == ib, "two identical random register sequences give bit-identical output and IRQ events");
}
