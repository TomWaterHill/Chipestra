// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Namco 163 sound chip (Namco163SoundDeviceAdapter) native tests.

#include "namco_163_adapter_tests.h"

#include "devices/sound/namco_163/namco_163_sound_device_adapter.h"
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

using Chip = Namco163SoundDeviceAdapter;
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

// RAM bytes through the data port, starting at `address` with auto-increment.
void Poke(Chip& chip, std::uint8_t address, std::initializer_list<std::uint8_t> bytes) {
    chip.WriteAddress(static_cast<std::uint8_t>(0x80 | address));
    for (std::uint8_t b : bytes) chip.WriteData(b);
}

// Renders `samples` samples (15 CPU cycles each; 15 * n cycles are due exactly n samples).
Channels Run(Chip& chip, std::uint64_t samples) {
    Channels out;
    chip.Render(samples * 15, out);
    return out;
}

// The one channel with a nonzero value in sample i, or -1 when all eight are 0.
int ActiveChannel(const Channels& c, std::size_t i) {
    int found = -1;
    for (std::size_t ch = 0; ch < Chip::kChannelCount; ++ch)
        if (c[ch][i] != 0.0f) found = static_cast<int>(ch);
    return found;
}

float Sum(const Channels& c, std::size_t i) {
    float s = 0;
    for (const auto& v : c) s += v[i];
    return s;
}

// A made-up setup: slots 4-7 enabled ($7F = 0x30 | the volume of slot 7), every slot with its own volume,
// frequency 0 (the phase stays 0, so the sample is wave[offset]) and the waveform byte at RAM 0 = 0xFF, i.e.
// samples 15 -> 7. Slot k has volume k - 2 (2, 3, 4, 5).
void SetUpFourChannels(Chip& chip) {
    for (unsigned slot = 4; slot < 8; ++slot) {
        const std::uint8_t base = static_cast<std::uint8_t>(0x40 + slot * 8);
        Poke(chip, base, {0, 0, 0, 0, 0, 0, 0, static_cast<std::uint8_t>(slot - 2)});
    }
    Poke(chip, 0x00, {0xFF});
    Poke(chip, 0x7F, {static_cast<std::uint8_t>(0x30 | 5)});
}

}  // namespace

void Namco163RamAddressAndAutoIncrement() {
    std::printf("Namco163RamAddressAndAutoIncrement\n");
    Chip chip;
    chip.WriteAddress(0x85);  // auto-increment, address 5
    chip.WriteData(0x11);
    chip.WriteData(0x22);
    chip.WriteData(0x33);
    Check(chip.RamForTest()[5] == 0x11 && chip.RamForTest()[6] == 0x22 && chip.RamForTest()[7] == 0x33, "data writes go to the addressed byte and the following ones");
    chip.WriteAddress(0x05);  // no auto-increment
    chip.WriteData(0x44);
    chip.WriteData(0x55);
    Check(chip.RamForTest()[5] == 0x55 && chip.RamForTest()[6] == 0x22, "without auto-increment the address stays");
    chip.WriteAddress(0x86);
    Check(chip.ReadData() == 0x22 && chip.ReadData() == 0x33, "a data read returns the byte and advances like a write");
    chip.WriteAddress(0x06);
    Check(chip.ReadData() == 0x22 && chip.ReadData() == 0x22, "a data read without auto-increment does not advance");
    chip.WriteAddress(0xFE);
    chip.WriteData(0xA1);
    chip.WriteData(0xA2);
    chip.WriteData(0xA3);
    Check(chip.RamForTest()[0x7E] == 0xA1 && chip.RamForTest()[0x7F] == 0xA2 && chip.RamForTest()[0] == 0xA3, "the address wraps from $7F to 0");
    chip.WriteAddress(0xFF);
    chip.WriteData(0xB1);
    Check(chip.RamForTest()[0x7F] == 0xB1, "the address register takes 7 bits (0xFF = $7F with auto-increment)");
    chip.Reset();
    Check(chip.RamForTest()[5] == 0 && chip.RamForTest()[0x7F] == 0, "Reset clears the RAM");
}

void Namco163TimeDivisionFollowsTheChannelCount() {
    std::printf("Namco163TimeDivisionFollowsTheChannelCount\n");
    Chip one;
    Poke(one, 0x00, {0xFF});
    Poke(one, 0x7F, {5});  // one channel (slot 7), volume 5
    Channels a = Run(one, 6);
    bool allSeven = true;
    for (std::size_t i = 0; i < 6; ++i) allSeven = allSeven && ActiveChannel(a, i) == 7;
    Check(allSeven, "with one channel every sample is slot 7");
    Check(a[7][0] == 7.0f * 5.0f / 128.0f, "its value is (sample - 8) * volume / 128 (7 * 5 / 128)");

    Chip four;
    SetUpFourChannels(four);
    Channels b = Run(four, 10);
    // The first sample is made by the power-on pointer (slot 7); the pointer then wraps to $78 - 24 = slot 4.
    const int expected[10] = {7, 4, 5, 6, 7, 4, 5, 6, 7, 4};
    bool order = true;
    for (std::size_t i = 0; i < 10; ++i) order = order && ActiveChannel(b, i) == expected[i];
    Check(order, "four channels take turns: slot 7, then 4, 5, 6, 7, 4, ...");
    Check(b[4][1] == 7.0f * 2.0f / 128.0f && b[5][2] == 7.0f * 3.0f / 128.0f && b[6][3] == 7.0f * 4.0f / 128.0f && b[7][4] == 7.0f * 5.0f / 128.0f,
          "each slot uses its own volume");

    Chip eight;
    for (unsigned slot = 0; slot < 8; ++slot) Poke(eight, static_cast<std::uint8_t>(0x40 + slot * 8), {0, 0, 0, 0, 0, 0, 0, 1});
    Poke(eight, 0x00, {0xFF});
    Poke(eight, 0x7F, {static_cast<std::uint8_t>(0x70 | 1)});
    Channels c = Run(eight, 17);
    // Eight channels: the pointer wraps to $78 - 56 = $40 = slot 0.
    bool cycle8 = ActiveChannel(c, 0) == 7;
    for (std::size_t i = 1; i < 17; ++i) cycle8 = cycle8 && ActiveChannel(c, i) == static_cast<int>((i - 1) % 8);
    Check(cycle8, "eight channels: slot 7, then 0 to 7 in turn");
}

void Namco163ReadsTheWaveformAndKeepsThePhase() {
    std::printf("Namco163ReadsTheWaveformAndKeepsThePhase\n");
    Chip chip;
    // Slot 7: frequency $10000 (one sample per step), length 256, offset 0, volume 8 and one channel.
    Poke(chip, 0x78, {0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x08});
    Poke(chip, 0x00, {0x21, 0x43, 0x65});  // 4-bit samples 1 2 3 4 5 6; the low nibble comes first
    Channels out = Run(chip, 4);
    const float expected[4] = {(2 - 8) * 8 / 128.0f, (3 - 8) * 8 / 128.0f, (4 - 8) * 8 / 128.0f, (5 - 8) * 8 / 128.0f};
    bool ok = true;
    for (std::size_t i = 0; i < 4; ++i) ok = ok && out[7][i] == expected[i];
    Check(ok, "the phase advances by the frequency and the 4-bit sample at (phase >> 16) is read, low nibble first");
    Check(chip.RamForTest()[0x7D] == 4 && chip.RamForTest()[0x7B] == 0 && chip.RamForTest()[0x79] == 0, "the phase is kept in RAM ($79, $7B, $7D)");

    Chip offsetChip;
    Poke(offsetChip, 0x78, {0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x08});  // offset 2 samples
    Poke(offsetChip, 0x00, {0x21, 0x43, 0x65});
    Channels o = Run(offsetChip, 2);
    Check(o[7][0] == (4 - 8) * 8 / 128.0f && o[7][1] == (5 - 8) * 8 / 128.0f, "the waveform offset (in 4-bit samples) is added");

    Chip shortChip;
    // length = 256 - (0x38 & 0xFC) = 200 samples; the phase starts at sample 199 and the next step reaches 200 = length -> 0.
    Poke(shortChip, 0x78, {0x00, 0x00, 0x00, 0x00, static_cast<std::uint8_t>(0x38 | 0x01), 199, 0x00, 0x08});
    Poke(shortChip, 0x00, {0x21});
    Channels s = Run(shortChip, 1);
    Check(s[7][0] == (1 - 8) * 8 / 128.0f && shortChip.RamForTest()[0x7D] == 0, "the phase wraps at the waveform length (256 - the upper six bits of $7C)");
}

void Namco163VolumeScalesTheSample() {
    std::printf("Namco163VolumeScalesTheSample\n");
    for (unsigned vol : {0u, 1u, 7u, 15u}) {
        Chip chip;
        Poke(chip, 0x00, {0xFF});
        Poke(chip, 0x7F, {static_cast<std::uint8_t>(vol)});
        Channels out = Run(chip, 1);
        Check(out[7][0] == static_cast<float>(7 * static_cast<int>(vol)) / 128.0f, "volume scales the sample: (15 - 8) * volume / 128");
    }
    Chip negative;
    Poke(negative, 0x00, {0x00});  // samples 0 -> -8
    Poke(negative, 0x7F, {15});
    Check(Run(negative, 1)[7][0] == -120.0f / 128.0f, "the sample is signed (-8 * 15 / 128)");
    Chip high;
    Poke(high, 0x00, {0xFF});
    Poke(high, 0x7F, {static_cast<std::uint8_t>(0x00 | 0x0A)});
    Check(Run(high, 1)[7][0] == 70.0f / 128.0f, "the volume is the low four bits of $7F");
}

void Namco163ChannelStreamsAddUpToTheChipOutput() {
    std::printf("Namco163ChannelStreamsAddUpToTheChipOutput\n");
    Chip chip;
    SetUpFourChannels(chip);
    Poke(chip, 0x00, {0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE});
    Poke(chip, 0x60, {0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x00, 0x04});
    Poke(chip, 0x68, {0x40, 0x00, 0x02, 0x00, 0xE0, 0x00, 0x04, 0x07});
    Channels out = Run(chip, 400);
    std::size_t multi = 0;
    float peak = 0;
    for (std::size_t i = 0; i < 400; ++i) {
        int n = 0;
        for (std::size_t ch = 0; ch < Chip::kChannelCount; ++ch)
            if (out[ch][i] != 0.0f) ++n;
        if (n > 1) ++multi;
        peak = std::max(peak, std::fabs(Sum(out, i)));
    }
    Check(multi == 0, "in every sample at most one of the eight streams has a value (one channel makes the sample)");
    Check(peak > 0.0f && peak <= 120.0f / 128.0f, "the output stays within the range of the chip (|value| <= 120 / 128)");
    // The sum of the eight streams is the single stream a listener hears: it must carry every channel's samples.
    std::size_t carried = 0;
    for (std::size_t ch = 4; ch < 8; ++ch)
        for (std::size_t i = 0; i < 400; ++i)
            if (out[ch][i] != 0.0f && Sum(out, i) == out[ch][i]) ++carried;
    std::size_t nonzero = 0;
    for (std::size_t ch = 0; ch < Chip::kChannelCount; ++ch)
        for (float v : out[ch])
            if (v != 0.0f) ++nonzero;
    Check(carried == nonzero && nonzero > 0, "every nonzero channel sample is the whole output of its sample");
}

void Namco163MuteSilencesOnlyThatChannel() {
    std::printf("Namco163MuteSilencesOnlyThatChannel\n");
    Chip plain, muted;
    SetUpFourChannels(plain);
    SetUpFourChannels(muted);
    muted.SetMutedChannels(1u << 5);
    Channels a = Run(plain, 12), b = Run(muted, 12);
    bool same = true, zero = true;
    for (std::size_t i = 0; i < 12; ++i) {
        for (std::size_t ch = 0; ch < Chip::kChannelCount; ++ch) {
            if (ch == 5) zero = zero && b[ch][i] == 0.0f;
            else same = same && a[ch][i] == b[ch][i];
        }
    }
    Check(zero, "a muted slot makes 0");
    Check(same, "the other slots are unchanged");
    Check(plain.RamForTest() == muted.RamForTest(), "the muted slot still runs (the RAM is the same as without the mute)");
}

void Namco163DisabledChipIsSilentAndStopped() {
    std::printf("Namco163DisabledChipIsSilentAndStopped\n");
    Chip chip;
    Poke(chip, 0x78, {0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x08});
    Poke(chip, 0x00, {0xFF});
    chip.SetDisabled(true);
    Channels out = Run(chip, 5);
    bool silent = true;
    for (const auto& v : out)
        for (float s : v) silent = silent && s == 0.0f;
    Check(silent && out[0].size() == 5, "while disabled the chip delivers zeros (one per sample)");
    Check(chip.RamForTest()[0x7D] == 0, "and does not run (the phase stays)");
    chip.SetDisabled(false);
    Channels again = Run(chip, 1);
    Check(again[7][0] != 0.0f && chip.RamForTest()[0x7D] == 1, "enabled again it runs from where it was");
}

void Namco163SampleCountFollowsTheOriginalsTiming() {
    std::printf("Namco163SampleCountFollowsTheOriginalsTiming\n");
    Chip chip;
    Channels out;
    Check(chip.Render(1, out) == 1, "1 CPU cycle: the first sample is already due (ceil(1 * 119318 / 1789772) = 1)");
    Check(chip.Render(14, out) == 0, "15 cycles in all: still 1 sample");
    Check(chip.Render(1, out) == 1, "16 cycles: the second sample");
    Check(chip.Render(1789772 - 16, out) == 119318 - 2, "one second of CPU cycles is 119318 samples in all");
    Check(chip.GeneratedSampleCountForTest() == 119318 && out[0].size() == 119318 && out[7].size() == 119318, "every stream has the same number of samples");
    chip.Reset();
    Check(chip.GeneratedSampleCountForTest() == 0, "Reset restarts the count");
}

void Namco163RenderIsChunkIndependentAndDeterministic() {
    std::printf("Namco163RenderIsChunkIndependentAndDeterministic\n");
    auto make = [](Chip& chip) {
        SetUpFourChannels(chip);
        Poke(chip, 0x00, {0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE});
        Poke(chip, 0x60, {0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x00, 0x04});
        Poke(chip, 0x68, {0x40, 0x00, 0x02, 0x00, 0xE0, 0x00, 0x04, 0x07});
    };
    Chip whole, chunked, again;
    make(whole);
    make(chunked);
    make(again);
    Channels a;
    whole.Render(60000, a);
    Channels b;
    std::uint64_t left = 60000;
    std::uint64_t chunk = 1;
    while (left > 0) {
        const std::uint64_t n = std::min(left, chunk);
        chunked.Render(n, b);
        left -= n;
        chunk = chunk * 3 + 1;
    }
    Channels c;
    again.Render(60000, c);
    Check(a == b, "the same total cycles in uneven chunks give the same streams");
    Check(a == c, "two chips with the same writes give the same streams");
}

namespace {

struct Access {
    std::uint64_t cycle;
    char kind;
    std::uint8_t value;
};

bool ReadAccesses(const std::filesystem::path& path, std::vector<Access>& out) {
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

void Namco163ReplayMatchesTheRecordedStreams() {
    std::printf("Namco163ReplayMatchesTheRecordedStreams\n");
    const char* referenceRoot = GetEnvVar("CHIPESTRA_REFERENCE_DIR");
    if (referenceRoot == nullptr || referenceRoot[0] == '\0') {
        std::printf("  SKIP: CHIPESTRA_REFERENCE_DIR is not set - the recordings of the original emulator are not available\n");
        return;
    }
    // One recording per sound request of a real game (0x80-0xA5: songs and effects), each from power-on: the
    // data writes, address writes and data reads of the chip with their CPU cycle, and the original emulator's
    // own sound stream (one float per sample, 119318 Hz).
    const std::filesystem::path dir = std::filesystem::path(referenceRoot) / "nes" / "rthunder" / "n163";
    std::size_t compared = 0;
    for (unsigned request = 0x80; request <= 0xA5; ++request) {
        char name[8];
        std::snprintf(name, sizeof(name), "%02X", request);
        std::vector<Access> accesses;
        std::vector<float> mame;
        if (!ReadAccesses(dir / (std::string(name) + "_writes.txt"), accesses) || !ReadFloats(dir / (std::string(name) + "_stream.f32"), mame)) {
            std::printf("  SKIP: the recording %s is not in CHIPESTRA_REFERENCE_DIR\\nes\\rthunder\\n163\n", name);
            continue;
        }
        Chip chip;
        Channels out;
        std::uint64_t cycle = 0;
        for (const Access& a : accesses) {
            if (a.cycle > cycle) {
                chip.Render(a.cycle - cycle, out);
                cycle = a.cycle;
            }
            if (a.kind == 'D') chip.WriteData(a.value);
            else if (a.kind == 'S') chip.WriteAddress(a.value);
            else if (a.kind == 'R') chip.ReadData();
        }
        while (chip.GeneratedSampleCountForTest() < mame.size()) chip.Render(15, out);
        std::size_t differing = 0, nonzero = 0;
        for (std::size_t i = 0; i < mame.size(); ++i) {
            if (mame[i] != 0.0f) ++nonzero;
            if (Sum(out, i) != mame[i]) ++differing;
        }
        char label[160];
        std::snprintf(label, sizeof(label), "request %s: all %zu samples (%zu nonzero) equal the original's stream", name, mame.size(), nonzero);
        Check(differing == 0, label);
        ++compared;
    }
    if (compared > 0) std::printf("  %zu recordings compared\n", compared);
}
