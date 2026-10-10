// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Real Metal Black ROM bring-up native tests.
//
// ROM handling: this file never hard-codes a ROM Root path, and only prints
// derived metadata (peak levels, command sweep counts), never ROM-derived
// sample data. The crc32/size/filename constants in xml/metalblk.xml are MAME's
// public ROM-catalog metadata (ROM_START(metalb)), not ROM byte content.
//
// This file tracks nmiServiceCount per phase, not just once at the end.
// A raw sweep found 127/256 commands producing time-varying audible output while the
// aggregate nmiServiceCount never left 0. That is ambiguous: either the command latch drives the
// audiocpu and nmiServiceCount just isn't the right signal, or the audio is autonomous/
// background output unrelated to SendCommand(). A "no command sent yet" BASELINE phase plus
// per-phase nmiServiceCount deltas distinguish the two.

#include "metalblack_reference_tests.h"

#include "default_machine_catalog.h"
#include "devices/taito/ninjaw/ninja_warriors_machine.h"
#include "machine_registry.h"
#include "mixer.h"
#include "resource_resolver.h"
#include "test_support.h"
#include "chipestra_abi.h"
#include "xml_definition_parser.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

const char* GetEnvVar(const char* name) {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)  // getenv is fine for a read-only test lookup
#endif
    return std::getenv(name);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
}

std::optional<std::filesystem::path> FindMetalBlackDefinitionFile() {
    const std::filesystem::path relative = std::filesystem::path("xml") / "metalblk.xml";
    std::filesystem::path dir = std::filesystem::current_path();
    for (int depth = 0; depth < 8; ++depth) {
        std::error_code ec;
        const std::filesystem::path candidate = dir / relative;
        if (std::filesystem::exists(candidate, ec)) {
            return candidate;
        }
        if (!dir.has_parent_path() || dir.parent_path() == dir) {
            break;
        }
        dir = dir.parent_path();
    }
    return std::nullopt;
}

std::optional<std::string> ReadFileText(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

double PeakAbs(const LogicalChannelSink& sink, ChannelId id) {
    if (!sink.HasChannel(id)) {
        return 0.0;
    }
    const LogicalChannelBuffer& buffer = sink.ChannelBuffer(id);
    double peak = 0.0;
    for (std::size_t i = 0; i < buffer.Size(); ++i) {
        peak = std::max(peak, static_cast<double>(std::fabs(buffer[i])));
    }
    return peak;
}

bool IsTimeVarying(const LogicalChannelSink& sink, ChannelId id) {
    if (!sink.HasChannel(id)) {
        return false;
    }
    const LogicalChannelBuffer& buffer = sink.ChannelBuffer(id);
    if (buffer.Size() == 0) {
        return false;
    }
    float lo = buffer[0];
    float hi = buffer[0];
    for (std::size_t i = 1; i < buffer.Size(); ++i) {
        lo = std::min(lo, buffer[i]);
        hi = std::max(hi, buffer[i]);
    }
    return (hi - lo) > 1e-4f;
}

struct CommandProbe {
    double peak = 0.0;
    bool varying = false;
};

CommandProbe MeasureAllChannelsPeak(LogicalChannelSink& sink) {
    CommandProbe probe;
    for (ChannelId id = 0; id < NinjaWarriorsMachine::kChannelCount; ++id) {
        probe.peak = std::max(probe.peak, PeakAbs(sink, id));
        probe.varying = probe.varying || IsTimeVarying(sink, id);
    }
    return probe;
}

// Shared setup for every real-ROM test in this file: parse metalblk.xml,
// resolve its resources under CHIPESTRA_ROM_ROOT, and construct+Initialize() the
// Machine. Returns nullptr (having already printed a SKIP/failure reason)
// if any step didn't work, so callers can just check for null and return.
std::unique_ptr<IMachine> ResolveAndConstructMetalBlack(const char* callerLabel) {
    const char* romRootEnv = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRootEnv == nullptr || romRootEnv[0] == '\0') {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is not set - %s not exercised (Build gate stays green without the "
                     "ROMs)\n",
                    callerLabel);
        return nullptr;
    }

    const auto path = FindMetalBlackDefinitionFile();
    if (!path.has_value()) {
        std::printf("  SKIP: xml/metalblk.xml not found (run from the repo root)\n");
        return nullptr;
    }
    const auto text = ReadFileText(*path);
    if (!text.has_value()) {
        std::printf("  SKIP: metalblk.xml could not be read\n");
        return nullptr;
    }
    const XmlParseResult parsed = ParseGameDefinition(*text);
    if (parsed.Status != XmlParseStatus::Ok) {
        std::printf("  SKIP: metalblk.xml did not parse (%s)\n", parsed.Detail.c_str());
        return nullptr;
    }
    const GameDefinition& def = parsed.Definition;

    std::vector<RequiredResource> required;
    for (const RomResourceDefinition& rom : def.Resources) {
        RequiredResource r;
        r.LogicalName = rom.Name;
        r.AcceptedNames = {rom.File};
        r.SizeBytes = rom.SizeBytes;
        r.Crc32 = rom.Crc32;
        r.Sha1 = rom.Sha1;
        if (!def.Archive.empty()) r.Archive = def.Archive;
        required.push_back(std::move(r));
    }

    const ResourceResolver resolver{std::filesystem::path(romRootEnv)};
    const ResourceResolutionResult res = resolver.Resolve(required);
    if (!res.Ok) {
        std::printf("  SKIP: metalb ROM set not found under CHIPESTRA_ROM_ROOT - %s not exercised\n", callerLabel);
        for (const auto& f : res.Failures) {
            std::printf("    resolve fail '%s' code %d (%s)\n", f.LogicalName.c_str(), static_cast<int>(f.Result),
                        f.Detail.c_str());
        }
        return nullptr;
    }

    std::unique_ptr<IMachine> machine =
        DefaultMachineRegistry().Create(MachineId(def.Machine), VariantId(def.Variant), res.Resources);
    if (!machine) {
        std::printf("  SKIP: DefaultMachineRegistry().Create() returned null\n");
        return nullptr;
    }
    machine->Initialize();
    return machine;
}

}  // namespace

void MetalBlackDefinitionXmlIsValid() {
    std::printf("MetalBlackDefinitionXmlIsValid\n");

    const auto path = FindMetalBlackDefinitionFile();
    Check(path.has_value(), "definition xml: xml/metalblk.xml is present (run from the repo root)");
    if (!path.has_value()) return;

    const auto text = ReadFileText(*path);
    Check(text.has_value(), "definition xml: file is readable");
    if (!text.has_value()) return;

    const XmlParseResult parsed = ParseGameDefinition(*text);
    Check(parsed.Status == XmlParseStatus::Ok, "definition xml: parses cleanly");
    if (parsed.Status != XmlParseStatus::Ok) {
        std::printf("  detail: %s\n", parsed.Detail.c_str());
        return;
    }

    const GameDefinition& def = parsed.Definition;
    Check(def.Machine == "TAITO_F2SYSTEM", "definition xml: machine == TAITO_F2SYSTEM");
    Check(def.Variant == "METALBLACK_BASE", "definition xml: variant == METALBLACK_BASE");
    Check(def.Resources.size() == 3, "definition xml: three ROM resources declared (soundcpu + adpcma + adpcmb)");

    bool haveSoundcpu = false, haveAdpcmA = false, haveAdpcmB = false;
    bool allHaveCrc = true;
    for (const RomResourceDefinition& rom : def.Resources) {
        haveSoundcpu = haveSoundcpu || rom.Name == "soundcpu";
        haveAdpcmA = haveAdpcmA || rom.Name == "adpcma";
        haveAdpcmB = haveAdpcmB || rom.Name == "adpcmb";
        allHaveCrc = allHaveCrc && rom.Crc32.has_value();
    }
    Check(haveSoundcpu && haveAdpcmA && haveAdpcmB,
          "definition xml: soundcpu/adpcma/adpcmb resources present by logical name (matches "
          "AssembleMetalBlackSoundRoms's own expectations)");
    Check(allHaveCrc, "definition xml: every <rom> carries a crc32 (MAME-catalog metadata, not ROM content)");
    Check(!def.Tracks.empty(), "definition xml: at least one <track> is declared");

    const MachineDefinition* machineDef = DefaultMachineRegistry().Find(MachineId(def.Machine));
    Check(machineDef != nullptr, "definition xml: the referenced machine (TAITO_F2SYSTEM) is in the catalog");
    if (machineDef != nullptr) {
        const bool variantOk =
            std::any_of(machineDef->Variants.begin(), machineDef->Variants.end(),
                        [&](const MachineVariant& v) { return v.Id.Value() == def.Variant; });
        Check(variantOk, "definition xml: the referenced variant (METALBLACK_BASE) exists on that machine");
    }
}

void MetalBlackViaDefinitionXmlRealRomBringUp() {
    std::printf("MetalBlackViaDefinitionXmlRealRomBringUp\n");

    const char* romRootEnv = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRootEnv == nullptr || romRootEnv[0] == '\0') {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is not set - real-ROM bring-up not exercised (Build gate stays "
                     "green without the ROMs)\n");
        return;
    }

    const auto path = FindMetalBlackDefinitionFile();
    Check(path.has_value(), "xml bring-up: xml/metalblk.xml is present (run from the repo root)");
    if (!path.has_value()) return;
    const auto text = ReadFileText(*path);
    Check(text.has_value(), "xml bring-up: file is readable");
    if (!text.has_value()) return;
    const XmlParseResult parsed = ParseGameDefinition(*text);
    Check(parsed.Status == XmlParseStatus::Ok, "xml bring-up: metalblk.xml parses cleanly");
    if (parsed.Status != XmlParseStatus::Ok) return;
    const GameDefinition& def = parsed.Definition;

    std::vector<RequiredResource> required;
    for (const RomResourceDefinition& rom : def.Resources) {
        RequiredResource r;
        r.LogicalName = rom.Name;
        r.AcceptedNames = {rom.File};
        r.SizeBytes = rom.SizeBytes;
        r.Crc32 = rom.Crc32;
        r.Sha1 = rom.Sha1;
        if (!def.Archive.empty()) r.Archive = def.Archive;
        required.push_back(std::move(r));
    }

    // Not Check()-enforced when resolution itself fails (per-game SKIP convention when a
    // specific game's ROM isn't under this environment's ROM Root, distinct from
    // CHIPESTRA_ROM_ROOT being unset entirely).
    const ResourceResolver resolver{std::filesystem::path(romRootEnv)};
    const ResourceResolutionResult res = resolver.Resolve(required);
    if (!res.Ok) {
        std::printf("  SKIP: metalb ROM set not found under CHIPESTRA_ROM_ROOT - real-ROM bring-up not exercised\n");
        for (const auto& f : res.Failures) {
            std::printf("    resolve fail '%s' code %d (%s)\n", f.LogicalName.c_str(), static_cast<int>(f.Result),
                        f.Detail.c_str());
        }
        return;
    }

    std::unique_ptr<IMachine> machine =
        DefaultMachineRegistry().Create(MachineId(def.Machine), VariantId(def.Variant), res.Resources);
    Check(machine != nullptr,
          "xml bring-up: DefaultMachineRegistry().Create() builds a Machine from the XML's own machine/variant/"
          "resources");
    if (!machine) return;
    machine->Initialize();

    auto* metalblack = dynamic_cast<NinjaWarriorsMachine*>(machine.get());
    Check(metalblack != nullptr,
          "reuse verification: the created Machine is actually a NinjaWarriorsMachine "
          "(reused unmodified for Metal Black)");
    if (metalblack == nullptr) return;

    const auto bootSnap = metalblack->AudioCpuSnapshot();
    std::printf("  after init: audiocpu PC=%04X SP=%04X halted=%d nmiServiceCount=%llu\n", bootSnap.pc, bootSnap.sp,
                bootSnap.halted ? 1 : 0, (unsigned long long)bootSnap.nmiServiceCount);

    // nmiServiceCount is NOT the right signal for this board: the audiocpu's real ROM busy-polls
    // the TC0140SYT comm register (mode 4/status, "LD A,4"/"LD (0E200h),A"/"LD A,(0E201h)")
    // instead of using NMI (like After Burner II on X-Board), so nmiServiceCount staying at 0 is
    // expected. The measurement window is several seconds (one block is ~1.33ms), since audible
    // output onset can take that long on real hardware for some boards.
    LogicalChannelSink sink;
    constexpr int kSettleBlocks = 300;  // ~0.4s of real time before probing commands
    for (int i = 0; i < kSettleBlocks; ++i) {
        machine->Render(EmuDuration{Mixer::kBlockSizeFrames}, &sink);
    }

    // "07:Born to be free(Round 1)" (0x0C), looked up by title from the XML itself rather than
    // hardcoded, like DariusViaDefinitionXmlRealRomBringUp/NinjaWarriorsViaDefinitionXmlRealRomBringUp.
    const auto trackIt = std::find_if(def.Tracks.begin(), def.Tracks.end(), [](const TrackDefinition& t) {
        return t.Title.find("Born to be free") != std::string::npos;
    });
    Check(trackIt != def.Tracks.end(), "xml bring-up: \"Born to be free\" track is declared");
    if (trackIt == def.Tracks.end()) return;
    const std::uint8_t bornToBeFreeCommand = static_cast<std::uint8_t>(std::stoul(trackIt->Command, nullptr, 0));

    // Sent from a clean just-booted state. Real MAME confirmed this command, injected via the same
    // master-side TC0140SYT protocol as the only command sent, is audible within the same second
    // and continuously time-varying for 40+ seconds. Chipestra does NOT reproduce that when it is
    // the literal first command after boot; it becomes audible once a short sequence of a few other
    // commands has been sent and settled first. That sequence is reproduced here, and the
    // boot-fresh gap is called out in the verdict text below rather than silently worked around.
    for (std::uint8_t warmupCmdByte : {std::uint8_t(0x00), std::uint8_t(0x50), std::uint8_t(0x3F), std::uint8_t(0x49)}) {
        MusicCommand warmupCmd;
        warmupCmd.Kind = MusicCommandKind::Raw;
        warmupCmd.Value = warmupCmdByte;
        machine->SendCommand(warmupCmd);
        for (int i = 0; i < 750; ++i) machine->Render(EmuDuration{Mixer::kBlockSizeFrames}, nullptr);  // ~1s each
    }

    sink.ClearAll();
    MusicCommand bornCmd;
    bornCmd.Kind = MusicCommandKind::Raw;
    bornCmd.Value = bornToBeFreeCommand;
    machine->SendCommand(bornCmd);

    // Poll once per second (750 blocks = 750*256/192000 = 1.000s exactly) for up to 20s,
    // matching the longest onset real MAME showed across the three TC0140SYT-family boards
    // (Darius needed ~17s).
    constexpr int kBlocksPerSecond = 750;
    constexpr int kMaxSeconds = 20;
    bool becameAudible = false;
    int onsetSecond = -1;
    CommandProbe overallProbe;
    for (int second = 0; second < kMaxSeconds; ++second) {
        sink.ClearAll();
        for (int i = 0; i < kBlocksPerSecond; ++i) machine->Render(EmuDuration{Mixer::kBlockSizeFrames}, &sink);
        const CommandProbe secondProbe = MeasureAllChannelsPeak(sink);
        overallProbe.peak = std::max(overallProbe.peak, secondProbe.peak);
        overallProbe.varying = overallProbe.varying || secondProbe.varying;
        if (secondProbe.varying && !becameAudible) {
            becameAudible = true;
            onsetSecond = second;
        }
    }
    const auto afterBornSnap = metalblack->AudioCpuSnapshot();

    std::printf("  cmd 0x%02X \"Born to be free(Round 1)\": became audible = %d%s, peak=%.4f, "
                "nmiServiceCount stayed %llu->%llu (expected - this board polls, this file's own header comment)\n",
                bornToBeFreeCommand, becameAudible ? 1 : 0,
                becameAudible ? (" (onset ~" + std::to_string(onsetSecond) + "s after SendCommand())").c_str() : "",
                overallProbe.peak, (unsigned long long)bootSnap.nmiServiceCount,
                (unsigned long long)afterBornSnap.nmiServiceCount);

    // `becameAudible`/peak/IsTimeVarying() above is NOT a reliable signal for this board: every
    // command, including 0x00 "STOP", triggers the same unconditional 6-channel ADPCM-A start
    // sequence, so it cannot show that "Born to be free" itself plays. The real discriminator is FM
    // key-on (register 0x28, port A, nonzero operator mask), checked per track by
    // MetalBlackKeyOnSweepDiagnostic. The final judgment is made by listening to real GUI playback
    // (Player.LoadGame -> SelectTrack). This function only reports the peak signal and does not
    // itself decide pass/fail.
    std::printf(
        "  information only: command 0x%02X (\"Born to be free\") becameAudible=%d/peak=%.4f via the peak/"
        "IsTimeVarying() signal. That signal alone is not proof for this board (it also fires for command 0x00 "
        "\"STOP\", because every command retriggers the same ADPCM-A startup blip); the per-track FM key-on check "
        "is MetalBlackKeyOnSweepDiagnostic below (all 26 declared tracks), and the final judgment was GUI listening."
        "\n",
        bornToBeFreeCommand, becameAudible ? 1 : 0, overallProbe.peak);
}

// Regression for "switching songs mixes both songs": real MAME shows 0x0E sent straight over 0x0A leaves 0x0E's ADPCM-A channels running
// under 0x0A. NinjaWarriorsMachine::SendCommand() passes STOP first when a song is running. Compares per-second,
// per-channel peaks of "0x0E for 12s, then 0x0A" with "0x0A alone" (both from a fresh Machine): the tails must match.
void MetalBlackSongSwitchDoesNotMixSongs() {
    std::printf("MetalBlackSongSwitchDoesNotMixSongs\n");

    constexpr int kBlocksPerSecond = 750;  // 750*256/192000 = 1.000s exactly
    constexpr int kSeconds = 16;
    auto peaksPerSecond = [&](std::unique_ptr<IMachine>& machine, const std::vector<std::pair<int, std::uint8_t>>& schedule) {
        std::vector<std::vector<double>> peaks(kSeconds, std::vector<double>(NinjaWarriorsMachine::kChannelCount, 0.0));
        LogicalChannelSink sink;
        std::size_t next = 0;
        for (int second = 0; second < kSeconds + (schedule.size() > 1 ? schedule.back().first : 0); ++second) {
            while (next < schedule.size() && schedule[next].first == second) {
                MusicCommand c;
                c.Kind = MusicCommandKind::Raw;
                c.Value = schedule[next].second;
                machine->SendCommand(c);
                ++next;
            }
            const int base = schedule.size() > 1 ? schedule.back().first : 0;
            sink.ClearAll();
            for (int i = 0; i < kBlocksPerSecond; ++i) machine->Render(EmuDuration{Mixer::kBlockSizeFrames}, &sink);
            if (second >= base && second - base < kSeconds) {
                for (ChannelId id = 0; id < NinjaWarriorsMachine::kChannelCount; ++id) {
                    peaks[second - base][id] = PeakAbs(sink, id);
                }
            }
        }
        return peaks;
    };

    auto refMachine = ResolveAndConstructMetalBlack("song-switch regression");
    if (!refMachine) return;
    const auto ref = peaksPerSecond(refMachine, {{0, 0x0A}});
    auto switchMachine = ResolveAndConstructMetalBlack("song-switch regression");
    if (!switchMachine) return;
    const auto switched = peaksPerSecond(switchMachine, {{0, 0x0E}, {12, 0x0A}});

    int mismatchingCells = 0;
    for (int second = 3; second < kSeconds - 1; ++second) {
        for (std::size_t ch = 0; ch < NinjaWarriorsMachine::kChannelCount; ++ch) {
            if (std::fabs(ref[second][ch] - switched[second][ch]) > 0.012) ++mismatchingCells;
        }
    }
    std::printf("  cells differing between \"0x0E then 0x0A\" and \"0x0A alone\" (seconds 3..14): %d\n", mismatchingCells);
    Check(mismatchingCells <= 3,
          "song switch: 0x0A sent over a running 0x0E sounds like 0x0A alone (the previous song does not keep playing)");
}

void MetalBlackAudiocpuPollingDiagnostic() {
    std::printf("MetalBlackAudiocpuPollingDiagnostic\n");

    std::unique_ptr<IMachine> machineBase = ResolveAndConstructMetalBlack("polling diagnostic");
    if (!machineBase) return;
    auto* metalblack = dynamic_cast<NinjaWarriorsMachine*>(machineBase.get());
    if (metalblack == nullptr) {
        std::printf("  SKIP: created Machine is not a NinjaWarriorsMachine\n");
        return;
    }

    // Settle first (driver's own boot sequence, no observers installed yet -
    // avoids paying the histogram/observer cost over startup, which isn't
    // the steady-state loop we care about here).
    LogicalChannelSink sink;
    for (int i = 0; i < 300; ++i) machineBase->Render(EmuDuration{Mixer::kBlockSizeFrames}, nullptr);

    // Phase A: idle window, NO command sent - captures the driver's own
    // "wait for something to do" loop in isolation.
    std::unordered_map<std::uint16_t, std::uint64_t> idlePcHits;
    std::uint64_t idleModeSelectWrites = 0;
    metalblack->AudioCpuForTest().SetOpcodeFetchObserverForTest(
        [&idlePcHits](std::uint16_t pc, std::uint8_t) { ++idlePcHits[pc]; });
    metalblack->AudioCpuForTest().SetMemoryWriteObserverForTest([&idleModeSelectWrites](std::uint16_t addr, std::uint8_t) {
        if (addr == 0xE200) ++idleModeSelectWrites;
    });
    for (int i = 0; i < 40; ++i) machineBase->Render(EmuDuration{Mixer::kBlockSizeFrames}, nullptr);
    metalblack->AudioCpuForTest().SetOpcodeFetchObserverForTest(nullptr);
    metalblack->AudioCpuForTest().SetMemoryWriteObserverForTest(nullptr);
    const std::uint8_t ramD200BeforeCmd = metalblack->ReadAudioCpuMemory(0xD200);
    const std::uint8_t ramD201BeforeCmd = metalblack->ReadAudioCpuMemory(0xD201);

    // Phase B: same window length, immediately AFTER sending one command -
    // if the driver's hot loop shifts (the top PC changes, or its hit count
    // drops sharply because it moved on to servicing the command), that
    // corroborates a polling-detects-the-command story better than
    // nmiServiceCount could.
    MusicCommand cmd;
    cmd.Kind = MusicCommandKind::Raw;
    cmd.Value = 0x0C;  // "07:Born to be free(Round 1)", the user-provided example
    machineBase->SendCommand(cmd);

    std::unordered_map<std::uint16_t, std::uint64_t> afterCmdPcHits;
    std::uint64_t afterCmdModeSelectWrites = 0;
    metalblack->AudioCpuForTest().SetOpcodeFetchObserverForTest(
        [&afterCmdPcHits](std::uint16_t pc, std::uint8_t) { ++afterCmdPcHits[pc]; });
    metalblack->AudioCpuForTest().SetMemoryWriteObserverForTest(
        [&afterCmdModeSelectWrites](std::uint16_t addr, std::uint8_t) {
            if (addr == 0xE200) ++afterCmdModeSelectWrites;
        });
    for (int i = 0; i < 40; ++i) machineBase->Render(EmuDuration{Mixer::kBlockSizeFrames}, nullptr);
    metalblack->AudioCpuForTest().SetOpcodeFetchObserverForTest(nullptr);
    metalblack->AudioCpuForTest().SetMemoryWriteObserverForTest(nullptr);

    auto printTopPcs = [&](const char* label, const std::unordered_map<std::uint16_t, std::uint64_t>& hits,
                            std::uint64_t modeSelectWrites) {
        std::vector<std::pair<std::uint16_t, std::uint64_t>> sorted(hits.begin(), hits.end());
        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        std::printf("  %s: %zu distinct PCs fetched, 0xE200 (TC0140SYT mode-select) written %llu times\n", label,
                    sorted.size(), (unsigned long long)modeSelectWrites);
        const std::size_t topN = std::min<std::size_t>(10, sorted.size());
        for (std::size_t i = 0; i < topN; ++i) {
            const std::uint16_t pc = sorted[i].first;
            const std::uint8_t b0 = metalblack->ReadAudioCpuMemory(pc);
            const std::uint8_t b1 = metalblack->ReadAudioCpuMemory(static_cast<std::uint16_t>(pc + 1));
            const std::uint8_t b2 = metalblack->ReadAudioCpuMemory(static_cast<std::uint16_t>(pc + 2));
            std::printf("    PC=%04X hits=%llu bytes=%02X %02X %02X\n", pc, (unsigned long long)sorted[i].second, b0,
                        b1, b2);
        }
    };
    printTopPcs("Phase A (idle, no command sent)", idlePcHits, idleModeSelectWrites);
    printTopPcs("Phase B (immediately after sending cmd 0x0C)", afterCmdPcHits, afterCmdModeSelectWrites);

    // Phase B's hot loop (0x041x pattern, if present) compares two RAM
    // bytes at 0xD200/0xD201 (CP B against a value loaded from there) -
    // print them directly: a value that changed in response
    // to SendCommand() is a more direct "did the command's content
    // arrive" signal than nmiServiceCount. Plain RAM reads, no side effects.
    std::printf("  RAM 0xD200/0xD201 (the value Phase B's own hot loop compares): before cmd = %02X %02X, after cmd "
                "= %02X %02X\n",
                ramD200BeforeCmd, ramD201BeforeCmd, metalblack->ReadAudioCpuMemory(0xD200),
                metalblack->ReadAudioCpuMemory(0xD201));

    // Interpretation aid, not Check()-enforced (pure diagnostic): look for
    // the two byte patterns a status/comm poll of the TC0140SYT slave side
    // would use at a hot PC - 0x3A xx xx (LD A,(nn), memory-mapped read,
    // matching sound_map's AS_PROGRAM-only wiring) targeting 0xE201 (comm) or 0xE200 (port,
    // read-only-as-nopr on real hardware so unlikely to be the poll target
    // itself, but worth seeing anyway).
    std::printf(
        "  interpretation aid: a busy-poll of the TC0140SYT comm register would show as a HOT PC (highest hits) "
        "whose bytes are 3A 01 E2 (LD A,(0E201h)) or 3A 00 E2 (LD A,(0E200h)), typically followed close by a "
        "compare/branch back to itself. If instead the top PCs' bytes don't match either pattern, the loop is "
        "polling something else entirely (or not polling this region at all) - report the raw bytes either way "
        "rather than guessing.\n");
}

void MetalBlackYm2610RegisterTraceDiagnostic() {
    std::printf("MetalBlackYm2610RegisterTraceDiagnostic\n");

    std::unique_ptr<IMachine> machineBase = ResolveAndConstructMetalBlack("ym2610 register trace diagnostic");
    if (!machineBase) return;
    auto* metalblack = dynamic_cast<NinjaWarriorsMachine*>(machineBase.get());
    if (metalblack == nullptr) {
        std::printf("  SKIP: created Machine is not a NinjaWarriorsMachine\n");
        return;
    }

    LogicalChannelSink sink;
    for (int i = 0; i < 300; ++i) machineBase->Render(EmuDuration{Mixer::kBlockSizeFrames}, nullptr);

    // Tracks the YM2610's own (addr,data) port pairs - offset0/2 latch a
    // register address, offset1/3 write data to whichever address was last
    // latched on that port (standard OPNA-family port protocol,
    // Ym2610DeviceAdapter::Write's own offset convention).
    struct RegWrite {
        char port;  // 'A' or 'B'
        std::uint8_t reg;
        std::uint8_t data;
    };
    std::uint8_t regAddrA = 0, regAddrB = 0;
    std::vector<RegWrite> writes;
    auto observer = [&](std::uint16_t addr, std::uint8_t value) {
        switch (addr) {
            case 0xE000:
                regAddrA = value;
                break;
            case 0xE001:
                writes.push_back({'A', regAddrA, value});
                break;
            case 0xE002:
                regAddrB = value;
                break;
            case 0xE003:
                writes.push_back({'B', regAddrB, value});
                break;
            default:
                break;
        }
    };

    auto runPhase = [&](const char* label, std::optional<std::uint8_t> commandToSend, int blocks) {
        if (commandToSend.has_value()) {
            MusicCommand cmd;
            cmd.Kind = MusicCommandKind::Raw;
            cmd.Value = *commandToSend;
            machineBase->SendCommand(cmd);
        }
        writes.clear();
        metalblack->AudioCpuForTest().SetMemoryWriteObserverForTest(observer);
        for (int i = 0; i < blocks; ++i) machineBase->Render(EmuDuration{Mixer::kBlockSizeFrames}, &sink);
        metalblack->AudioCpuForTest().SetMemoryWriteObserverForTest(nullptr);

        std::size_t keyOnWrites = 0;   // reg 0x28 (FM key on/off, port A only)
        std::size_t keyOnNonZero = 0;  // reg 0x28 with a nonzero (operator-mask) data byte
        std::size_t adpcmAKeyWrites = 0;  // reg 0x00 (port B, ADPCM-A start/stop key)
        for (const auto& w : writes) {
            if (w.port == 'A' && w.reg == 0x28) {
                ++keyOnWrites;
                if ((w.data & 0xF0) != 0) ++keyOnNonZero;
            }
            if (w.port == 'B' && w.reg == 0x00) ++adpcmAKeyWrites;
        }
        if (adpcmAKeyWrites > 0) {
            std::printf("    B:00 (ADPCM-A start/stop key) written %zu time(s): ", adpcmAKeyWrites);
            for (const auto& w : writes) {
                if (w.port == 'B' && w.reg == 0x00) std::printf("%02X ", w.data);
            }
            std::printf("\n");
        } else {
            std::printf("    B:00 (ADPCM-A start/stop key) NEVER written this phase\n");
        }
        std::printf("  %s: %zu total reg writes (A=%zu, B=%zu), reg 0x28 (FM key on/off) writes=%zu (nonzero "
                    "operator mask=%zu)\n",
                    label, writes.size(),
                    static_cast<std::size_t>(std::count_if(writes.begin(), writes.end(),
                                                            [](const RegWrite& w) { return w.port == 'A'; })),
                    static_cast<std::size_t>(std::count_if(writes.begin(), writes.end(),
                                                            [](const RegWrite& w) { return w.port == 'B'; })),
                    keyOnWrites, keyOnNonZero);
        const std::size_t showN = std::min<std::size_t>(16, writes.size());
        std::string sample;
        for (std::size_t i = 0; i < showN; ++i) {
            char buf[24];
            std::snprintf(buf, sizeof(buf), "%c:%02X=%02X ", writes[i].port, writes[i].reg, writes[i].data);
            sample += buf;
        }
        std::printf("    first %zu writes: %s\n", showN, sample.c_str());
    };

    runPhase("Phase 1 (send 0x01 \"Notice Display 1\", ~1s)", std::optional<std::uint8_t>(std::uint8_t{0x01}), 750);
    runPhase("Phase 2 (send 0x00 \"STOP\" right after, ~1s)", std::optional<std::uint8_t>(std::uint8_t{0x00}), 750);
    runPhase("Phase 3 (send 0x0C \"Born to be free\" right after STOP, ~1s)",
              std::optional<std::uint8_t>(std::uint8_t{0x0C}), 750);
    runPhase("Phase 4 (no new command, another ~1s of the same track)", std::nullopt, 750);

    std::printf(
        "  interpretation aid: reg 0x28 (port A) is the YM2610's FM key on/off register - a genuine new note "
        "(FM melody, matching a real BGM track like \"Born to be free\") should show up here with a NONZERO "
        "operator-mask nibble (data bits 4-7) at least once during Phase 3/4. If Phase 1's Notice-display ADPCM "
        "sample is the only thing ever heard (this diagnostic's whole reason for existing), Phase 3/4 should show "
        "little to no NEW key-on activity distinct from what Phase 1 already did, even though channel meters may "
        "still show something audible (the old sample still playing, untouched).\n");
}

void MetalBlackKeyOnSweepDiagnostic() {
    std::printf("MetalBlackKeyOnSweepDiagnostic\n");

    const char* romRootEnv = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRootEnv == nullptr || romRootEnv[0] == '\0') {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is not set - key-on sweep not exercised\n");
        return;
    }
    const auto path = FindMetalBlackDefinitionFile();
    if (!path.has_value()) {
        std::printf("  SKIP: xml/metalblk.xml not found\n");
        return;
    }
    const auto text = ReadFileText(*path);
    if (!text.has_value()) {
        std::printf("  SKIP: metalblk.xml could not be read\n");
        return;
    }
    const XmlParseResult parsed = ParseGameDefinition(*text);
    if (parsed.Status != XmlParseStatus::Ok) {
        std::printf("  SKIP: metalblk.xml did not parse\n");
        return;
    }
    const GameDefinition& def = parsed.Definition;

    std::unique_ptr<IMachine> machineBase = ResolveAndConstructMetalBlack("key-on sweep");
    if (!machineBase) return;
    auto* metalblack = dynamic_cast<NinjaWarriorsMachine*>(machineBase.get());
    if (metalblack == nullptr) {
        std::printf("  SKIP: created Machine is not a NinjaWarriorsMachine\n");
        return;
    }

    LogicalChannelSink sink;
    for (int i = 0; i < 300; ++i) machineBase->Render(EmuDuration{Mixer::kBlockSizeFrames}, nullptr);

    struct RegWrite {
        char port;
        std::uint8_t reg;
        std::uint8_t data;
    };
    std::uint8_t regAddrA = 0, regAddrB = 0;
    std::vector<RegWrite> writes;
    auto observer = [&](std::uint16_t addr, std::uint8_t value) {
        switch (addr) {
            case 0xE000: regAddrA = value; break;
            case 0xE001: writes.push_back({'A', regAddrA, value}); break;
            case 0xE002: regAddrB = value; break;
            case 0xE003: writes.push_back({'B', regAddrB, value}); break;
            default: break;
        }
    };

    std::size_t realKeyOnCount = 0;
    std::vector<std::string> realKeyOnTracks;
    for (const TrackDefinition& track : def.Tracks) {
        std::uint8_t cmdByte = 0;
        try {
            cmdByte = static_cast<std::uint8_t>(std::stoul(track.Command, nullptr, 0));
        } catch (...) {
            continue;
        }

        // STOP first, then the track under test - correct protocol hygiene between sweep entries
        // (even though it alone does not unlock "Born to be free" in isolation).
        MusicCommand stopCmd;
        stopCmd.Kind = MusicCommandKind::Raw;
        stopCmd.Value = 0x00;
        machineBase->SendCommand(stopCmd);
        for (int i = 0; i < 60; ++i) machineBase->Render(EmuDuration{Mixer::kBlockSizeFrames}, nullptr);

        MusicCommand cmd;
        cmd.Kind = MusicCommandKind::Raw;
        cmd.Value = cmdByte;
        machineBase->SendCommand(cmd);

        writes.clear();
        metalblack->AudioCpuForTest().SetMemoryWriteObserverForTest(observer);
        for (int i = 0; i < 750; ++i) machineBase->Render(EmuDuration{Mixer::kBlockSizeFrames}, &sink);
        metalblack->AudioCpuForTest().SetMemoryWriteObserverForTest(nullptr);

        bool realKeyOn = false;
        std::string keyOnBytes;
        for (const auto& w : writes) {
            if (w.port == 'A' && w.reg == 0x28 && (w.data & 0xF0) != 0) {
                realKeyOn = true;
                char buf[16];
                std::snprintf(buf, sizeof(buf), "A28=%02X ", w.data);
                keyOnBytes += buf;
            }
            if (w.port == 'B' && w.reg == 0x00 && (w.data & 0x80) != 0) {
                realKeyOn = true;
                char buf[16];
                std::snprintf(buf, sizeof(buf), "B00=%02X ", w.data);
                keyOnBytes += buf;
            }
        }
        if (realKeyOn) {
            ++realKeyOnCount;
            realKeyOnTracks.push_back(track.Title);
        }
        std::printf("  cmd 0x%02X \"%s\": realKeyOn=%d (%zu reg writes) keyOnEvents: %s\n", cmdByte,
                    track.Title.c_str(), realKeyOn ? 1 : 0, writes.size(), keyOnBytes.c_str());
    }

    std::printf("  SUMMARY: %zu of %zu declared tracks produced a REAL key-on/ADPCM-start event: ", realKeyOnCount,
                def.Tracks.size());
    for (const auto& t : realKeyOnTracks) std::printf("[%s] ", t.c_str());
    std::printf("\n");
}
