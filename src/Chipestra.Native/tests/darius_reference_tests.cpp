// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Real Darius ROM bring-up native tests.
//
// ROM handling: this file never hard-codes a ROM Root path, and only prints
// derived metadata (peak levels, command sweep counts), never ROM-derived
// sample data. The crc32/size/filename constants in xml/darius.xml are MAME's
// public ROM-catalog metadata (ROM_START(darius)), not ROM byte content.
//
// A non-zero/time-varying meter peak is necessary but NOT sufficient for "the
// expected content is playing": IsTimeVarying() below guards against the
// DC-bias false positive (e.g. AY8910 volume 0) by requiring the buffer's
// samples to actually differ across the block, not just be nonzero.

#include "darius_reference_tests.h"

#include "default_machine_catalog.h"
#include "devices/taito/darius/darius_machine.h"
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

std::optional<std::filesystem::path> FindDariusDefinitionFile() {
    const std::filesystem::path relative = std::filesystem::path("xml") / "darius.xml";
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

// Guards against the DC-bias false positive: true only if the channel's samples actually vary across the
// block (min != max beyond a tiny float-noise epsilon), not just nonzero.
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
    double fmPeak = 0.0;    // max |sample| across LogicalChannel ids 0-2, 6-8 (ym1/ym2 FM)
    double ssgPeak = 0.0;   // max |sample| across LogicalChannel ids 3-5, 9-11 (ym1/ym2 SSG)
    double adpcmPeak = 0.0;  // LogicalChannel id 12 (MSM5205)
    bool fmVarying = false;
    bool ssgVarying = false;
    bool adpcmVarying = false;
};

CommandProbe MeasurePeaks(LogicalChannelSink& sink) {
    CommandProbe probe;
    for (ChannelId id : {0u, 1u, 2u, 6u, 7u, 8u}) {
        probe.fmPeak = std::max(probe.fmPeak, PeakAbs(sink, id));
        probe.fmVarying = probe.fmVarying || IsTimeVarying(sink, id);
    }
    for (ChannelId id : {3u, 4u, 5u, 9u, 10u, 11u}) {
        probe.ssgPeak = std::max(probe.ssgPeak, PeakAbs(sink, id));
        probe.ssgVarying = probe.ssgVarying || IsTimeVarying(sink, id);
    }
    probe.adpcmPeak = PeakAbs(sink, 12);
    probe.adpcmVarying = IsTimeVarying(sink, 12);
    return probe;
}

}  // namespace

void DariusDefinitionXmlIsValid() {
    std::printf("DariusDefinitionXmlIsValid\n");

    const auto path = FindDariusDefinitionFile();
    Check(path.has_value(), "definition xml: xml/darius.xml is present (run from the repo root)");
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
    Check(def.Machine == "TAITO_DARIUS", "definition xml: machine == TAITO_DARIUS");
    Check(def.Variant == "DARIUS_BASE", "definition xml: variant == DARIUS_BASE");
    Check(def.Resources.size() == 2, "definition xml: two ROM resources declared (soundcpu + adpcm)");

    bool haveSoundcpu = false;
    bool haveAdpcm = false;
    bool allHaveCrc = true;
    for (const RomResourceDefinition& rom : def.Resources) {
        haveSoundcpu = haveSoundcpu || rom.Name == "soundcpu";
        haveAdpcm = haveAdpcm || rom.Name == "adpcm";
        allHaveCrc = allHaveCrc && rom.Crc32.has_value();
    }
    Check(haveSoundcpu && haveAdpcm,
          "definition xml: soundcpu + adpcm resources present by logical name (matches AssembleDariusSoundRoms's "
          "own expectations)");
    Check(allHaveCrc, "definition xml: every <rom> carries a crc32 (MAME-catalog metadata, not ROM content)");
    Check(!def.Tracks.empty(), "definition xml: at least one <track> is declared");

    const MachineDefinition* machineDef = DefaultMachineRegistry().Find(MachineId(def.Machine));
    Check(machineDef != nullptr, "definition xml: the referenced machine (TAITO_DARIUS) is in the catalog");
    if (machineDef != nullptr) {
        const bool variantOk =
            std::any_of(machineDef->Variants.begin(), machineDef->Variants.end(),
                        [&](const MachineVariant& v) { return v.Id.Value() == def.Variant; });
        Check(variantOk, "definition xml: the referenced variant (DARIUS_BASE) exists on that machine");
    }
}

void DariusViaDefinitionXmlRealRomBringUp() {
    std::printf("DariusViaDefinitionXmlRealRomBringUp\n");

    const char* romRootEnv = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRootEnv == nullptr || romRootEnv[0] == '\0') {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is not set - real-ROM bring-up not exercised (Build gate stays "
                     "green without the ROMs)\n");
        return;
    }

    const auto path = FindDariusDefinitionFile();
    Check(path.has_value(), "xml bring-up: xml/darius.xml is present (run from the repo root)");
    if (!path.has_value()) return;
    const auto text = ReadFileText(*path);
    Check(text.has_value(), "xml bring-up: file is readable");
    if (!text.has_value()) return;
    const XmlParseResult parsed = ParseGameDefinition(*text);
    Check(parsed.Status == XmlParseStatus::Ok, "xml bring-up: darius.xml parses cleanly");
    if (parsed.Status != XmlParseStatus::Ok) return;
    const GameDefinition& def = parsed.Definition;

    // Exactly what Player.LoadGame(gameId, romRoot) does (NativeRuntimeSession
    // .LoadMachineWithResources -> chipestra_load_machine): turn every <rom> the
    // XML itself declares into a RequiredResource and resolve THAT list.
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
    Check(res.Ok, "xml bring-up: every resource darius.xml declares resolves under CHIPESTRA_ROM_ROOT (archive=\"darius\")");
    if (!res.Ok) {
        for (const auto& f : res.Failures) {
            std::printf("    resolve fail '%s' code %d (%s)\n", f.LogicalName.c_str(), static_cast<int>(f.Result),
                        f.Detail.c_str());
        }
        return;
    }

    std::unique_ptr<IMachine> machine =
        DefaultMachineRegistry().Create(MachineId(def.Machine), VariantId(def.Variant), res.Resources);
    Check(machine != nullptr,
          "xml bring-up: DefaultMachineRegistry().Create() builds a Machine from the XML's own machine/variant/resources");
    if (!machine) return;
    machine->Initialize();

    auto* darius = dynamic_cast<DariusMachine*>(machine.get());
    Check(darius != nullptr, "xml bring-up: the created Machine is actually a DariusMachine");
    if (darius == nullptr) return;

    const auto bootSnap = darius->AudioCpuSnapshot();
    std::printf("  after init: audiocpu PC=%04X SP=%04X halted=%d nmiServiceCount=%llu\n", bootSnap.pc, bootSnap.sp,
                bootSnap.halted ? 1 : 0, (unsigned long long)bootSnap.nmiServiceCount);

    // Write-category counters: distinguish real writes into the ym1/
    // ym2 register windows (0x9000-0x9001, 0xA000-0xA001) and the pan/
    // bankswitch window (0xC000-0xDC00) from everything else (RAM, ROM-
    // write-attempts). Reported only, not used to gate PASS/FAIL.
    int diagYmWrites = 0;
    int diagPanWrites = 0;
    darius->AudioCpuForTest().SetMemoryWriteObserverForTest([&diagYmWrites, &diagPanWrites](std::uint16_t addr, std::uint8_t) {
        if ((addr >= 0x9000 && addr <= 0x9001) || (addr >= 0xA000 && addr <= 0xA001)) {
            ++diagYmWrites;
        } else if (addr >= 0xC000 && addr <= 0xDC00) {
            ++diagPanWrites;
        }
    });

    LogicalChannelSink sink;
    constexpr int kSettleBlocks = 300;  // ~1.6s of real time before probing commands
    for (int i = 0; i < kSettleBlocks; ++i) {
        machine->Render(EmuDuration{Mixer::kBlockSizeFrames}, &sink);
    }

    // "CAPTAIN NEO" - an in-game BGM (command 0x06), looked
    // up by title from the XML itself, not hardcoded, so a future XML edit
    // can't silently desync this test from what it claims to verify.
    const auto trackIt = std::find_if(def.Tracks.begin(), def.Tracks.end(),
                                       [](const TrackDefinition& t) { return t.Title.rfind("CAPTAIN NEO", 0) == 0; });
    Check(trackIt != def.Tracks.end(), "xml bring-up: \"CAPTAIN NEO\" track is declared");
    if (trackIt == def.Tracks.end()) return;
    const std::uint8_t captainNeoCommand = static_cast<std::uint8_t>(std::stoul(trackIt->Command, nullptr, 0));

    // One full second per command: onset can take up to ~17s even on real
    // hardware, and 40 blocks (~53ms at 256 frames/block, 192kHz) is far too short.
    constexpr int kMeasureBlocksPerCommand = 750;  // 750*256/192000 = 1.000s exactly
    int fmHits = 0, ssgHits = 0, adpcmHits = 0;
    CommandProbe captainNeoProbe;
    CommandProbe stopProbe;

    for (const TrackDefinition& track : def.Tracks) {
        const std::uint8_t cmdByte = static_cast<std::uint8_t>(std::stoul(track.Command, nullptr, 0));
        sink.ClearAll();

        MusicCommand cmd;
        cmd.Kind = MusicCommandKind::Raw;
        cmd.Value = cmdByte;
        machine->SendCommand(cmd);

        for (int i = 0; i < kMeasureBlocksPerCommand; ++i) {
            machine->Render(EmuDuration{Mixer::kBlockSizeFrames}, &sink);
        }

        const CommandProbe probe = MeasurePeaks(sink);
        if (cmdByte == captainNeoCommand) captainNeoProbe = probe;
        if (cmdByte == 0x00) stopProbe = probe;
        if (probe.fmVarying) ++fmHits;
        if (probe.ssgVarying) ++ssgHits;
        if (probe.adpcmVarying) ++adpcmHits;
    }

    const auto afterSweepSnap = darius->AudioCpuSnapshot();
    std::printf("  command sweep (%zu declared tracks, one persistent Machine): FM time-varying on %d, SSG "
                "time-varying on %d, ADPCM time-varying on %d\n",
                def.Tracks.size(), fmHits, ssgHits, adpcmHits);
    std::printf("  after sweep: audiocpu PC=%04X nmiServiceCount=%llu irqServiceCount=%llu ymWrites=%d panWrites=%d\n",
                afterSweepSnap.pc, (unsigned long long)afterSweepSnap.nmiServiceCount,
                (unsigned long long)afterSweepSnap.irqServiceCount, diagYmWrites, diagPanWrites);
    std::printf("  cmd 0x%02X \"CAPTAIN NEO\": FM peak=%.4f (varying=%d), SSG peak=%.4f (varying=%d), ADPCM "
                "peak=%.4f (varying=%d)\n",
                captainNeoCommand, captainNeoProbe.fmPeak, captainNeoProbe.fmVarying ? 1 : 0, captainNeoProbe.ssgPeak,
                captainNeoProbe.ssgVarying ? 1 : 0, captainNeoProbe.adpcmPeak, captainNeoProbe.adpcmVarying ? 1 : 0);
    std::printf("  cmd 0x00 \"Stop\": FM peak=%.4f (varying=%d), SSG peak=%.4f (varying=%d)\n", stopProbe.fmPeak,
                stopProbe.fmVarying ? 1 : 0, stopProbe.ssgPeak, stopProbe.ssgVarying ? 1 : 0);

    // Reported only, not Check()-enforced. Darius plays correctly in the GUI (PC060HA command injection,
    // low-nibble-first order, the 0xEF boot handshake, the Z80 LD A,I fix). If the sweep below finds no
    // FM/SSG/ADPCM activity at all, that is a regression.
    if (fmHits == 0 && ssgHits == 0 && adpcmHits == 0) {
        std::printf(
            "  FAILED (regression) - no track in this %zu-track sweep produced FM, SSG or ADPCM output "
            "(nmiServiceCount=%llu, %d YM2203 register writes, %d pan-window writes). Darius plays correctly in "
            "the GUI (nibble order, 0xEF boot handshake, Z80 LD A,I, group-delay compensation) - suspect "
            "a recent change to DariusMachine, "
            "DariusCommandInterface or the Z80/YM2203/MSM5205 adapters.\n",
            def.Tracks.size(), (unsigned long long)afterSweepSnap.nmiServiceCount, diagYmWrites, diagPanWrites);
    }
}

void DariusAudiocpuPollingDiagnostic() {
    std::printf("DariusAudiocpuPollingDiagnostic\n");

    const char* romRootEnv = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRootEnv == nullptr || romRootEnv[0] == '\0') {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is not set - polling diagnostic not exercised\n");
        return;
    }
    const auto path = FindDariusDefinitionFile();
    if (!path.has_value()) {
        std::printf("  SKIP: xml/darius.xml not found\n");
        return;
    }
    const auto text = ReadFileText(*path);
    if (!text.has_value()) {
        std::printf("  SKIP: darius.xml could not be read\n");
        return;
    }
    const XmlParseResult parsed = ParseGameDefinition(*text);
    if (parsed.Status != XmlParseStatus::Ok) {
        std::printf("  SKIP: darius.xml did not parse\n");
        return;
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
        std::printf("  SKIP: darius ROM set not found under CHIPESTRA_ROM_ROOT\n");
        return;
    }
    std::unique_ptr<IMachine> machineBase =
        DefaultMachineRegistry().Create(MachineId(def.Machine), VariantId(def.Variant), res.Resources);
    if (!machineBase) {
        std::printf("  SKIP: Create() returned null\n");
        return;
    }
    machineBase->Initialize();
    auto* darius = dynamic_cast<DariusMachine*>(machineBase.get());
    if (darius == nullptr) {
        std::printf("  SKIP: created Machine is not a DariusMachine\n");
        return;
    }

    for (int i = 0; i < 300; ++i) machineBase->Render(EmuDuration{Mixer::kBlockSizeFrames}, nullptr);

    // Darius's PC060HA slave side is at 0xB000 (slave_port_w, mode
    // select)/0xB001 (slave_comm_r/w), unlike Ninja Warriors/Metal Black's
    // 0xE200/0xE201, but the same underlying protocol (taitosnd.cpp's
    // pc060ha_device is a zero-override subclass of tc0140syt_device).
    std::unordered_map<std::uint16_t, std::uint64_t> idlePcHits;
    std::uint64_t idleModeSelectWrites = 0;
    darius->AudioCpuForTest().SetOpcodeFetchObserverForTest(
        [&idlePcHits](std::uint16_t pc, std::uint8_t) { ++idlePcHits[pc]; });
    darius->AudioCpuForTest().SetMemoryWriteObserverForTest([&idleModeSelectWrites](std::uint16_t addr, std::uint8_t) {
        if (addr == 0xB000) ++idleModeSelectWrites;
    });
    for (int i = 0; i < 40; ++i) machineBase->Render(EmuDuration{Mixer::kBlockSizeFrames}, nullptr);
    darius->AudioCpuForTest().SetOpcodeFetchObserverForTest(nullptr);
    darius->AudioCpuForTest().SetMemoryWriteObserverForTest(nullptr);

    // "CAPTAIN NEO" (command 0x06) - the same track DariusViaDefinitionXmlRealRomBringUp
    // checks by title lookup.
    const auto trackIt = std::find_if(def.Tracks.begin(), def.Tracks.end(),
                                       [](const TrackDefinition& t) { return t.Title.rfind("CAPTAIN NEO", 0) == 0; });
    const std::uint8_t cmdByte =
        trackIt != def.Tracks.end() ? static_cast<std::uint8_t>(std::stoul(trackIt->Command, nullptr, 0)) : 0x06;
    MusicCommand cmd;
    cmd.Kind = MusicCommandKind::Raw;
    cmd.Value = cmdByte;
    machineBase->SendCommand(cmd);

    std::unordered_map<std::uint16_t, std::uint64_t> afterCmdPcHits;
    std::uint64_t afterCmdModeSelectWrites = 0;
    darius->AudioCpuForTest().SetOpcodeFetchObserverForTest(
        [&afterCmdPcHits](std::uint16_t pc, std::uint8_t) { ++afterCmdPcHits[pc]; });
    darius->AudioCpuForTest().SetMemoryWriteObserverForTest(
        [&afterCmdModeSelectWrites](std::uint16_t addr, std::uint8_t) {
            if (addr == 0xB000) ++afterCmdModeSelectWrites;
        });
    for (int i = 0; i < 40; ++i) machineBase->Render(EmuDuration{Mixer::kBlockSizeFrames}, nullptr);
    darius->AudioCpuForTest().SetOpcodeFetchObserverForTest(nullptr);
    darius->AudioCpuForTest().SetMemoryWriteObserverForTest(nullptr);

    auto printTopPcs = [&](const char* label, const std::unordered_map<std::uint16_t, std::uint64_t>& hits,
                            std::uint64_t modeSelectWrites) {
        std::vector<std::pair<std::uint16_t, std::uint64_t>> sorted(hits.begin(), hits.end());
        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        std::printf("  %s: %zu distinct PCs fetched, 0xB000 (PC060HA mode-select) written %llu times\n", label,
                    sorted.size(), (unsigned long long)modeSelectWrites);
        const std::size_t topN = std::min<std::size_t>(20, sorted.size());
        for (std::size_t i = 0; i < topN; ++i) {
            const std::uint16_t pc = sorted[i].first;
            const std::uint8_t b0 = darius->ReadAudioCpuMemory(pc);
            const std::uint8_t b1 = darius->ReadAudioCpuMemory(static_cast<std::uint16_t>(pc + 1));
            const std::uint8_t b2 = darius->ReadAudioCpuMemory(static_cast<std::uint16_t>(pc + 2));
            std::printf("    PC=%04X hits=%llu bytes=%02X %02X %02X\n", pc, (unsigned long long)sorted[i].second, b0,
                        b1, b2);
        }
    };
    char phaseBLabel[64];
    std::snprintf(phaseBLabel, sizeof(phaseBLabel), "Phase B (immediately after sending cmd 0x%02X)", cmdByte);
    printTopPcs("Phase A (idle, no command sent)", idlePcHits, idleModeSelectWrites);
    printTopPcs(phaseBLabel, afterCmdPcHits, afterCmdModeSelectWrites);
}

// ---------------------------------------------------------------------
// Song-switch regressions (SendCommand() sends the driver's own STOP, then renders a silent gap,
// before the requested command when a song is already running).
// ---------------------------------------------------------------------

namespace {

std::unique_ptr<IMachine> ResolveAndConstructDarius(const char* callerLabel) {
    const char* romRootEnv = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRootEnv == nullptr || romRootEnv[0] == '\0') {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is not set - %s not exercised (Build gate stays green without the "
                     "ROMs)\n",
                    callerLabel);
        return nullptr;
    }
    const auto path = FindDariusDefinitionFile();
    if (!path.has_value()) {
        std::printf("  SKIP: xml/darius.xml not found (run from the repo root)\n");
        return nullptr;
    }
    const auto text = ReadFileText(*path);
    if (!text.has_value()) {
        std::printf("  SKIP: darius.xml could not be read\n");
        return nullptr;
    }
    const XmlParseResult parsed = ParseGameDefinition(*text);
    if (parsed.Status != XmlParseStatus::Ok) {
        std::printf("  SKIP: darius.xml did not parse (%s)\n", parsed.Detail.c_str());
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
        std::printf("  SKIP: Darius ROM set not found under CHIPESTRA_ROM_ROOT - %s not exercised\n", callerLabel);
        return nullptr;
    }
    std::unique_ptr<IMachine> machine =
        DefaultMachineRegistry().Create(MachineId(def.Machine), VariantId(def.Variant), res.Resources);
    if (machine) machine->Initialize();
    return machine;
}

constexpr int kDariusBlocksPerSecond = 750;  // 750*256/192000 = 1.000s exactly

void SendDariusCommand(IMachine& machine, std::uint8_t value) {
    MusicCommand c;
    c.Kind = MusicCommandKind::Raw;
    c.Value = value;
    machine.SendCommand(c);
}

// Renders `seconds` seconds, discarding the output. The same `sink` must be used for every Render() of one
// Machine: the Machine registers its channels with the first sink it is given.
void RenderDariusSeconds(IMachine& machine, LogicalChannelSink& sink, int seconds) {
    for (int second = 0; second < seconds; ++second) {
        sink.ClearAll();
        for (int i = 0; i < kDariusBlocksPerSecond; ++i) machine.Render(EmuDuration{Mixer::kBlockSizeFrames}, &sink);
    }
}

// Renders `seconds` seconds and returns each LogicalChannel's peak per second ([second][channel]).
std::vector<std::vector<double>> DariusPeaksPerSecond(IMachine& machine, LogicalChannelSink& sink, int seconds) {
    const std::size_t channelCount = machine.LogicalChannels().size();
    std::vector<std::vector<double>> peaks(seconds, std::vector<double>(channelCount, 0.0));
    for (int second = 0; second < seconds; ++second) {
        sink.ClearAll();
        for (int i = 0; i < kDariusBlocksPerSecond; ++i) machine.Render(EmuDuration{Mixer::kBlockSizeFrames}, &sink);
        for (std::size_t ch = 0; ch < channelCount; ++ch) {
            peaks[second][ch] = PeakAbs(sink, static_cast<ChannelId>(ch));
        }
    }
    return peaks;
}

int DariusDifferingCells(const std::vector<std::vector<double>>& a, const std::vector<std::vector<double>>& b, int fromSecond,
                         int toSecondExclusive, double tolerance) {
    int mismatching = 0;
    for (int second = fromSecond; second < toSecondExclusive; ++second) {
        for (std::size_t ch = 0; ch < a[second].size(); ++ch) {
            if (std::fabs(a[second][ch] - b[second][ch]) > tolerance) ++mismatching;
        }
    }
    return mismatching;
}

}  // namespace

void DariusSongSwitchDoesNotMixSongs() {
    std::printf("DariusSongSwitchDoesNotMixSongs\n");
    constexpr int kSeconds = 12;

    auto refMachine = ResolveAndConstructDarius("song-switch regression");
    if (!refMachine) return;
    LogicalChannelSink refSink;
    SendDariusCommand(*refMachine, 0x07);
    const auto ref = DariusPeaksPerSecond(*refMachine, refSink, kSeconds);

    // 0x28 is a looping SE: without the STOP it would keep sounding under whatever is chosen next.
    auto switchMachine = ResolveAndConstructDarius("song-switch regression");
    if (!switchMachine) return;
    LogicalChannelSink switchSink;
    SendDariusCommand(*switchMachine, 0x28);
    RenderDariusSeconds(*switchMachine, switchSink, 10);
    SendDariusCommand(*switchMachine, 0x07);
    const auto switched = DariusPeaksPerSecond(*switchMachine, switchSink, kSeconds);

    const int mismatching = DariusDifferingCells(ref, switched, 3, kSeconds - 1, 0.012);
    std::printf("  cells differing between \"0x28 then 0x07\" and \"0x07 alone\" (seconds 3..%d): %d\n", kSeconds - 2,
                mismatching);
    Check(mismatching <= 3,
          "song switch: 0x07 sent over a running looping SE (0x28) sounds like 0x07 alone (the SE does not keep playing)");
}

void DariusSwitchFromBgmToSeStopsTheBgm() {
    std::printf("DariusSwitchFromBgmToSeStopsTheBgm\n");
    constexpr int kSeconds = 10;

    auto refMachine = ResolveAndConstructDarius("BGM-to-SE switch regression");
    if (!refMachine) return;
    LogicalChannelSink refSink;
    SendDariusCommand(*refMachine, 0x28);
    const auto ref = DariusPeaksPerSecond(*refMachine, refSink, kSeconds);

    auto switchMachine = ResolveAndConstructDarius("BGM-to-SE switch regression");
    if (!switchMachine) return;
    LogicalChannelSink switchSink;
    SendDariusCommand(*switchMachine, 0x06);
    RenderDariusSeconds(*switchMachine, switchSink, 8);
    SendDariusCommand(*switchMachine, 0x28);
    const auto switched = DariusPeaksPerSecond(*switchMachine, switchSink, kSeconds);

    const int mismatching = DariusDifferingCells(ref, switched, 2, kSeconds, 0.012);
    std::printf("  cells differing between \"0x06 then 0x28\" and \"0x28 alone\" (seconds 2..%d): %d\n", kSeconds - 1,
                mismatching);
    Check(mismatching <= 3,
          "song switch: 0x28 chosen over a running BGM (0x06) sounds like 0x28 alone (the BGM stops)");
}

// STOP chosen as a track (type="control"): Player sends 0x00 but does not start rendering for such a track, so
// the STOP is still unprocessed when the next track is selected. A STOP must count as "something was sent" so
// the next switch still inserts its own STOP and silent gap (otherwise the new command overwrites the pending
// 0x00 and the previous looping SE keeps sounding).
void DariusSongSwitchThroughUnrenderedStopDoesNotMixSongs() {
    std::printf("DariusSongSwitchThroughUnrenderedStopDoesNotMixSongs\n");
    constexpr int kSeconds = 12;

    auto refMachine = ResolveAndConstructDarius("song-switch-through-STOP regression");
    if (!refMachine) return;
    LogicalChannelSink refSink;
    SendDariusCommand(*refMachine, 0x07);
    const auto ref = DariusPeaksPerSecond(*refMachine, refSink, kSeconds);

    auto switchMachine = ResolveAndConstructDarius("song-switch-through-STOP regression");
    if (!switchMachine) return;
    LogicalChannelSink switchSink;
    SendDariusCommand(*switchMachine, 0x28);
    RenderDariusSeconds(*switchMachine, switchSink, 10);
    SendDariusCommand(*switchMachine, 0x00);  // the STOP track: delivered, but nothing is rendered afterwards
    SendDariusCommand(*switchMachine, 0x07);
    const auto switched = DariusPeaksPerSecond(*switchMachine, switchSink, kSeconds);

    const int mismatching = DariusDifferingCells(ref, switched, 3, kSeconds - 1, 0.012);
    std::printf("  cells differing between \"0x28, STOP (not rendered), 0x07\" and \"0x07 alone\" (seconds 3..%d): %d\n",
                kSeconds - 2, mismatching);
    Check(mismatching <= 3,
          "song switch through an unrendered STOP: 0x07 sounds like 0x07 alone (the earlier looping SE does not keep playing)");
}
