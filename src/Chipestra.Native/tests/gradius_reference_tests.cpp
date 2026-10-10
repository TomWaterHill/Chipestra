// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Real Gradius ROM bring-up native tests.
//
// This file never hard-codes a ROM Root path, and only ever prints derived
// metadata (peak levels, command sweep counts) - never ROM-derived sample
// data. The crc32/size/filename constants below are MAME's own public
// ROM-catalog metadata, not ROM byte content, matching the convention
// outrun.xml/afterburn2.xml's <rom> attributes already use.
//
// GradiusAy8910AndK005289RealRomBringUp() builds its RequiredResource list
// directly instead of going through GameDefinition/XML
// (ResourceResolver::Resolve() takes that list regardless of where it came
// from). xml/gradius.xml is validated separately by
// GradiusDefinitionXmlIsValid(), matching OutRunDefinitionXmlIsValid's
// pattern (outrun_reference_tests.cpp).
//
// This ROM set (archive="gradius", gradius.zip) is the Mask ROM (GX400,
// later-revision) board - distinct from the original Bubble System board
// (gradiusb.zip, ~96s of its own warm-up, bubble-memory specific boot
// sequence). This project targets the Mask ROM set.

#include "gradius_reference_tests.h"

#include "default_machine_catalog.h"
#include "devices/konami/gradius/gradius_machine.h"
#include "devices/konami/gradius/gradius_rom_layout.h"
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
#include <optional>
#include <sstream>
#include <string>
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

// The "gradius" archive's sound-relevant ROMs, plus the main 68000's
// "maincpu"/"maindata" ROMs: the main CPU's boot code is required to satisfy
// the shared-RAM handshake the sound Z80's own boot sequence stalls on
// without it.
std::vector<RequiredResource> GradiusRequiredResources() {
    RequiredResource soundcpu;
    soundcpu.LogicalName = "soundcpu";
    soundcpu.AcceptedNames = {"400-e03.5l"};
    soundcpu.SizeBytes = 0x2000;
    soundcpu.Crc32 = 0xa5a8e57d;
    soundcpu.Archive = "gradius";

    RequiredResource wave0;
    wave0.LogicalName = "wave0";
    wave0.AcceptedNames = {"400-a01.fse"};
    wave0.SizeBytes = 0x100;
    wave0.Crc32 = 0x5827b1e8;
    wave0.Archive = "gradius";

    RequiredResource wave1;
    wave1.LogicalName = "wave1";
    wave1.AcceptedNames = {"400-a02.fse"};
    wave1.SizeBytes = 0x100;
    wave1.Crc32 = 0x2f44f970;
    wave1.Archive = "gradius";

    // ROM_START(gradius)'s "maincpu" region (2x32KB, ROM_LOAD16_BYTE
    // interleaved) - the main 68000's own boot/game-logic program.
    RequiredResource maincpuHi;
    maincpuHi.LogicalName = "maincpu_hi";
    maincpuHi.AcceptedNames = {"400-a06.15l"};
    maincpuHi.SizeBytes = 0x8000;
    maincpuHi.Crc32 = 0xb99d8cff;
    maincpuHi.Archive = "gradius";

    RequiredResource maincpuLo;
    maincpuLo.LogicalName = "maincpu_lo";
    maincpuLo.AcceptedNames = {"400-a04.10l"};
    maincpuLo.SizeBytes = 0x8000;
    maincpuLo.Crc32 = 0xd02c9552;
    maincpuLo.Archive = "gradius";

    // ROM_START(gradius)'s "maindata" region (2x128KB, ROM_LOAD16_BYTE
    // interleaved) - the "bubble" ROM pair; not needed by the sound Z80
    // but mapped directly as ROM by the main CPU.
    RequiredResource maindataHi;
    maindataHi.LogicalName = "maindata_hi";
    maindataHi.AcceptedNames = {"456-a07.17l"};
    maindataHi.SizeBytes = 0x20000;
    maindataHi.Crc32 = 0x92df792c;
    maindataHi.Archive = "gradius";

    RequiredResource maindataLo;
    maindataLo.LogicalName = "maindata_lo";
    maindataLo.AcceptedNames = {"456-a05.12l"};
    maindataLo.SizeBytes = 0x20000;
    maindataLo.Crc32 = 0x5cafb263;
    maindataLo.Archive = "gradius";

    return {soundcpu, wave0, wave1, maincpuHi, maincpuLo, maindataHi, maindataLo};
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

// Locate xml/gradius.xml: relative to cwd (how tools/
// verify.ps1 runs the suite), else walk up a few parents (matches
// outrun_reference_tests.cpp's own FindOutRunDefinitionFile()).
std::optional<std::filesystem::path> FindGradiusDefinitionFile() {
    const std::filesystem::path relative = std::filesystem::path("xml") / "gradius.xml";
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

struct CommandProbe {
    double ay8910Peak = 0.0;   // max |sample| across LogicalChannel ids 0-5
    double k005289Peak = 0.0;  // max |sample| across LogicalChannel ids 6-7
    double vlm5030Peak = 0.0;  // LogicalChannel id 8
};

CommandProbe MeasurePeaks(LogicalChannelSink& sink) {
    CommandProbe probe;
    for (ChannelId id = 0; id < 6; ++id) {
        probe.ay8910Peak = std::max(probe.ay8910Peak, PeakAbs(sink, id));
    }
    for (ChannelId id = 6; id < 8; ++id) {
        probe.k005289Peak = std::max(probe.k005289Peak, PeakAbs(sink, id));
    }
    probe.vlm5030Peak = PeakAbs(sink, 8);
    return probe;
}

}  // namespace

void GradiusAy8910AndK005289RealRomBringUp() {
    std::printf("GradiusAy8910AndK005289RealRomBringUp\n");

    const char* romRootEnv = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRootEnv == nullptr || romRootEnv[0] == '\0') {
        std::printf(
            "  SKIP: CHIPESTRA_ROM_ROOT is not set - real-ROM bring-up not exercised\n");
        return;
    }

    const ResourceResolver resolver{std::filesystem::path(romRootEnv)};
    const ResourceResolutionResult res = resolver.Resolve(GradiusRequiredResources());
    if (!res.Ok) {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is set but Gradius ROMs did not resolve under it (archive=\"gradius\")\n");
        for (const auto& f : res.Failures) {
            std::printf("    resolve fail '%s' code %d (%s)\n", f.LogicalName.c_str(), static_cast<int>(f.Result),
                        f.Detail.c_str());
        }
        return;
    }
    const GradiusSoundRoms roms = AssembleGradiusSoundRoms(res.Resources);
    Check(roms.Z80Program.size() == 0x2000, "bring-up: sound Z80 program ROM assembled at the expected 8KB");
    Check(roms.K005289WaveformProm.size() == K005289DeviceAdapter::kWaveformPromTotalBytes,
          "bring-up: K005289 waveform PROM assembled at the expected 512 bytes");
    const GradiusMainCpuRoms mainRoms = AssembleGradiusMainCpuRoms(res.Resources);
    Check(mainRoms.MainProgram.size() == 0x10000, "bring-up: main CPU program ROM de-interleaved to the expected 64KB");
    Check(mainRoms.MainData.size() == 0x40000, "bring-up: main CPU data ROM de-interleaved to the expected 256KB");

    // --- boot + command sweep, on ONE persistent Machine ------------------
    // The real driver needs several hundred canonical blocks of cumulative
    // real time - not just Initialize()'s own kBootWarmupFrames warm-up -
    // before it starts touching ay2/K005289 at all (it appears to run some
    // longer self-test/settle sequence first, paced by the VBLANK NMI). A
    // fresh Machine per probed command (afterburner2_reference_tests.cpp's
    // ProbeCommand pattern) would reset that progress every time - so this
    // sweep instead sends every command to the SAME long-running Machine,
    // sequentially, clearing the sink between commands so each command's
    // peak is still measured independently.
    GradiusMachine machine(roms.Z80Program, roms.K005289WaveformProm, mainRoms.MainProgram, mainRoms.MainData);
    machine.Initialize();
    const auto bootSnap = machine.Z80Snapshot();
    Check(!bootSnap.halted || bootSnap.pc < GradiusMemoryMap::kRomSize,
          "bring-up: after Initialize()'s boot warm-up, the sound Z80 is either running or idling inside ROM "
          "(not stuck outside it)");
    std::printf("  after init: PC=%04X SP=%04X IM=%u halted=%d IFF1=%d irqServiceCount=%llu\n", bootSnap.pc,
                bootSnap.sp, bootSnap.im, bootSnap.halted ? 1 : 0, bootSnap.iff1 ? 1 : 0,
                (unsigned long long)bootSnap.irqServiceCount);
    // IFF1 being set is the success signal of the shared-RAM boot handshake;
    // it reliably sets within kBootWarmupMaxFrames (30s - comfortably above
    // the ~24s this Mask ROM (GX400, gradius.zip) set is known to need).
    Check(bootSnap.iff1, "sound Z80 IFF1 is set - the shared-RAM boot handshake resolved");

    LogicalChannelSink sink;
    constexpr int kInitialSettleBlocks = 600;  // ~47 vblank periods
    for (int i = 0; i < kInitialSettleBlocks; ++i) {
        machine.Render(EmuDuration{Mixer::kBlockSizeFrames}, &sink);
    }

    constexpr double kSilenceFloor = 0.002;
    constexpr int kMeasureBlocksPerCommand = 24;
    bool anyAy8910Audio = false;
    bool anyK005289Audio = false;
    bool anyVlm5030Audio = false;
    double bestAy8910Peak = 0.0;
    double bestK005289Peak = 0.0;
    double bestVlm5030Peak = 0.0;
    std::uint8_t bestAy8910Command = 0;
    std::uint8_t bestK005289Command = 0;
    std::uint8_t bestVlm5030Command = 0;
    int ay8910Hits = 0;
    int k005289Hits = 0;
    int vlm5030Hits = 0;
    // "Beginning Of The History (AIR BATTLE BGM)" (xml/gradius.xml track
    // id=1), captured during the sweep below rather than sent a second time.
    constexpr std::uint8_t kBeginningOfTheHistoryCommand = 0x41;
    CommandProbe beginningOfTheHistoryProbe;

    for (int c = 0x00; c <= 0xFF; ++c) {
        sink.ClearAll();

        MusicCommand cmd;
        cmd.Kind = MusicCommandKind::Raw;
        cmd.Value = static_cast<std::uint8_t>(c);
        machine.SendCommand(cmd);

        for (int i = 0; i < kMeasureBlocksPerCommand; ++i) {
            machine.Render(EmuDuration{Mixer::kBlockSizeFrames}, &sink);
        }

        const CommandProbe probe = MeasurePeaks(sink);
        if (c == kBeginningOfTheHistoryCommand) {
            beginningOfTheHistoryProbe = probe;
        }
        if (probe.ay8910Peak > kSilenceFloor) {
            anyAy8910Audio = true;
            ++ay8910Hits;
            if (probe.ay8910Peak > bestAy8910Peak) {
                bestAy8910Peak = probe.ay8910Peak;
                bestAy8910Command = static_cast<std::uint8_t>(c);
            }
        }
        if (probe.k005289Peak > kSilenceFloor) {
            anyK005289Audio = true;
            ++k005289Hits;
            if (probe.k005289Peak > bestK005289Peak) {
                bestK005289Peak = probe.k005289Peak;
                bestK005289Command = static_cast<std::uint8_t>(c);
            }
        }
        if (probe.vlm5030Peak > kSilenceFloor) {
            anyVlm5030Audio = true;
            ++vlm5030Hits;
            if (probe.vlm5030Peak > bestVlm5030Peak) {
                bestVlm5030Peak = probe.vlm5030Peak;
                bestVlm5030Command = static_cast<std::uint8_t>(c);
            }
        }
    }

    std::printf(
        "  command sweep (256 bytes, one persistent Machine): AY8910 audible on %d, K005289 audible on %d, "
        "VLM5030 audible on %d\n",
        ay8910Hits, k005289Hits, vlm5030Hits);
    std::printf(
        "  strongest AY8910: cmd 0x%02X peak=%.4f | strongest K005289: cmd 0x%02X peak=%.4f | strongest VLM5030: "
        "cmd 0x%02X peak=%.4f\n",
        bestAy8910Command, bestAy8910Peak, bestK005289Command, bestK005289Peak, bestVlm5030Command, bestVlm5030Peak);
    std::printf("  cmd 0x%02X \"Beginning Of The History\": AY8910 peak=%.4f, K005289 peak=%.4f, VLM5030 peak=%.4f\n",
                kBeginningOfTheHistoryCommand, beginningOfTheHistoryProbe.ay8910Peak,
                beginningOfTheHistoryProbe.k005289Peak, beginningOfTheHistoryProbe.vlm5030Peak);

    Check(anyAy8910Audio, "at least one command produces audible AY8910 output on the real ROM");

    Check(anyK005289Audio, "K005289: at least one command produces audible output on the real ROM");
    // VLM5030 (speech) is NOT Check()-enforced, unlike AY8910/K005289 above:
    // no specific voice/speech command is documented in xml/gradius.xml, and
    // real voice samples may be triggered by main-CPU game-state logic
    // rather than a raw command byte.
    std::printf(anyVlm5030Audio
                     ? "  VLM5030: at least one command produced audible output\n"
                     : "  VLM5030: no command produced audible output - may need a main-CPU-driven "
                       "trigger this raw-command sweep doesn't reach\n");

    Check(beginningOfTheHistoryProbe.ay8910Peak > kSilenceFloor || beginningOfTheHistoryProbe.k005289Peak > kSilenceFloor,
          "command 0x41 \"Beginning Of The History\" (AIR BATTLE BGM) produces audible output");
}

void GradiusDefinitionXmlIsValid() {
    std::printf("GradiusDefinitionXmlIsValid\n");

    const auto path = FindGradiusDefinitionFile();
    Check(path.has_value(), "definition xml: xml/gradius.xml is present (run from the repo root)");
    if (!path.has_value()) {
        return;
    }

    const auto text = ReadFileText(*path);
    Check(text.has_value(), "definition xml: file is readable");
    if (!text.has_value()) {
        return;
    }

    const XmlParseResult parsed = ParseGameDefinition(*text);
    Check(parsed.Status == XmlParseStatus::Ok, "definition xml: parses cleanly");
    if (parsed.Status != XmlParseStatus::Ok) {
        std::printf("  detail: %s\n", parsed.Detail.c_str());
        return;
    }

    const GameDefinition& def = parsed.Definition;
    Check(def.Machine == "KONAMI_GRADIUS_BUBBLESYSTEM", "definition xml: machine == KONAMI_GRADIUS_BUBBLESYSTEM");
    Check(def.Variant == "GRADIUS_BASE", "definition xml: variant == GRADIUS_BASE");
    // GradiusMachine needs the main 68000 CPU's four ROM resources; without
    // them the machine would get an all-zero main-CPU ROM that never
    // completes its shared-RAM boot handshake. So 7: soundcpu/wave0/wave1 +
    // maincpu_hi/maincpu_lo/maindata_hi/maindata_lo.
    Check(def.Resources.size() == 7,
          "definition xml: seven ROM resources declared (soundcpu + wave0 + wave1 + maincpu_hi + maincpu_lo + "
          "maindata_hi + maindata_lo)");

    bool haveSoundcpu = false;
    bool haveWave0 = false;
    bool haveWave1 = false;
    bool haveMainCpuHi = false;
    bool haveMainCpuLo = false;
    bool haveMainDataHi = false;
    bool haveMainDataLo = false;
    bool allHaveCrc = true;
    for (const RomResourceDefinition& rom : def.Resources) {
        haveSoundcpu = haveSoundcpu || rom.Name == "soundcpu";
        haveWave0 = haveWave0 || rom.Name == "wave0";
        haveWave1 = haveWave1 || rom.Name == "wave1";
        haveMainCpuHi = haveMainCpuHi || rom.Name == "maincpu_hi";
        haveMainCpuLo = haveMainCpuLo || rom.Name == "maincpu_lo";
        haveMainDataHi = haveMainDataHi || rom.Name == "maindata_hi";
        haveMainDataLo = haveMainDataLo || rom.Name == "maindata_lo";
        allHaveCrc = allHaveCrc && rom.Crc32.has_value();
    }
    Check(haveSoundcpu && haveWave0 && haveWave1,
          "definition xml: soundcpu + wave0 + wave1 resources present by logical name (matches "
          "AssembleGradiusSoundRoms's own expectations)");
    Check(haveMainCpuHi && haveMainCpuLo && haveMainDataHi && haveMainDataLo,
          "definition xml: maincpu_hi + maincpu_lo + maindata_hi + maindata_lo resources present by logical name "
          "(matches AssembleGradiusMainCpuRoms's own expectations)");
    Check(allHaveCrc, "definition xml: every <rom> carries a crc32 (MAME-catalog metadata, not ROM content)");
    Check(!def.Tracks.empty(), "definition xml: at least one <track> is declared");

    const MachineDefinition* machineDef = DefaultMachineRegistry().Find(MachineId(def.Machine));
    Check(machineDef != nullptr, "definition xml: the referenced machine (KONAMI_GRADIUS_BUBBLESYSTEM) is in the catalog");
    if (machineDef != nullptr) {
        const bool variantOk = std::any_of(
            machineDef->Variants.begin(), machineDef->Variants.end(),
            [&](const MachineVariant& v) { return v.Id.Value() == def.Variant; });
        Check(variantOk, "definition xml: the referenced variant (GRADIUS_BASE) exists on that machine");
    }
}

// Regression guard for xml/gradius.xml's own declared <resources> list:
// GradiusAy8910AndK005289RealRomBringUp above builds its RequiredResource
// list by hand, bypassing the game XML. This test is the only one in this
// file that resolves resources FROM the parsed XML's own def.Resources and
// constructs the Machine via DefaultMachineRegistry().Create() (the same two
// steps chipestra_load_machine performs), rather than calling
// GradiusMachine's constructor directly.
void GradiusViaDefinitionXmlRealRomBringUp() {
    std::printf("GradiusViaDefinitionXmlRealRomBringUp\n");

    const char* romRootEnv = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRootEnv == nullptr || romRootEnv[0] == '\0') {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is not set - real-ROM bring-up not exercised\n");
        return;
    }

    const auto path = FindGradiusDefinitionFile();
    Check(path.has_value(), "xml bring-up: xml/gradius.xml is present (run from the repo root)");
    if (!path.has_value()) return;
    const auto text = ReadFileText(*path);
    Check(text.has_value(), "xml bring-up: file is readable");
    if (!text.has_value()) return;
    const XmlParseResult parsed = ParseGameDefinition(*text);
    Check(parsed.Status == XmlParseStatus::Ok, "xml bring-up: gradius.xml parses cleanly");
    if (parsed.Status != XmlParseStatus::Ok) return;
    const GameDefinition& def = parsed.Definition;

    // Exactly what Player.LoadGame(gameId, romRoot) does (NativeRuntimeSession
    // .LoadMachineWithResources -> chipestra_load_machine): turn every <rom> the
    // XML itself declares into a RequiredResource and resolve THAT list -
    // nothing hand-added, nothing hand-omitted. If gradius.xml ever loses
    // one of the four main-CPU resources, this loop won't secretly
    // compensate for it the way a hand-built list would.
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
    Check(res.Ok, "xml bring-up: every resource gradius.xml declares resolves under CHIPESTRA_ROM_ROOT (archive=\"gradius\")");
    if (!res.Ok) {
        for (const auto& f : res.Failures) {
            std::printf("    resolve fail '%s' code %d (%s)\n", f.LogicalName.c_str(), static_cast<int>(f.Result),
                        f.Detail.c_str());
        }
        return;
    }

    std::unique_ptr<IMachine> machine =
        DefaultMachineRegistry().Create(MachineId(def.Machine), VariantId(def.Variant), res.Resources);
    Check(machine != nullptr, "xml bring-up: DefaultMachineRegistry().Create() builds a Machine from the XML's own "
                              "machine/variant/resources");
    if (!machine) return;
    machine->Initialize();

    // "Beginning Of The History (AIR BATTLE BGM)" - looked up by title from
    // the XML itself (not hardcoded as 0x41 - if the XML's own command byte
    // for this track ever changes, this test should follow it, not silently
    // test a stale value).
    const auto trackIt = std::find_if(def.Tracks.begin(), def.Tracks.end(), [](const TrackDefinition& t) {
        return t.Title.rfind("Beginning Of The History", 0) == 0;
    });
    Check(trackIt != def.Tracks.end(), "xml bring-up: \"Beginning Of The History\" track is declared");
    if (trackIt == def.Tracks.end()) return;
    const std::uint8_t commandByte = static_cast<std::uint8_t>(std::stoul(trackIt->Command, nullptr, 0));

    LogicalChannelSink sink;
    constexpr int kSettleBlocks = 600;  // matches GradiusAy8910AndK005289RealRomBringUp's own settle
    for (int i = 0; i < kSettleBlocks; ++i) {
        machine->Render(EmuDuration{Mixer::kBlockSizeFrames}, &sink);
    }

    MusicCommand cmd;
    cmd.Kind = MusicCommandKind::Raw;
    cmd.Value = commandByte;
    machine->SendCommand(cmd);

    sink.ClearAll();
    // Long enough that genuine oscillation (which only starts ~0.82s into
    // this command) is measured, and to cross at least one of the ~5s-period
    // VLM5030 boot-announcement re-triggers seen when the main-CPU ROM is
    // missing - in that case K005289 stays silent across this whole window.
    constexpr int kMeasureBlocks = (192000 / static_cast<int>(Mixer::kBlockSizeFrames)) * 8;  // 8s
    for (int i = 0; i < kMeasureBlocks; ++i) {
        machine->Render(EmuDuration{Mixer::kBlockSizeFrames}, &sink);
    }

    const CommandProbe probe = MeasurePeaks(sink);
    std::printf("  cmd 0x%02X \"%s\" via XML-declared resources: AY8910 peak=%.4f, K005289 peak=%.4f\n", commandByte,
                trackIt->Title.c_str(), probe.ay8910Peak, probe.k005289Peak);
    constexpr double kSilenceFloor = 0.002;  // matches GradiusAy8910AndK005289RealRomBringUp's own value
    Check(probe.k005289Peak > kSilenceFloor,
          "xml bring-up: K005289 is genuinely audible when the Machine is built from gradius.xml's OWN declared "
          "resources (not a hand-built resource list)");
}
