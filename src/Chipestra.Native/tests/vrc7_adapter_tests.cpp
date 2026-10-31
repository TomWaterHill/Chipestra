// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Konami VRC7 sound chip (Vrc7SoundDeviceAdapter) native tests.

#include "vrc7_adapter_tests.h"

#include "devices/sound/vrc7/vrc7_sound_device_adapter.h"
#include "resource_resolver.h"
#include "test_support.h"

#ifdef _MSC_VER
#include <intrin.h>
#pragma warning(push, 0)
#endif
#include "ymfm/ymfm.h"
#include "ymfm/ymfm_opl.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Chip = Vrc7SoundDeviceAdapter;
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

// A made-up 144-byte table (not a real instrument set).
std::vector<std::uint8_t> MadeUpTable(unsigned multiplier, unsigned offset) {
    std::vector<std::uint8_t> t(Chip::kInstrumentTableSize);
    for (std::size_t i = 0; i < t.size(); ++i) t[i] = static_cast<std::uint8_t>(i * multiplier + offset);
    return t;
}

void Write(Chip& chip, std::uint8_t reg, std::uint8_t value) {
    chip.WriteAddress(reg);
    chip.WriteData(value);
}

// The user instrument: a sustained two-operator voice.
void ProgramUserInstrument(Chip& chip) {
    const std::uint8_t patch[8] = {0x21, 0x21, 0x20, 0x00, 0xF0, 0xF0, 0x0F, 0x07};
    for (std::uint8_t i = 0; i < 8; ++i) Write(chip, i, patch[i]);
}

// Key on a channel with an instrument number (0 = the user instrument) and volume attenuation.
void KeyOn(Chip& chip, unsigned channel, unsigned instrument, unsigned volume, std::uint8_t fnumLow, unsigned block) {
    Write(chip, static_cast<std::uint8_t>(0x30 + channel), static_cast<std::uint8_t>((instrument << 4) | volume));
    Write(chip, static_cast<std::uint8_t>(0x10 + channel), fnumLow);
    Write(chip, static_cast<std::uint8_t>(0x20 + channel), static_cast<std::uint8_t>(0x10 | (block << 1)));
}

Channels Run(Chip& chip, std::uint64_t samples) {
    Channels out;
    chip.Render(samples * Chip::kInputClocksPerSample, out);
    return out;
}

double Energy(const std::vector<float>& v) {
    double e = 0;
    for (float s : v) e += static_cast<double>(s) * s;
    return e;
}

bool SameSamples(const std::vector<float>& a, const std::vector<float>& b) { return a == b; }

}  // namespace

void Vrc7InstrumentTableIsRequired() {
    std::printf("Vrc7InstrumentTableIsRequired\n");
    bool emptyThrew = false, shortThrew = false, longThrew = false, okBuilt = false;
    try { Chip c{std::vector<std::uint8_t>{}}; } catch (const std::invalid_argument&) { emptyThrew = true; }
    try { Chip c{std::vector<std::uint8_t>(143)}; } catch (const std::invalid_argument&) { shortThrew = true; }
    try { Chip c{std::vector<std::uint8_t>(145)}; } catch (const std::invalid_argument&) { longThrew = true; }
    try { Chip c{MadeUpTable(37, 11)}; okBuilt = true; } catch (...) {}
    Check(emptyThrew, "no table: the chip cannot be built");
    Check(shortThrew && longThrew, "a table of any size but 144 bytes is refused");
    Check(okBuilt, "a 144-byte table builds the chip");
    Check(Chip::kOutputSampleRateHz == 49715, "the output rate is the clock / 72, truncated: 49715 Hz");
}

void Vrc7RenderCountsSamplesAndCarriesTheRemainder() {
    std::printf("Vrc7RenderCountsSamplesAndCarriesTheRemainder\n");
    Chip chip{MadeUpTable(37, 11)};
    Channels out;
    const std::size_t a = chip.Render(72 * 10 + 5, out);
    const std::size_t b = chip.Render(67, out);  // 5 + 67 = 72: one more sample
    const std::size_t c = chip.Render(71, out);  // not enough for one
    Check(a == 10 && b == 1 && c == 0, "10 samples for 725 clocks, 1 more when the remainder reaches 72, none for 71");
    bool sizes = true;
    for (const auto& v : out) sizes = sizes && v.size() == 11;
    Check(sizes, "every channel received the same number of samples");
    bool silent = true;
    for (const auto& v : out)
        for (float s : v) silent = silent && s == 0.0f;
    Check(silent, "a chip with no register written is silent");
    Check(chip.GeneratedSampleCountForTest() == 11, "the sample counter agrees");
}

void Vrc7UserInstrumentSoundsOnlyOnItsChannel() {
    std::printf("Vrc7UserInstrumentSoundsOnlyOnItsChannel\n");
    Chip chip{MadeUpTable(37, 11)};
    ProgramUserInstrument(chip);
    KeyOn(chip, 2, 0, 2, 0x80, 4);
    Channels out = Run(chip, 4000);
    Check(Energy(out[2]) > 0.0, "the keyed channel makes sound");
    bool others = true;
    for (std::size_t ch = 0; ch < Chip::kChannelCount; ++ch)
        if (ch != 2) others = others && Energy(out[ch]) == 0.0;
    Check(others, "the other five channels stay silent");
    // Releasing the key (bit 4 of the frequency-high register) decays the note.
    Write(chip, 0x22, 0x08);
    Channels tail = Run(chip, 40000);
    const std::vector<float> end(tail[2].end() - 2000, tail[2].end());
    const std::vector<float> start(out[2].begin(), out[2].begin() + 2000);
    Check(Energy(end) < Energy(start) * 0.1, "after the key is released the note dies away");
}

void Vrc7BuiltInInstrumentReadsTheTable() {
    std::printf("Vrc7BuiltInInstrumentReadsTheTable\n");
    // Instrument 0 is the user instrument; 1-15 come from the table.
    auto make = [](unsigned multiplier, unsigned offset, unsigned instrument) {
        Chip chip{MadeUpTable(multiplier, offset)};
        ProgramUserInstrument(chip);
        KeyOn(chip, 0, instrument, 1, 0x90, 3);
        return Run(chip, 6000)[0];
    };
    const auto builtInA = make(37, 11, 5);
    const auto builtInB = make(53, 7, 5);
    const auto userA = make(37, 11, 0);
    const auto userB = make(53, 7, 0);
    Check(Energy(builtInA) > 0.0 && Energy(builtInB) > 0.0, "a built-in instrument sounds");
    Check(!SameSamples(builtInA, builtInB), "a built-in instrument sounds different with a different table");
    Check(SameSamples(userA, userB), "the user instrument does not depend on the table");
    Check(!SameSamples(builtInA, userA), "a built-in instrument differs from the user instrument");
}

void Vrc7ChannelsAddUpToTheChipOutput() {
    std::printf("Vrc7ChannelsAddUpToTheChipOutput\n");
    const auto table = MadeUpTable(37, 11);
    Chip chip{table};
    ymfm::ymfm_interface intf;
    ymfm::ds1001 reference(intf, table.data());
    reference.reset();
    auto both = [&](std::uint8_t reg, std::uint8_t value) {
        Write(chip, reg, value);
        reference.write_address(reg);
        reference.write_data(value);
    };
    const std::uint8_t patch[8] = {0x21, 0x21, 0x20, 0x00, 0xF0, 0xF0, 0x0F, 0x07};
    for (std::uint8_t i = 0; i < 8; ++i) both(i, patch[i]);
    // Six channels with different instruments, volumes and pitches.
    const unsigned instruments[6] = {0, 3, 7, 1, 12, 15};
    for (unsigned ch = 0; ch < 6; ++ch) {
        both(static_cast<std::uint8_t>(0x30 + ch), static_cast<std::uint8_t>((instruments[ch] << 4) | (ch & 3)));
        both(static_cast<std::uint8_t>(0x10 + ch), static_cast<std::uint8_t>(0x60 + 17 * ch));
        both(static_cast<std::uint8_t>(0x20 + ch), static_cast<std::uint8_t>(0x10 | ((2 + ch % 3) << 1)));
    }
    const std::size_t samples = 20000;
    Channels out = Run(chip, samples);
    bool exact = true;
    std::size_t nonzero = 0;
    for (std::size_t i = 0; i < samples; ++i) {
        ymfm::ds1001::output_data o;
        reference.generate(&o, 1);
        std::int32_t sum = 0;
        for (std::size_t ch = 0; ch < Chip::kChannelCount; ++ch)
            sum += static_cast<std::int32_t>(std::lround(out[ch][i] * 32768.0f));
        exact = exact && sum == o.data[0];
        if (o.data[0] != 0) ++nonzero;
    }
    Check(nonzero > samples / 2, "the chip made sound for most of the run");
    Check(exact, "the six channels add up to ymfm's own mixed output in every one of 20000 samples");
}

void Vrc7MuteZeroesOnlyThatChannel() {
    std::printf("Vrc7MuteZeroesOnlyThatChannel\n");
    auto setup = [](Chip& chip) {
        ProgramUserInstrument(chip);
        KeyOn(chip, 0, 0, 1, 0x70, 3);
        KeyOn(chip, 1, 0, 1, 0xA0, 4);
    };
    Chip plain{MadeUpTable(37, 11)};
    Chip muted{MadeUpTable(37, 11)};
    setup(plain);
    setup(muted);
    muted.SetMutedChannels(0x01);
    const Channels a = Run(plain, 3000);
    const Channels b = Run(muted, 3000);
    Check(Energy(a[0]) > 0.0 && Energy(a[1]) > 0.0, "both keyed channels sound");
    Check(Energy(b[0]) == 0.0, "the muted channel is 0");
    Check(SameSamples(a[1], b[1]), "the other channel is unchanged by the mute");
    muted.SetMutedChannels(0);
    plain.SetMutedChannels(0);
    const Channels a2 = Run(plain, 3000);
    const Channels b2 = Run(muted, 3000);
    Check(SameSamples(a2[0], b2[0]), "after unmuting the channel is where it would have been (the chip kept running)");
}

void Vrc7ChannelsSevenToNineAndRhythmAreIgnored() {
    std::printf("Vrc7ChannelsSevenToNineAndRhythmAreIgnored\n");
    Chip chip{MadeUpTable(37, 11)};
    ProgramUserInstrument(chip);
    for (std::uint8_t ch = 6; ch < 9; ++ch) {
        Write(chip, static_cast<std::uint8_t>(0x30 + ch), 0x01);
        Write(chip, static_cast<std::uint8_t>(0x10 + ch), 0x80);
        Write(chip, static_cast<std::uint8_t>(0x20 + ch), 0x18);
    }
    Write(chip, 0x0E, 0x3F);  // rhythm mode and all the drums
    Channels out = Run(chip, 5000);
    bool silent = true;
    for (const auto& v : out) silent = silent && Energy(v) == 0.0;
    Check(silent, "writes for channels 7-9 and the rhythm register make no sound");
    KeyOn(chip, 5, 0, 1, 0x80, 3);
    Channels again = Run(chip, 3000);
    Check(Energy(again[5]) > 0.0, "channel 6 still works");
}

void Vrc7RenderIsChunkIndependentAndDeterministic() {
    std::printf("Vrc7RenderIsChunkIndependentAndDeterministic\n");
    auto setup = [](Chip& chip) {
        ProgramUserInstrument(chip);
        KeyOn(chip, 0, 0, 2, 0x70, 3);
        KeyOn(chip, 3, 9, 1, 0xB0, 4);
    };
    Chip whole{MadeUpTable(37, 11)};
    Chip chunked{MadeUpTable(37, 11)};
    Chip again{MadeUpTable(37, 11)};
    setup(whole);
    setup(chunked);
    setup(again);
    const std::uint64_t total = 72ull * 9000 + 30;
    Channels a, b, c;
    whole.Render(total, a);
    again.Render(total, c);
    std::uint64_t done = 0;
    std::uint64_t step = 1;
    while (done < total) {
        const std::uint64_t n = std::min<std::uint64_t>(step, total - done);
        chunked.Render(n, b);
        done += n;
        step = step * 3 + 1;
        if (step > 5000) step = 7;
    }
    bool same = true, repeat = true;
    for (std::size_t ch = 0; ch < Chip::kChannelCount; ++ch) {
        same = same && SameSamples(a[ch], b[ch]);
        repeat = repeat && SameSamples(a[ch], c[ch]);
    }
    Check(same, "the same output whatever the chunk sizes");
    Check(repeat, "two chips given the same writes make the same output");
    // Reset puts the registers back, so the run repeats.
    whole.Reset();
    setup(whole);
    Channels d;
    whole.Render(total, d);
    bool afterReset = true;
    for (std::size_t ch = 0; ch < Chip::kChannelCount; ++ch) afterReset = afterReset && SameSamples(a[ch], d[ch]);
    Check(afterReset, "after Reset() the same writes give the same output again");
}

namespace {

struct RecordedWrite {
    double cpuCycle;
    bool data;
    std::uint8_t value;
};

bool ReadWrites(const std::filesystem::path& path, std::vector<RecordedWrite>& out) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream fields(line);
        double c = 0;
        char t = 0;
        unsigned v = 0;
        if (fields >> c >> t >> std::hex >> v) out.push_back({c, t == 'D', static_cast<std::uint8_t>(v)});
    }
    return !out.empty();
}

bool ReadFloats(const std::filesystem::path& path, std::vector<float>& out) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return false;
    const std::streamsize size = in.tellg();
    in.seekg(0);
    out.resize(static_cast<std::size_t>(size) / sizeof(float));
    in.read(reinterpret_cast<char*>(out.data()), size);
    return !out.empty();
}

}  // namespace

void Vrc7ReplayMatchesTheRecordedStream() {
    std::printf("Vrc7ReplayMatchesTheRecordedStream\n");
    const char* romRoot = GetEnvVar("CHIPESTRA_ROM_ROOT");
    const char* referenceRoot = GetEnvVar("CHIPESTRA_REFERENCE_DIR");
    if (romRoot == nullptr || romRoot[0] == '\0' || referenceRoot == nullptr || referenceRoot[0] == '\0') {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT and CHIPESTRA_REFERENCE_DIR are needed (the instrument ROM and the recordings)\n");
        return;
    }
    RequiredResource table;
    table.LogicalName = "instruments";
    table.AcceptedNames = {"ds1001_instruments.bin"};
    table.SizeBytes = Chip::kInstrumentTableSize;
    table.Crc32 = Chip::kInstrumentTableCrc32;
    table.Archive = "ds1001";
    const ResourceResolver resolver{std::filesystem::path(romRoot)};
    ResourceResolutionResult res = resolver.Resolve({table});
    if (!res.Ok) {
        std::printf("  SKIP: the instrument ROM (ds1001.zip) was not found under CHIPESTRA_ROM_ROOT: %s\n",
                    res.Failures.empty() ? "no detail" : res.Failures[0].Detail.c_str());
        return;
    }
    const ResolvedResource* rom = res.Resources.Find("instruments");
    Check(rom != nullptr, "ds1001: the instrument table resolves (archive ds1001, CRC-32 9d699efc)");
    if (rom == nullptr) return;
    const std::vector<std::uint8_t> instruments(rom->Data(), rom->Data() + rom->Size());

    // Recordings of a real game's requests: the VRC7 register writes (time in CPU cycles, fractional) and the
    // original emulator's own sound stream (one float per sample, 49715 Hz). The first song uses the user
    // instrument only; the second also uses built-in instruments.
    const std::filesystem::path dir = std::filesystem::path(referenceRoot) / "nes" / "lagrange" / "vrc7";
    const char* names[2] = {"5B", "73"};
    for (const char* name : names) {
        std::vector<RecordedWrite> writes;
        std::vector<float> mame;
        const bool haveWrites = ReadWrites(dir / (std::string(name) + "_writes.txt"), writes);
        const bool haveStream = ReadFloats(dir / (std::string(name) + "_stream.f32"), mame);
        if (!haveWrites || !haveStream) {
            std::printf("  SKIP: the recording %s is not in CHIPESTRA_REFERENCE_DIR\\nes\\lagrange\\vrc7\n", name);
            continue;
        }
        Chip chip{instruments};
        // The original's stream puts sample i at i / 49715 s; a write at time t reaches the chip before the
        // first sample whose time is >= t. In CPU cycles (21477272 / 12 Hz) that is i * cps >= cycle.
        const double cps = 21477272.0 / 12.0 / static_cast<double>(Chip::kOutputSampleRateHz);
        std::size_t next = 0;
        std::size_t differing = 0;
        double maxDiff = 0;
        for (std::size_t i = 0; i < mame.size(); ++i) {
            while (next < writes.size() && writes[next].cpuCycle <= static_cast<double>(i) * cps) {
                if (writes[next].data) chip.WriteData(writes[next].value);
                else chip.WriteAddress(writes[next].value);
                ++next;
            }
            Channels out;
            chip.Render(Chip::kInputClocksPerSample, out);
            float sum = 0;
            for (const auto& v : out) sum += v[0];
            const double d = std::fabs(static_cast<double>(sum) - mame[i]);
            if (d > 1e-6) ++differing;
            if (d > maxDiff) maxDiff = d;
        }
        char label[160];
        std::snprintf(label, sizeof(label), "song %s: all %zu samples equal the original emulator's stream (differing %zu, largest difference %g)",
                      name, mame.size(), differing, maxDiff);
        Check(differing == 0, label);
    }
}
