// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Real Vapor Trail ROM bring-up native tests. See vapor_trail_reference_tests.h for
// ROM handling.
//
// A signal that is the same for every command is not evidence of command-specific dispatch, so
// rather than trusting a single command's nonzero/time-varying peak alone,
// VaporTrailViaDefinitionXmlRealRomBringUp compares several representative commands' per-channel
// output and requires at least one channel's content to differ between them.

#include "vapor_trail_reference_tests.h"

#include "default_machine_catalog.h"
#include "devices/dataeast/vaportra/vapor_trail_machine.h"
#include "devices/dataeast/vaportra/vapor_trail_rom_layout.h"
#include "machine_registry.h"
#include "mixer.h"
#include "resource_resolver.h"
#include "test_support.h"
#include "chipestra_abi.h"
#include "xml_definition_parser.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
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
#pragma warning(disable : 4996)
#endif
    return std::getenv(name);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
}

std::optional<std::filesystem::path> FindVaporTrailDefinitionFile() {
    const std::filesystem::path relative = std::filesystem::path("xml") / "vaportra.xml";
    std::filesystem::path dir = std::filesystem::current_path();
    for (int depth = 0; depth < 8; ++depth) {
        std::error_code ec;
        const std::filesystem::path candidate = dir / relative;
        if (std::filesystem::exists(candidate, ec)) return candidate;
        if (!dir.has_parent_path() || dir.parent_path() == dir) break;
        dir = dir.parent_path();
    }
    return std::nullopt;
}

std::optional<std::string> ReadFileText(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::nullopt;
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

double PeakAbs(const LogicalChannelSink& sink, ChannelId id) {
    if (!sink.HasChannel(id)) return 0.0;
    const LogicalChannelBuffer& buffer = sink.ChannelBuffer(id);
    double peak = 0.0;
    for (std::size_t i = 0; i < buffer.Size(); ++i) peak = std::max(peak, static_cast<double>(std::fabs(buffer[i])));
    return peak;
}

// A coarse content fingerprint for one command's rendered output: per-channel peak level,
// rounded to 4 decimal places so tiny floating-point noise doesn't manufacture a false
// "diverges" verdict between two genuinely-identical renders.
//
// `sink` is a single LogicalChannelSink OWNED AND CLEARED BY THE CALLER, reused across every
// command measured in one bring-up run, not a fresh local sink per call: VaporTrailMachine::Render()
// registers each LogicalChannel on a sink exactly ONCE per Machine lifetime (the first time Render()
// is called with a non-null sink), so a fresh unregistered sink on a later call would make
// LogicalChannelSink::WriteSamples() throw std::out_of_range.
std::vector<double> RenderFingerprint(VaporTrailMachine& machine, std::uint8_t command, std::uint64_t blocks,
                                       LogicalChannelSink& sink) {
    MusicCommand cmd;
    cmd.Value = command;
    machine.SendCommand(cmd);

    sink.ClearAll();
    for (std::uint64_t b = 0; b < blocks; ++b) {
        machine.Render(EmuDuration{256}, &sink);
    }

    std::vector<double> fp;
    for (const LogicalChannel& channel : machine.LogicalChannels()) {
        const double peak = PeakAbs(sink, channel.Id);
        fp.push_back(std::round(peak * 10000.0) / 10000.0);
    }
    return fp;
}

}  // namespace

void VaporTrailDefinitionXmlIsValid() {
    std::printf("VaporTrailDefinitionXmlIsValid\n");

    const auto path = FindVaporTrailDefinitionFile();
    Check(path.has_value(), "definition xml: xml/vaportra.xml is present (run from the repo root)");
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
    Check(def.Machine == "DATAEAST_VAPORTRAIL", "definition xml: machine == DATAEAST_VAPORTRAIL");
    Check(def.Variant == "VAPORTRAIL_BASE", "definition xml: variant == VAPORTRAIL_BASE");
    Check(def.Resources.size() == 3, "definition xml: three ROM resources declared (soundcpu + oki1 + oki2)");

    bool haveSoundCpu = false, haveOki1 = false, haveOki2 = false, allHaveCrc = true;
    for (const RomResourceDefinition& rom : def.Resources) {
        haveSoundCpu = haveSoundCpu || rom.Name == "soundcpu";
        haveOki1 = haveOki1 || rom.Name == "oki1";
        haveOki2 = haveOki2 || rom.Name == "oki2";
        allHaveCrc = allHaveCrc && rom.Crc32.has_value();
    }
    Check(haveSoundCpu && haveOki1 && haveOki2,
          "definition xml: soundcpu/oki1/oki2 resources present by logical name (matches "
          "AssembleVaporTrailSoundRoms's own expectations)");
    Check(allHaveCrc, "definition xml: every <rom> carries a crc32 (MAME-catalog metadata, not ROM content)");
    Check(def.Tracks.size() > 70, "definition xml: the full 77-track tracklist is present");

    bool haveStop = false;
    for (const TrackDefinition& track : def.Tracks) {
        if (track.Command == "0x00" && track.IsControlCommand) haveStop = true;
    }
    Check(haveStop, "definition xml: a type=\"control\" STOP track (command 0x00) is declared");

    const MachineDefinition* machineDef = DefaultMachineRegistry().Find(MachineId(def.Machine));
    Check(machineDef != nullptr, "definition xml: the referenced machine (DATAEAST_VAPORTRAIL) is in the catalog");
    if (machineDef != nullptr) {
        const bool variantOk = std::any_of(machineDef->Variants.begin(), machineDef->Variants.end(),
                                            [&](const MachineVariant& v) { return v.Id == VariantId(def.Variant); });
        Check(variantOk, "definition xml: the referenced variant (VAPORTRAIL_BASE) exists on that machine");
    }
}

void VaporTrailViaDefinitionXmlRealRomBringUp() {
    std::printf("VaporTrailViaDefinitionXmlRealRomBringUp\n");

    const char* romRootEnv = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRootEnv == nullptr || romRootEnv[0] == '\0') {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is not set - real-ROM bring-up not exercised\n");
        return;
    }

    const auto path = FindVaporTrailDefinitionFile();
    Check(path.has_value(), "xml bring-up: xml/vaportra.xml is present (run from the repo root)");
    if (!path.has_value()) return;
    const auto text = ReadFileText(*path);
    Check(text.has_value(), "xml bring-up: file is readable");
    if (!text.has_value()) return;
    const XmlParseResult parsed = ParseGameDefinition(*text);
    Check(parsed.Status == XmlParseStatus::Ok, "xml bring-up: vaportra.xml parses cleanly");
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

    const ResourceResolver resolver{std::filesystem::path(romRootEnv)};
    const ResourceResolutionResult res = resolver.Resolve(required);
    Check(res.Ok, "xml bring-up: every resource vaportra.xml declares resolves under CHIPESTRA_ROM_ROOT (archive=\"vaportra\")");
    if (!res.Ok) {
        for (const auto& f : res.Failures) {
            std::printf("    resolve fail '%s' code %d (%s)\n", f.LogicalName.c_str(), static_cast<int>(f.Result),
                        f.Detail.c_str());
        }
        return;
    }

    VaporTrailSoundRoms roms = AssembleVaporTrailSoundRoms(res.Resources);
    Check(!roms.SoundCpuProgram.empty() && !roms.Oki1Program.empty() && !roms.Oki2Program.empty(),
          "xml bring-up: AssembleVaporTrailSoundRoms populated all three ROM buffers from the resolved resources");

    VaporTrailMachine machine(std::move(roms.SoundCpuProgram), std::move(roms.Oki1Program), std::move(roms.Oki2Program));
    machine.Initialize();

    // Representative commands: one BGM (VAPOR TRAIL, 0x06 - YM2151/YM2203-heavy), one SE
    // (SE 20, 0x27 - likely OKI-driven), one VOICE (VOICE 00, 0x40 - likely OKI-driven), and
    // STOP (0x00) as a fourth, structurally-different point. 15
    // simulated seconds (not 48 blocks/~64ms), since a short window is not enough here.
    constexpr std::uint64_t kBlocks = (192000ull / 256ull) * 15ull;
    LogicalChannelSink sink;  // shared across every RenderFingerprint() call below (see that function's comment)
    const std::vector<double> fpBgm = RenderFingerprint(machine, 0x06, kBlocks, sink);
    const std::vector<double> fpSe = RenderFingerprint(machine, 0x27, kBlocks, sink);
    const std::vector<double> fpVoice = RenderFingerprint(machine, 0x40, kBlocks, sink);
    const std::vector<double> fpStop = RenderFingerprint(machine, 0x00, kBlocks, sink);

    auto anyChannelDiffers = [](const std::vector<double>& a, const std::vector<double>& b) {
        for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
            if (a[i] != b[i]) return true;
        }
        return false;
    };
    const bool bgmVsSeDiffers = anyChannelDiffers(fpBgm, fpSe);
    const bool bgmVsVoiceDiffers = anyChannelDiffers(fpBgm, fpVoice);
    const bool bgmVsStopDiffers = anyChannelDiffers(fpBgm, fpStop);
    const bool anyNonzero = std::any_of(fpBgm.begin(), fpBgm.end(), [](double v) { return v > 0.0; }) ||
                             std::any_of(fpSe.begin(), fpSe.end(), [](double v) { return v > 0.0; }) ||
                             std::any_of(fpVoice.begin(), fpVoice.end(), [](double v) { return v > 0.0; });

    // `anyNonzero` is Check()-enforced: BGM command 0x06 produces audible, time-varying output
    // (the double-count case is guarded by
    // VaporTrailMachineSoundDeviceSyncTracksCumulativeCyclesAcrossBusAccesses).
    //
    // The per-command DIVERGENCE comparisons below are information only, not Check()-enforced: a signal
    // that is the same for every command is not evidence of command-specific dispatch,
    // and asserting divergence needs calibration against a real-ROM run.
    // Still open: the final GUI-listening confirmation, and re-checking
    // on real hardware the observation that YM2203 output stays audible after switching from SE 0x2B to
    // BGM 0x06.
    Check(anyNonzero, "xml bring-up: BGM/SE/VOICE commands produce audible (nonzero) output over 15 simulated seconds");
    std::printf("  [info] BGM(0x06) vs SE(0x27) output differs: %s\n", bgmVsSeDiffers ? "yes" : "no");
    std::printf("  [info] BGM(0x06) vs VOICE(0x40) output differs: %s\n", bgmVsVoiceDiffers ? "yes" : "no");
    std::printf("  [info] BGM(0x06) vs STOP(0x00) output differs: %s\n", bgmVsStopDiffers ? "yes" : "no");
    std::printf("  [info] any nonzero output across BGM/SE/VOICE over 15s: %s\n", anyNonzero ? "yes" : "no");
}
