// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Konami VRC6 sound chip (Vrc6SoundDeviceAdapter) native tests. All register sequences are hand-written.
// Expected values come from the documented behaviour of the chip, worked out in this file, not read back
// from the adapter.
//
// Time units: one output sample per CPU cycle, each channel's value divided by 128. A generator whose
// reload value is p steps once every (p + 1) cycles; a step happens on the cycle the down counter reads 0,
// so with the counter loaded with p by the register writes, the first step is on cycle p + 1 (counting from
// 1). The pulse duty counter starts at 15 and goes down by one per step; the output is the volume while the
// counter is <= the duty field (ignore-duty bit clear) and 0 otherwise, and holds between steps. The
// sawtooth has a 14-step period: on steps 2, 4, ..., 12 of a period the accumulator gains the rate and the
// output is accumulator / 8 (8 bit accumulator); the 14th step clears both.

#include "vrc6_adapter_tests.h"

#include "devices/sound/vrc6/vrc6_sound_device_adapter.h"
#include "test_support.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using Chip = Vrc6SoundDeviceAdapter;
using Channels = std::array<std::vector<float>, Chip::kChannelCount>;

Channels Run(Chip& chip, std::uint64_t cycles) {
    Channels out;
    chip.Render(cycles, out);
    return out;
}

// The value (in 1/128 units) of sample `index` of a channel.
int Units(const std::vector<float>& v, std::size_t index) { return static_cast<int>(v[index] * 128.0f + 0.5f); }

}  // namespace

void Vrc6ResetState() {
    std::printf("Vrc6ResetState\n");
    Chip chip;
    Check(chip.PulseDutyForTest(0) == 15 && chip.PulseDutyForTest(1) == 15, "after reset both duty counters are 15");
    Check(chip.MasterFreqShiftForTest() == 0, "after reset the frequency shift is 0");
    Channels out = Run(chip, 100);
    bool silent = true;
    for (const auto& v : out) silent = silent && v.size() == 100;
    for (const auto& v : out)
        for (float s : v) silent = silent && s == 0.0f;
    Check(silent, "after reset every channel makes 100 samples (one per cycle) and all are 0");
}

void Vrc6PulsePeriodAndDuty() {
    std::printf("Vrc6PulsePeriodAndDuty\n");
    Chip chip;
    chip.Write(0x000, 0x4F);  // duty field 4, volume 15
    chip.Write(0x001, 0x03);  // frequency 3: a step every 4 cycles
    chip.Write(0x002, 0x80);  // enable
    Channels out = Run(chip, 192);
    bool ok = true;
    int high = 0;
    for (int n = 1; n <= 192; ++n) {
        const int step = n / 4;
        int expected = 0;
        if (step > 0) {
            const int duty = (15 - step) & 15;
            expected = duty <= 4 ? 15 : 0;
        }
        if (Units(out[0], n - 1) != expected) ok = false;
        if (expected) ++high;
    }
    Check(ok, "pulse 1 with frequency 3 and duty field 4: steps every 4 cycles, high while the counter is 4..0");
    Check(high == 3 * 20, "a 64-cycle period is high for 20 cycles (5 of 16 steps), 60 in three periods");
    bool others = true;
    for (float s : out[1]) others = others && s == 0.0f;
    for (float s : out[2]) others = others && s == 0.0f;
    Check(others, "pulse 2 and the sawtooth stay silent");

    // Pulse 2 has its own registers and the same behaviour.
    Chip chip2;
    chip2.Write(0x100, 0x2A);  // duty field 2, volume 10
    chip2.Write(0x101, 0x01);  // a step every 2 cycles
    chip2.Write(0x102, 0x80);
    Channels o2 = Run(chip2, 64);
    bool ok2 = true;
    for (int n = 1; n <= 64; ++n) {
        const int step = n / 2;
        int expected = 0;
        if (step > 0) expected = ((15 - step) & 15) <= 2 ? 10 : 0;
        if (Units(o2[1], n - 1) != expected) ok2 = false;
    }
    Check(ok2, "pulse 2 (volume 10, duty field 2, a step every 2 cycles) follows the same rule");

    // The high bits of the frequency come from the high register (12 bit frequency).
    Chip chip3;
    chip3.Write(0x000, 0x8F);
    chip3.Write(0x002, 0x01);  // while disabled, so the counter takes the new frequency
    chip3.Write(0x001, 0x00);
    chip3.Write(0x002, 0x81);  // frequency 0x100: a step every 257 cycles (the enable write does not reload)
    Channels o3 = Run(chip3, 600);
    Check(Units(o3[0], 255) == 0 && Units(o3[0], 256) == 15, "frequency 0x100: the first step (output rises) is on cycle 257");
}

void Vrc6PulseConstantVolumeMode() {
    std::printf("Vrc6PulseConstantVolumeMode\n");
    Chip chip;
    chip.Write(0x000, 0x8B);  // bit 7: duty ignored, volume 11
    chip.Write(0x001, 0x01);
    chip.Write(0x002, 0x80);
    Channels out = Run(chip, 200);
    bool ok = Units(out[0], 0) == 0;
    for (int n = 1; n < 200; ++n) ok = ok && Units(out[0], n) == 11;
    Check(ok, "with the duty ignored the output is the volume from the first step (cycle 2 with frequency 1) on");
}

void Vrc6SawtoothAccumulation() {
    std::printf("Vrc6SawtoothAccumulation\n");
    Chip chip;
    chip.Write(0x200, 0x08);  // rate 8
    chip.Write(0x201, 0x00);
    chip.Write(0x202, 0x80);  // enable, frequency 0: a step every cycle
    Channels out = Run(chip, 14 * 3);
    bool ok = true;
    for (int n = 1; n <= 14 * 3; ++n) {
        const int clock = (n - 1) % 14;  // the step counter before this cycle's step
        const int expected = (clock >= 2 && clock <= 12) ? clock / 2 : 0;
        if (Units(out[2], n - 1) != expected) ok = false;
    }
    Check(ok, "rate 8: the output climbs 1..6 on steps 2, 4, ..., 12 of 14 and drops to 0 on the 14th, repeatedly");

    // Rate 63 reaches the 8 bit wrap: accumulator 63, 126, 189, 252, 59 (315 - 256), 122.
    Chip wrap;
    wrap.Write(0x200, 0x3F);
    wrap.Write(0x202, 0x80);
    Channels w = Run(wrap, 14);
    const int want[14] = {0, 0, 7, 7, 15, 15, 23, 23, 31, 31, 7, 7, 15, 0};
    bool wok = true;
    for (int n = 0; n < 14; ++n) wok = wok && Units(w[2], n) == want[n];
    Check(wok, "rate 63: outputs 7, 15, 23, 31, then 7, 15 after the 8 bit accumulator wraps, then 0 on the last step");

    // The rate register keeps only 6 bits.
    Chip masked;
    masked.Write(0x200, 0xC8);
    masked.Write(0x202, 0x80);
    Channels m = Run(masked, 3);
    Check(Units(m[2], 2) == 1, "only the low 6 bits of the rate count (0xC8 acts as 8)");

    // A sawtooth period of frequency f lasts 14 * (f + 1) cycles.
    Chip slow;
    slow.Write(0x200, 0x08);
    slow.Write(0x201, 0x01);
    slow.Write(0x202, 0x80);
    Channels s = Run(slow, 56);
    Check(Units(s[2], 0) == 0 && Units(s[2], 5) == 1 && Units(s[2], 9) == 2, "frequency 1: a step every 2 cycles, so the output reaches 1 on cycle 6 and 2 on cycle 10");
}

void Vrc6FrequencyControl() {
    std::printf("Vrc6FrequencyControl\n");
    Chip chip;
    chip.Write(0x003, 0x02);
    Check(chip.MasterFreqShiftForTest() == 4, "$9003 bit 1 divides the frequencies by 16 (shift 4)");
    chip.Write(0x003, 0x04);
    Check(chip.MasterFreqShiftForTest() == 8, "$9003 bit 2 divides them by 256 (shift 8)");
    chip.Write(0x003, 0x06);
    Check(chip.MasterFreqShiftForTest() == 8, "with both bits set the divide by 256 wins");
    chip.Write(0x003, 0x00);
    Check(chip.MasterFreqShiftForTest() == 0, "both clear: no division");

    // Shift 4: frequency 0x7F becomes 7, a step every 8 cycles.
    Chip shifted;
    shifted.Write(0x003, 0x02);
    shifted.Write(0x000, 0x8F);
    shifted.Write(0x001, 0x7F);
    shifted.Write(0x002, 0x80);
    Channels o = Run(shifted, 20);
    Check(Units(o[0], 6) == 0 && Units(o[0], 7) == 15, "frequency 0x7F with the divide by 16: the first step is on cycle 8");

    // Halt: bit 0 stops every generator and the output is silent; releasing it continues from the same point.
    Chip halt;
    halt.Write(0x000, 0x8F);
    halt.Write(0x001, 0x00);
    halt.Write(0x002, 0x80);  // a step every cycle: constant 15 from cycle 1
    Run(halt, 10);
    halt.Write(0x003, 0x01);
    Channels h = Run(halt, 50);
    bool silent = true;
    for (const auto& v : h)
        for (float s : v) silent = silent && s == 0.0f;
    Check(silent, "with the halt bit set all channels are silent");
    const int duty = halt.PulseDutyForTest(0);
    Run(halt, 30);
    Check(halt.PulseDutyForTest(0) == duty, "and the generators do not advance while halted");
    halt.Write(0x003, 0x00);
    Channels r = Run(halt, 5);
    Check(Units(r[0], 0) == 15, "after the halt bit is cleared the pulse sounds again");
}

void Vrc6EnableResetsThePhase() {
    std::printf("Vrc6EnableResetsThePhase\n");
    Chip chip;
    chip.Write(0x000, 0x4F);
    chip.Write(0x001, 0x00);
    chip.Write(0x002, 0x80);
    Run(chip, 5);
    Check(chip.PulseDutyForTest(0) != 15, "after some steps the duty counter has moved");
    chip.Write(0x002, 0x00);  // disable
    Check(chip.PulseDutyForTest(0) == 15, "disabling the channel puts the duty counter back to 15");
    Channels o = Run(chip, 10);
    bool silent = true;
    for (float s : o[0]) silent = silent && s == 0.0f;
    Check(silent, "a disabled pulse is silent");

    Chip saw;
    saw.Write(0x200, 0x08);
    saw.Write(0x202, 0x80);
    Run(saw, 8);
    Check(saw.SawAccumulatorForTest() != 0, "the sawtooth accumulator has moved");
    saw.Write(0x202, 0x00);
    Check(saw.SawAccumulatorForTest() == 0, "disabling the sawtooth clears the accumulator");
    Channels so = Run(saw, 10);
    bool ssilent = true;
    for (float s : so[2]) ssilent = ssilent && s == 0.0f;
    Check(ssilent, "a disabled sawtooth is silent");
}

void Vrc6BusAddressesReachTheRegisters() {
    std::printf("Vrc6BusAddressesReachTheRegisters\n");
    Chip chip;
    chip.WriteIo(0x9000, 0x8F);
    chip.WriteIo(0x9001, 0x00);
    chip.WriteIo(0x9002, 0x80);
    chip.WriteIo(0xA000, 0x8A);
    chip.WriteIo(0xA001, 0x00);
    chip.WriteIo(0xA002, 0x80);
    chip.WriteIo(0xB000, 0x08);
    chip.WriteIo(0xB001, 0x00);
    chip.WriteIo(0xB002, 0x80);
    Channels o = Run(chip, 4);
    Check(Units(o[0], 3) == 15 && Units(o[1], 3) == 10, "$9000-$9002 reach pulse 1 and $A000-$A002 reach pulse 2");
    Check(Units(o[2], 2) == 1, "$B000-$B002 reach the sawtooth");

    Chip other;
    other.WriteIo(0xB003, 0xFF);  // mirroring / PRG RAM control of the cartridge, not a sound register
    other.WriteIo(0xA003, 0xFF);
    other.WriteIo(0x8000, 0xFF);
    other.WriteIo(0xC000, 0xFF);
    Check(other.MasterFreqShiftForTest() == 0, "other addresses do nothing");
    other.WriteIo(0x9003, 0x04);
    Check(other.MasterFreqShiftForTest() == 8, "$9003 is the frequency control");
}

void Vrc6MuteZeroesOnlyTheOutput() {
    std::printf("Vrc6MuteZeroesOnlyTheOutput\n");
    auto setup = [](Chip& c) {
        c.Write(0x000, 0x4F);
        c.Write(0x001, 0x03);
        c.Write(0x002, 0x80);
        c.Write(0x100, 0x8A);
        c.Write(0x101, 0x00);
        c.Write(0x102, 0x80);
        c.Write(0x200, 0x08);
        c.Write(0x202, 0x80);
    };
    Chip plain, muted;
    setup(plain);
    setup(muted);
    muted.SetMutedChannels(0x02);  // pulse 2
    Channels a = Run(plain, 100);
    Channels b = Run(muted, 100);
    bool pulse2Zero = true, restSame = true;
    for (std::size_t i = 0; i < 100; ++i) {
        pulse2Zero = pulse2Zero && b[1][i] == 0.0f;
        restSame = restSame && a[0][i] == b[0][i] && a[2][i] == b[2][i];
    }
    Check(pulse2Zero, "a muted channel makes 0");
    Check(restSame, "the other channels are unchanged");
    muted.SetMutedChannels(0x00);
    plain.SetMutedChannels(0x00);
    Channels c = Run(plain, 50);
    Channels d = Run(muted, 50);
    bool same = true;
    for (std::size_t ch = 0; ch < Chip::kChannelCount; ++ch)
        for (std::size_t i = 0; i < 50; ++i) same = same && c[ch][i] == d[ch][i];
    Check(same, "after unmuting the channel continues exactly where it would have been (the generators kept running)");
}

void Vrc6RenderIsChunkIndependentAndDeterministic() {
    std::printf("Vrc6RenderIsChunkIndependentAndDeterministic\n");
    auto setup = [](Chip& c) {
        c.Write(0x000, 0x5C);
        c.Write(0x001, 0x37);
        c.Write(0x002, 0x80);
        c.Write(0x100, 0x39);
        c.Write(0x101, 0x91);
        c.Write(0x102, 0x81);
        c.Write(0x200, 0x2D);
        c.Write(0x201, 0x05);
        c.Write(0x202, 0x80);
    };
    Chip whole, chunked, again;
    setup(whole);
    setup(chunked);
    setup(again);
    Channels a = Run(whole, 5000);
    Channels b;
    const std::uint64_t chunks[] = {1, 7, 100, 1000, 3000, 892};
    for (std::uint64_t n : chunks) chunked.Render(n, b);
    Channels c = Run(again, 5000);
    bool same = true, repeat = true;
    for (std::size_t ch = 0; ch < Chip::kChannelCount; ++ch) {
        same = same && a[ch] == b[ch];
        repeat = repeat && a[ch] == c[ch];
    }
    Check(same, "rendering in chunks gives exactly the samples of one call");
    Check(repeat, "two chips given the same writes give identical samples (deterministic)");
    bool nonSilent = false;
    for (const auto& v : a)
        for (float s : v) nonSilent = nonSilent || s != 0.0f;
    Check(nonSilent, "the test sequence is audible");
    float peak = 0.0f;
    for (std::size_t i = 0; i < 5000; ++i) {
        const float sum = a[0][i] + a[1][i] + a[2][i];
        if (sum > peak) peak = sum;
    }
    Check(peak <= 61.0f / 128.0f, "the three channels together never exceed 61 / 128");
}
