// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Famicom Disk System sound chip (Rp2c33SoundDeviceAdapter) native tests. All register sequences are
// hand-written. Expected values come from the documented behaviour of the chip, worked out in this file
// (accumulator arithmetic, envelope periods, the modulator's rounding formula, the master volume table),
// not read back from the adapter.
//
// Time units used below: the chip makes one output sample per CPU cycle. The wavetable address advances by
// (accumulator >> 16) after every cycle's accumulation, and the sample of a cycle is taken after that step, so
// after n cycles the accumulator has added n * rate. A volume envelope with speed s and an envelope speed
// register e steps once every ((s + 1) * (e + 1) * 8 + 1) cycles; the master volume multiplies the output by
// 32, 21, 16 or 12.

#include "rp2c33_adapter_tests.h"

#include "devices/sound/rp2c33/rp2c33_sound_device_adapter.h"
#include "test_support.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using Chip = Rp2c33SoundDeviceAdapter;

struct Rig {
    Chip chip;
    std::vector<float> out;

    Rig() { chip.SetSoundIoEnabled(true); }

    void W(std::uint16_t address, std::uint8_t value) { chip.WriteIo(address, value); }
    std::uint8_t R(std::uint16_t address) { return chip.ReadIo(address); }
    void Cycles(std::uint64_t n) { chip.Render(n, out); }

    // Fills the wavetable (entry i = f(i)) while the wave write enable ($4089 bit 7) is set, then leaves
    // the chip in play mode with the given master volume.
    void LoadWave(const std::uint8_t* entries, int mvol) {
        W(0x4089, 0x80);
        for (int i = 0; i < 64; ++i) W(static_cast<std::uint16_t>(0x4040 + i), entries[i]);
        W(0x4089, static_cast<std::uint8_t>(mvol));
    }
};

bool Near(float a, double b) { return std::fabs(static_cast<double>(a) - b) < 1e-6; }

// What one sample is for a wave entry, the envelope output and a master volume (see the file comment).
double Expect(int waveEntry, int envelope, int mvol) {
    static const int kMasterVolume[4] = {32, 21, 16, 12};
    return static_cast<double>((waveEntry - 32) * (envelope < 32 ? envelope : 32) * kMasterVolume[mvol]) / 32768.0;
}

}  // namespace

void Rp2c33ResetState() {
    std::printf("Rp2c33ResetState\n");
    Rig r;
    Check(r.chip.WaveHaltedForTest(), "after reset the wave is halted");
    Check(r.chip.VolumeEnvelopeOutForTest() == 0 && r.chip.ModulatorEnvelopeOutForTest() == 0, "after reset both envelope outputs are 0");
    r.Cycles(100);
    bool silent = r.out.size() == 100;
    for (float s : r.out) silent = silent && s == 0.0f;
    Check(silent, "after reset the chip makes 100 samples (one per cycle) and all are 0");
    Rp2c33SoundDeviceAdapter plain;
    Check(plain.ReadIo(0x4090) == 0, "reading while the sound I/O is disabled (the state after reset) gives 0");
    Check(!plain.SoundIoEnabled(), "the sound I/O enable is clear after reset");
}

void Rp2c33SoundIoEnableGatesTheRegisters() {
    std::printf("Rp2c33SoundIoEnableGatesTheRegisters\n");
    Chip chip;
    chip.WriteIo(0x4080, 0x8F);  // volume envelope: disabled, gain 15
    chip.WriteIo(0x4089, 0x00);
    Check(chip.VolumeEnvelopeOutForTest() == 0, "a register write while the sound I/O is disabled does nothing");
    chip.WriteIo(0x4040, 0x15);
    chip.SetSoundIoEnabled(true);
    Check(chip.ReadIo(0x4040) == 0x40, "a wavetable write while disabled did not reach the table (entry reads 0 | 0x40)");
    chip.WriteIo(0x4080, 0x8F);
    Check(chip.VolumeEnvelopeOutForTest() == 15, "with the sound I/O enabled the same write takes effect (gain 15 in direct mode)");
    chip.WriteIo(0x4081, 0x8F);
    chip.WriteIo(0x4081, 0x00);
    Check(chip.VolumeEnvelopeOutForTest() == 15, "$4081 is not a sound register and is ignored");
    chip.SetSoundIoEnabled(false);
    Check(chip.ReadIo(0x4090) == 0, "disabling the sound I/O again makes reads answer 0");
    chip.Reset();
    Check(!chip.SoundIoEnabled(), "Reset() clears the sound I/O enable");
}

void Rp2c33WavetableWriteNeedsTheWriteEnable() {
    std::printf("Rp2c33WavetableWriteNeedsTheWriteEnable\n");
    Rig r;
    // After reset the wavetable is writable ($4089 bit 7 reads as set); entries are 6 bits wide.
    r.W(0x4045, 0xFF);
    Check(r.R(0x4045) == (0x3F | 0x40), "a wavetable entry keeps its low 6 bits and reads back with bit 6 set");
    r.W(0x4046, 0x21);
    r.W(0x4089, 0x00);  // play mode: the table is read-only
    r.W(0x4046, 0x05);
    Check(r.R(0x4046) == (0x21 | 0x40), "in play mode a wavetable write is ignored");
    r.W(0x4089, 0x80);
    r.W(0x4046, 0x05);
    Check(r.R(0x4046) == (0x05 | 0x40), "with $4089 bit 7 set the table is writable again");
    Check(r.R(0x407F) == 0x40, "an untouched entry reads 0 with bit 6 set");
}

void Rp2c33WavePlaybackAndFrequency() {
    std::printf("Rp2c33WavePlaybackAndFrequency\n");
    Rig r;
    std::uint8_t wave[64];
    for (int i = 0; i < 64; ++i) wave[i] = static_cast<std::uint8_t>(i);
    r.LoadWave(wave, 0);
    r.W(0x4080, 0x80 | 32);  // envelope disabled, output gain 32
    r.W(0x4082, 0x00);
    r.W(0x4083, 0x04);  // frequency 0x400, wave running, envelope running
    // 0x400 per cycle = 1/64 of the 16.16 accumulator: the address steps every 64 cycles, and the sample of
    // cycle n (counting from 1) sees address n / 64.
    r.Cycles(8192);
    bool ok = true;
    for (int n = 1; n <= 8192; ++n) {
        const int addr = (n * 1024 >> 16) & 63;
        ok = ok && Near(r.out[static_cast<std::size_t>(n - 1)], Expect(addr, 32, 0));
    }
    Check(ok, "frequency 0x400 steps the wavetable address once every 64 cycles");
    Check(r.chip.WaveAddressForTest() == 128, "after 8192 cycles at 0x400 the wave address counter has stepped 128 times");
    r.out.clear();
    r.W(0x4082, 0x00);
    r.W(0x4083, 0x08);  // frequency 0x800: twice as fast
    r.Cycles(2048);
    ok = true;
    const std::uint32_t acc0 = 8192u * 1024u & 0xFFFF;  // the accumulator was not reset by the second write (wave not halted)
    for (int n = 1; n <= 2048; ++n) {
        const int addr = static_cast<int>((128 + ((acc0 + static_cast<std::uint32_t>(n) * 2048u) >> 16)) & 63);
        ok = ok && Near(r.out[static_cast<std::size_t>(n - 1)], Expect(addr, 32, 0));
    }
    Check(ok, "frequency 0x800 doubles the stepping rate");
    // The wavetable has 64 entries: after 4096 cycles at 0x400 the address has gone round once.
    Rig q;
    q.LoadWave(wave, 0);
    q.W(0x4080, 0x80 | 32);
    q.W(0x4082, 0x00);
    q.W(0x4083, 0x04);
    q.Cycles(4096);
    Check(q.chip.WaveAddressForTest() == 64 && (q.chip.WaveAddressForTest() & 63) == 0, "64 steps bring the address round to entry 0 again (4096 cycles at 0x400)");
}

void Rp2c33WaveHaltFreezesTheAddress() {
    std::printf("Rp2c33WaveHaltFreezesTheAddress\n");
    Rig r;
    std::uint8_t wave[64];
    for (int i = 0; i < 64; ++i) wave[i] = static_cast<std::uint8_t>(63 - i);
    r.LoadWave(wave, 0);
    r.W(0x4080, 0x80 | 32);
    r.W(0x4082, 0x00);
    r.W(0x4083, 0x08);
    r.Cycles(500);
    const std::uint8_t addr = r.chip.WaveAddressForTest();
    Check(addr != 0, "the wave address moved while running");
    r.W(0x4083, 0x88);  // halt bit set (frequency kept)
    r.out.clear();
    r.Cycles(1000);
    bool constant = true;
    for (float s : r.out) constant = constant && s == r.out.front();
    Check(constant && r.chip.WaveAddressForTest() == addr, "with the wave halted the address and the output stay put");
    Check(Near(r.out.front(), Expect(wave[addr & 63], 32, 0)), "a halted wave still outputs its current entry");
    r.W(0x4083, 0x08);  // restart: the accumulator was cleared by the halt
    r.out.clear();
    r.Cycles(32);  // 32 * 0x800 = one full address step
    Check(r.chip.WaveAddressForTest() == static_cast<std::uint8_t>(addr + 1), "restarting resumes from the held address with a cleared accumulator");
}

void Rp2c33VolumeEnvelope() {
    std::printf("Rp2c33VolumeEnvelope\n");
    Rig r;
    std::uint8_t wave[64] = {};
    r.LoadWave(wave, 0);
    r.W(0x4083, 0x00);  // wave and envelope running (frequency 0)
    // Rising: mode bit 6, speed 1 -> a step every (1 + 1) * 1 * 8 + 1 = 17 cycles.
    r.W(0x4080, 0x40 | 1);
    Check(r.chip.VolumeEnvelopeOutForTest() == 0, "the envelope starts at 0");
    r.Cycles(17 * 5);
    Check(r.chip.VolumeEnvelopeOutForTest() == 5, "a rising envelope with speed 1 has stepped 5 times after 85 cycles");
    r.Cycles(17 * 40);
    Check(r.chip.VolumeEnvelopeOutForTest() == 32, "a rising envelope stops at 32");
    // Falling from 32: write the direct gain first, then the falling mode, speed 0 -> a step every 9 cycles.
    r.W(0x4080, 0x80 | 32);
    Check(r.chip.VolumeEnvelopeOutForTest() == 32, "direct mode sets the output to the written gain");
    r.W(0x4080, 0x00);
    r.Cycles(9 * 4);
    Check(r.chip.VolumeEnvelopeOutForTest() == 28, "a falling envelope with speed 0 has stepped 4 times after 36 cycles");
    r.Cycles(9 * 100);
    Check(r.chip.VolumeEnvelopeOutForTest() == 0, "a falling envelope stops at 0");
    // The output sample follows the envelope.
    std::uint8_t loud[64];
    for (int i = 0; i < 64; ++i) loud[i] = 63;
    Rig q;
    q.LoadWave(loud, 0);
    q.W(0x4083, 0x00);
    q.W(0x4080, 0x80 | 10);
    q.Cycles(1);
    Check(Near(q.out[0], Expect(63, 10, 0)), "the output sample is (entry - 32) * envelope * master volume / 32768");
}

void Rp2c33EnvelopeSpeedRegisterAndHalt() {
    std::printf("Rp2c33EnvelopeSpeedRegisterAndHalt\n");
    Rig r;
    std::uint8_t wave[64] = {};
    r.LoadWave(wave, 0);
    r.W(0x4083, 0x00);
    r.W(0x408A, 3);        // overall envelope speed 3
    r.W(0x4080, 0x40 | 0);  // rising, speed 0: a step every (0 + 1) * (3 + 1) * 8 + 1 = 33 cycles
    r.Cycles(33 * 3);
    Check(r.chip.VolumeEnvelopeOutForTest() == 3, "the overall speed register ($408A) scales the volume envelope period");
    r.W(0x4083, 0x40);  // envelope halt bit: the envelope freezes
    r.Cycles(33 * 10);
    Check(r.chip.VolumeEnvelopeOutForTest() == 3, "with the envelope halt bit set the envelope does not move");
    r.W(0x4083, 0x00);
    r.Cycles(33);
    Check(r.chip.VolumeEnvelopeOutForTest() == 4, "clearing the halt bit lets it continue (its clock was cleared by the halt)");
    // The modulator envelope uses the same overall speed.
    r.W(0x4084, 0x40 | 0);
    r.Cycles(33 * 2);
    Check(r.chip.ModulatorEnvelopeOutForTest() == 2, "the modulator envelope steps at the same overall rate");
    r.W(0x4084, 0x80 | 20);
    Check(r.chip.ModulatorEnvelopeOutForTest() == 20, "the modulator envelope in direct mode outputs the written gain");
}

void Rp2c33ModulatorTableAndPosition() {
    std::printf("Rp2c33ModulatorTableAndPosition\n");
    Rig r;
    r.W(0x4087, 0x80);  // modulator halted: the table can be written
    // Table: entry 0 = 2 (+2), entry 1 = 4 (reset the position), all others 0 (no change). The step counter
    // counts twice per entry (it is one bit longer than the table index), so each entry is applied twice.
    for (int i = 0; i < 32; ++i) r.W(0x4088, static_cast<std::uint8_t>(i == 0 ? 2 : (i == 1 ? 4 : 0)));
    r.W(0x4085, 0x10);  // position 16
    Check(r.chip.ModulatorPositionForTest() == 16, "$4085 sets the modulator position");
    r.W(0x4086, 0x00);
    r.W(0x4087, 0x08);  // modulator frequency 0x800: one table step every 32 cycles
    r.Cycles(31);
    Check(r.chip.ModulatorPositionForTest() == 16, "no table step happens before 32 cycles");
    r.Cycles(1);
    Check(r.chip.ModulatorPositionForTest() == 18, "the first table entry (+2) is applied after 32 cycles");
    r.Cycles(32);
    Check(r.chip.ModulatorPositionForTest() == 20, "each table entry is applied for two steps (+2 again)");
    r.Cycles(32);
    Check(r.chip.ModulatorPositionForTest() == 0, "table value 4 resets the position to 0");
    r.Cycles(32 * 20);
    Check(r.chip.ModulatorPositionForTest() == 0, "table value 0 leaves the position alone");
    // Every table value: 0 +0, 1 +1, 2 +2, 3 +4, 4 reset to 0, 5 -4, 6 -2, 7 -1.
    const int expectedPosition[8] = {16, 17, 18, 20, 0, 12, 14, 15};
    for (int v = 0; v < 8; ++v) {
        Rig t;
        t.W(0x4087, 0x80);
        for (int i = 0; i < 32; ++i) t.W(0x4088, static_cast<std::uint8_t>(v));
        t.W(0x4085, 0x10);
        t.W(0x4086, 0x00);
        t.W(0x4087, 0x08);
        t.Cycles(32);
        char what[96];
        std::snprintf(what, sizeof what, "table value %d moves the modulator position from 16 to %d", v, expectedPosition[v]);
        Check(t.chip.ModulatorPositionForTest() == expectedPosition[v], what);
    }
    // Writes to the table are ignored once the modulator runs; the 7-bit position wraps.
    Rig q;
    q.W(0x4087, 0x80);
    for (int i = 0; i < 32; ++i) q.W(0x4088, 1);  // all +1
    q.W(0x4085, 0x7F);
    q.W(0x4086, 0x00);
    q.W(0x4087, 0x08);
    q.Cycles(32);
    Check(q.chip.ModulatorPositionForTest() == 0, "the position is 7 bits wide and wraps from 0x7F to 0");
    q.Cycles(32 * 9);
    Check(q.chip.ModulatorPositionForTest() == 9, "an all +1 table advances one position per 32 cycles");
}

void Rp2c33ModulatorBendsThePitch() {
    std::printf("Rp2c33ModulatorBendsThePitch\n");
    // Modulator halted at a fixed position, gain from the direct-mode modulator envelope. The pitch change
    // is  round(F * ((pos * gain) >> 4) / 64):
    //   pos 8, gain 16, F 0x100:  (128 >> 4) = 8,  F * 8 = 2048,  2048 >> 6 = 32  -> rate 0x100 + 32 = 288
    //   pos -8 (0x78), gain 16:   (-128 >> 4) = -8, F * -8 = -2048, -2048 >> 6 = -32 -> rate 224
    // The first stage drops 4 bits with an odd rounding: a non-zero remainder adds 2 to a positive result
    // that is below 128 and, for a negative position, subtracts 1 only while bit 7 of the result is clear:
    //   pos 1, gain 1:    1 >> 4 = 0 with remainder 1 -> 0 + 2 = 2;  F 0x100: 512 >> 6 = 8  -> rate 264
    //   pos -1 (0x7F):    -1 >> 4 = -1 (bit 7 set, no adjustment);   F 0x100: -256 >> 6 = -4 -> rate 252
    //   pos 1, gain 1, F 0x110: 2 * 272 = 544 = 8 * 64 + 32: a remainder of 32 rounds up -> 9 -> rate 281
    struct Case { std::uint8_t pos; int gain; int freq; int rate; std::uint64_t before, after; };
    // After n cycles the accumulator is n * rate; the address is that >> 16.
    const Case cases[] = {
        {0x08, 16, 0x100, 288, 2275, 2276},  // 2275 * 288 = 655200 -> 9, 2276 * 288 = 655488 -> 10
        {0x78, 16, 0x100, 224, 2925, 2926},  // 2925 * 224 = 655200 -> 9, 2926 * 224 = 655424 -> 10
        {0x01, 1, 0x100, 264, 248, 249},     // 248 * 264 = 65472 -> 0, 249 * 264 = 65736 -> 1
        {0x7F, 1, 0x100, 252, 260, 261},     // 260 * 252 = 65520 -> 0, 261 * 252 = 65772 -> 1
        {0x01, 1, 0x110, 281, 233, 234},     // 233 * 281 = 65473 -> 0, 234 * 281 = 65754 -> 1
    };
    for (const Case& c : cases) {
        Rig r;
        std::uint8_t wave[64] = {};
        r.LoadWave(wave, 0);
        r.W(0x4087, 0x80);  // modulator halted, so the position stays
        r.W(0x4085, c.pos);
        r.W(0x4084, static_cast<std::uint8_t>(0x80 | c.gain));
        r.W(0x4082, static_cast<std::uint8_t>(c.freq & 0xFF));
        r.W(0x4083, static_cast<std::uint8_t>(c.freq >> 8));  // wave running
        r.Cycles(c.before);
        const int stepBefore = r.chip.WaveAddressForTest();
        r.Cycles(c.after - c.before);
        const int stepAfter = r.chip.WaveAddressForTest();
        // The expected address is the step count of the accumulated rate: (cycles * rate) >> 16.
        const int expectBefore = static_cast<int>((c.before * static_cast<std::uint64_t>(c.rate)) >> 16);
        const int expectAfter = static_cast<int>((c.after * static_cast<std::uint64_t>(c.rate)) >> 16);
        char what[160];
        std::snprintf(what, sizeof what, "modulator position 0x%02X, gain %d, frequency 0x%X gives rate %d (address %d after %llu cycles, %d after %llu)",
                      c.pos, c.gain, c.freq, c.rate, expectBefore, static_cast<unsigned long long>(c.before), expectAfter, static_cast<unsigned long long>(c.after));
        Check(stepBefore == expectBefore && stepAfter == expectAfter, what);
    }
    // A modulator envelope of 0 switches the modulator off: the rate is the plain frequency.
    Rig q;
    std::uint8_t wave[64] = {};
    q.LoadWave(wave, 0);
    q.W(0x4087, 0x80);
    q.W(0x4085, 0x08);
    q.W(0x4084, 0x80 | 0);
    q.W(0x4082, 0x00);
    q.W(0x4083, 0x01);
    q.Cycles(256);  // 256 * 256 = one address step exactly
    Check(q.chip.WaveAddressForTest() == 1, "with the modulator envelope at 0 the pitch is unmodulated (256 cycles at 0x100 = one step)");
}

void Rp2c33MasterVolumeAndGainClamp() {
    std::printf("Rp2c33MasterVolumeAndGainClamp\n");
    const int table[4] = {32, 21, 16, 12};
    for (int mvol = 0; mvol < 4; ++mvol) {
        Rig r;
        std::uint8_t wave[64] = {};
        wave[0] = 63;
        r.LoadWave(wave, mvol);
        r.W(0x4080, 0x80 | 32);
        r.W(0x4083, 0x80);  // halted at entry 0: the output is just that entry
        r.Cycles(1);
        char what[96];
        std::snprintf(what, sizeof what, "master volume %d multiplies the output by %d (31 * 32 * %d / 32768)", mvol, table[mvol], table[mvol]);
        Check(Near(r.out[0], static_cast<double>(31 * 32 * table[mvol]) / 32768.0), what);
    }
    Rig q;
    std::uint8_t wave[64] = {};
    q.LoadWave(wave, 0);  // entry 0 = 0: the lowest possible level
    q.W(0x4080, 0x80 | 32);
    q.W(0x4083, 0x80);
    q.Cycles(1);
    Check(q.out[0] == -1.0f, "the lowest entry at full gain and master volume 0 is exactly -1.0");
    q.W(0x4080, 0x80 | 63);  // a direct gain above 32 is clamped to 32 in the output
    q.out.clear();
    q.Cycles(1);
    Check(q.out[0] == -1.0f, "a direct gain above 32 is clamped to 32");
    Check(q.chip.VolumeEnvelopeOutForTest() == 63, "the envelope output itself keeps the written gain");
}

void Rp2c33ReadbackRegisters() {
    std::printf("Rp2c33ReadbackRegisters\n");
    Rig r;
    r.W(0x4080, 0x80 | 21);
    Check(r.R(0x4090) == (21 | 0x40), "$4090 reads the volume envelope output with bit 6 set");
    r.W(0x4084, 0x80 | 9);
    Check(r.R(0x4092) == (9 | 0x40), "$4092 reads the modulator envelope output with bit 6 set");
    r.W(0x4087, 0x80);
    r.W(0x4085, 0x33);
    Check(r.chip.Read(0x17) == 0x33, "reading offset 0x17 gives the modulator position");
    std::uint8_t wave[64] = {};
    wave[0] = 0x2A;
    r.LoadWave(wave, 0);
    Check(r.chip.Read(0x16) == (0x2A | 0x40), "reading offset 0x16 gives the current wavetable entry with bit 6 set");
    Check(r.R(0x4091) == 0, "$4091 is not reachable through the bus and reads 0");
    r.W(0x4089, 0x80);
    r.W(0x4087, 0x80);
    for (int i = 0; i < 32; ++i) r.W(0x4088, i == 0 ? 6 : 0);  // 32 writes bring the table address back to entry 0
    Check(r.chip.Read(0x13) == 2, "offset 0x13 gives bits 5-11 of the table address (64 >> 5 = 2)");
    Check(r.chip.Read(0x15) == 0x0E, "offset 0x15 gives the increment of the current table entry (value 6 = -2, low 4 bits)");
    Check(r.chip.Read(0x14) == 0, "offset 0x14 reads 0");
}

void Rp2c33RenderIsChunkIndependentAndDeterministic() {
    std::printf("Rp2c33RenderIsChunkIndependentAndDeterministic\n");
    auto setup = [](Rig& r) {
        std::uint8_t wave[64];
        for (int i = 0; i < 64; ++i) wave[i] = static_cast<std::uint8_t>((i * 7 + 3) & 63);
        r.LoadWave(wave, 1);
        r.W(0x4087, 0x80);
        for (int i = 0; i < 32; ++i) r.W(0x4088, static_cast<std::uint8_t>((i * 5) & 7));
        r.W(0x4085, 0x05);
        r.W(0x4086, 0x30);
        r.W(0x4087, 0x01);
        r.W(0x4084, 0x40 | 3);
        r.W(0x4080, 0x40 | 2);
        r.W(0x408A, 1);
        r.W(0x4082, 0x77);
        r.W(0x4083, 0x02);
    };
    Rig a;
    setup(a);
    a.Cycles(60000);
    Rig b;
    setup(b);
    std::uint64_t left = 60000;
    std::uint64_t chunk = 1;
    while (left > 0) {
        const std::uint64_t n = chunk < left ? chunk : left;
        b.Cycles(n);
        left -= n;
        chunk = chunk * 3 % 1999 + 1;
    }
    Check(a.out.size() == 60000 && b.out.size() == 60000, "both runs made one sample per cycle");
    Check(a.out == b.out, "rendering in irregular chunks gives exactly the samples of one big call");
    Rig c;
    setup(c);
    c.Cycles(60000);
    Check(a.out == c.out, "two instances given the same writes make identical output (deterministic)");
    bool moving = false;
    for (std::size_t i = 1; i < a.out.size(); ++i) moving = moving || a.out[i] != a.out[0];
    Check(moving, "the test configuration is not silent or constant");
}
