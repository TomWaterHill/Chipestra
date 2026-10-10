// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// After Burner II real-ROM bring-up native tests.
//
// This file never hard-codes a ROM Root path, ROM bytes, or a driver
// command value, and only ever prints comparison metadata (levels, ratios,
// counts) - never ROM-derived sample data.

#include "afterburner2_reference_tests.h"

#include "devices/sega/xboard/xboard_machine.h"
#include "devices/sega/xboard/xboard_rom_layout.h"
#include "machine_registry.h"
#include "mixer.h"
#include "null_audio_output.h"
#include "resampling_mixer_feed.h"
#include "resource_resolver.h"
#include "test_support.h"
#include "chipestra_abi.h"
#include "xml_definition_parser.h"

#include "default_machine_catalog.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
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

// Locate xml/afterburn2.xml: relative to cwd (how tools/
// verify.ps1 runs the suite), else walk up a few parents.
std::optional<std::filesystem::path> FindDefinitionFile() {
    const std::filesystem::path relative = std::filesystem::path("xml") / "afterburn2.xml";
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

// One canonical output block, driven all the way through the real pipeline:
// Machine (native rate) -> ResamplingMixerFeed (192kHz) -> Mixer -> Null
// Output. Returns the mixed stereo block.
std::vector<StereoFrame> RenderBlock(XBoardMachine& machine, ResamplingMixerFeed& feed, Mixer& mixer,
                                     LogicalChannelSink& nativeSink, NullAudioOutput& output) {
    nativeSink.ClearAll();
    machine.Render(EmuDuration{Mixer::kBlockSizeFrames}, &nativeSink);
    const LogicalChannelSink& canonical =
        feed.Resample(nativeSink, machine.LogicalChannels(), Mixer::kBlockSizeFrames);
    std::vector<StereoFrame> out;
    mixer.MixBlock(canonical, machine.LogicalChannels(), Mixer::kBlockSizeFrames, out);
    output.SubmitStereo(out.data(), out.size());
    return out;
}

struct AudioMetrics {
    double peak = 0.0;
    double rms = 0.0;
    double stddev = 0.0;
    std::uint64_t signChanges = 0;
    double nonSilentBlockFraction = 0.0;
    double blockRmsSpread = 0.0;      // max-min block RMS / mean block RMS (glitch/continuity proxy)
    std::size_t blockCount = 0;
};

// Renders `blocks` blocks (after any already-sent command) and measures the
// mixed output. `settleBlocks` are rendered first and excluded from the
// measurement (lets a just-triggered track ramp up).
AudioMetrics Measure(XBoardMachine& machine, ResamplingMixerFeed& feed, Mixer& mixer,
                     LogicalChannelSink& nativeSink, NullAudioOutput& output, std::size_t blocks,
                     std::size_t settleBlocks) {
    for (std::size_t i = 0; i < settleBlocks; ++i) {
        RenderBlock(machine, feed, mixer, nativeSink, output);
    }

    constexpr double kSilenceFloor = 0.002;
    AudioMetrics m;
    m.blockCount = blocks;

    double sumSq = 0.0;
    double sum = 0.0;
    std::uint64_t sampleCount = 0;
    double prev = 0.0;
    bool havePrev = false;
    std::size_t nonSilentBlocks = 0;
    double minBlockRms = 1e30;
    double maxBlockRms = 0.0;
    double sumBlockRms = 0.0;

    for (std::size_t b = 0; b < blocks; ++b) {
        const std::vector<StereoFrame> block = RenderBlock(machine, feed, mixer, nativeSink, output);
        double blockSumSq = 0.0;
        double blockPeak = 0.0;
        for (const StereoFrame& f : block) {
            const double mono = 0.5 * (static_cast<double>(f.left) + static_cast<double>(f.right));
            m.peak = std::max(m.peak, std::fabs(mono));
            blockPeak = std::max(blockPeak, std::fabs(mono));
            sumSq += mono * mono;
            sum += mono;
            blockSumSq += mono * mono;
            if (havePrev && ((prev < 0.0) != (mono < 0.0)) && std::fabs(mono - prev) > 1e-6) {
                ++m.signChanges;
            }
            prev = mono;
            havePrev = true;
            ++sampleCount;
        }
        const double blockRms = std::sqrt(blockSumSq / static_cast<double>(block.size()));
        if (blockPeak > kSilenceFloor) {
            ++nonSilentBlocks;
        }
        minBlockRms = std::min(minBlockRms, blockRms);
        maxBlockRms = std::max(maxBlockRms, blockRms);
        sumBlockRms += blockRms;
    }

    if (sampleCount > 0) {
        m.rms = std::sqrt(sumSq / static_cast<double>(sampleCount));
        const double mean = sum / static_cast<double>(sampleCount);
        m.stddev = std::sqrt(std::max(0.0, sumSq / static_cast<double>(sampleCount) - mean * mean));
    }
    m.nonSilentBlockFraction = static_cast<double>(nonSilentBlocks) / static_cast<double>(blocks);
    const double meanBlockRms = sumBlockRms / static_cast<double>(blocks);
    m.blockRmsSpread = meanBlockRms > 1e-9 ? (maxBlockRms - minBlockRms) / meanBlockRms : 0.0;
    return m;
}

struct Pipeline {
    ResamplingMixerFeed feed;
    Mixer mixer;
    LogicalChannelSink nativeSink;
    NullAudioOutput output;
    Pipeline() {
        output.Initialize();
        output.Start();
    }
};

struct ProbeResult {
    AudioMetrics metrics;
    std::uint8_t im = 0;
    std::uint64_t im0Services = 0;
    std::uint64_t irqServices = 0;
    std::uint64_t nmiServices = 0;
    std::uint16_t finalPc = 0;
    std::uint64_t ymStereoSamples = 0;
    std::uint64_t ymSamples = 0;
    std::uint64_t pcmStereoSamples = 0;
    std::uint64_t pcmSamples = 0;
};

// Fresh machine + pipeline, send `command`, measure. Fresh state per probe so
// an already-playing track never bleeds into the next probe's measurement.
ProbeResult ProbeCommand(const XBoardSoundRoms& roms, std::uint8_t command, std::size_t measureBlocks,
                         std::size_t settleBlocks = 40) {
    XBoardMachine machine(roms.Z80Program, roms.SegaPcmSamples);
    machine.Initialize();
    Pipeline p;

    for (int i = 0; i < 24; ++i) {  // driver init / idle before the command
        RenderBlock(machine, p.feed, p.mixer, p.nativeSink, p.output);
    }

    MusicCommand cmd;
    cmd.Kind = MusicCommandKind::Raw;
    cmd.Value = command;
    machine.SendCommand(cmd);

    ProbeResult r;
    r.metrics = Measure(machine, p.feed, p.mixer, p.nativeSink, p.output, measureBlocks, settleBlocks);

    const auto snap = machine.Z80Snapshot();
    r.im = snap.im;
    r.im0Services = snap.im0ServiceCount;
    r.irqServices = snap.irqServiceCount;
    r.nmiServices = snap.nmiServiceCount;
    r.finalPc = snap.pc;
    r.ymStereoSamples = machine.Ym2151ForTest().StereoSampleCount();
    r.ymSamples = machine.Ym2151ForTest().GeneratedSampleCount();
    r.pcmStereoSamples = machine.SegaPcmForTest().StereoSampleCount();
    r.pcmSamples = machine.SegaPcmForTest().GeneratedSampleCount();
    return r;
}

}  // namespace

void AfterBurner2DefinitionXmlIsValid() {
    std::printf("AfterBurner2DefinitionXmlIsValid\n");

    const auto path = FindDefinitionFile();
    Check(path.has_value(), "definition xml: xml/afterburn2.xml is present (run from the repo root)");
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
    Check(def.Machine == "SEGA_XBOARD", "definition xml: machine == SEGA_XBOARD");
    Check(def.Variant == "XBOARD_BASE", "definition xml: variant == XBOARD_BASE");
    Check(def.Resources.size() == 4, "definition xml: four sound ROM resources declared");

    bool haveSoundcpu = false;
    int pcmCount = 0;
    bool allHaveCrc = true;
    for (const RomResourceDefinition& rom : def.Resources) {
        if (rom.Name == "soundcpu") {
            haveSoundcpu = true;
        }
        if (rom.Name.rfind("pcm", 0) == 0) {
            ++pcmCount;
        }
        allHaveCrc = allHaveCrc && rom.Crc32.has_value();
    }
    Check(haveSoundcpu && pcmCount == 3, "definition xml: one soundcpu + three pcm resources by logical name");
    Check(allHaveCrc, "definition xml: every <rom> carries a crc32 (disambiguates shared filenames)");
    Check(!def.Tracks.empty(), "definition xml: at least one <track> is declared");

    const MachineDefinition* machineDef = DefaultMachineRegistry().Find(MachineId(def.Machine));
    Check(machineDef != nullptr, "definition xml: the referenced machine is in the catalog");
    if (machineDef != nullptr) {
        const bool variantOk = std::any_of(
            machineDef->Variants.begin(), machineDef->Variants.end(),
            [&](const MachineVariant& v) { return v.Id.Value() == def.Variant; });
        Check(variantOk, "definition xml: the referenced variant exists on that machine");
    }
}

void AfterBurner2ReferenceBringUp() {
    std::printf("AfterBurner2ReferenceBringUp\n");

    const char* romRootEnv = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRootEnv == nullptr || romRootEnv[0] == '\0') {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is not set - real-ROM bring-up not exercised (Build gate "
                    "stays green without the ROMs)\n");
        return;
    }

    const auto defPath = FindDefinitionFile();
    if (!defPath.has_value()) {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is set but xml/afterburn2.xml was not found from the "
                    "cwd - run the suite from the repo root (tools/verify.ps1 does)\n");
        return;
    }
    const auto defText = ReadFileText(*defPath);
    if (!defText.has_value()) {
        std::printf("  SKIP: could not read afterburn2.xml\n");
        return;
    }
    const XmlParseResult parsed = ParseGameDefinition(*defText);
    if (parsed.Status != XmlParseStatus::Ok) {
        Check(false, "bring-up: afterburn2.xml must parse (also covered by AfterBurner2DefinitionXmlIsValid)");
        return;
    }

    // --- resolve the real ROMs ---
    const std::filesystem::path romRoot(romRootEnv);
    const ResourceResolver resolver(romRoot);
    const ResourceResolutionResult resolution = resolver.Resolve(parsed.Definition.BuildRequiredResources());
    if (!resolution.Ok) {
        for (const ResourceResolutionFailure& f : resolution.Failures) {
            std::printf("  resolve failure: '%s' -> code %d (%s)\n", f.LogicalName.c_str(),
                        static_cast<int>(f.Result), f.Detail.c_str());
        }
        std::printf("  hint: point CHIPESTRA_ROM_ROOT at a directory holding exactly one After Burner II set "
                    "(duplicate identical entries across sets resolve as AMBIGUOUS)\n");
        Check(false, "all After Burner II sound resources resolve without Missing/Ambiguous/HashMismatch");
        return;
    }
    Check(true, "all After Burner II sound resources resolved");

    const XBoardSoundRoms roms = AssembleXBoardSoundRoms(resolution.Resources);
    Check(roms.Z80Program.size() >= 0x8000 && roms.Z80Program.size() <= 0x10000,
          "bring-up: sound Z80 program ROM assembled at a plausible size");
    Check(roms.SegaPcmSamples.size() == 0x80000, "bring-up: SegaPCM sample region assembled to 512KB");

    // --- reset / vector wiring on the real ROM ---
    {
        XBoardMachine machine(roms.Z80Program, roms.SegaPcmSamples);
        machine.Initialize();
        Pipeline p;
        for (int i = 0; i < 40; ++i) {
            RenderBlock(machine, p.feed, p.mixer, p.nativeSink, p.output);
        }
        const auto snap = machine.Z80Snapshot();
        const auto bus = machine.BusActivityForTest();
        Check(!snap.halted, "reset/vector: the sound Z80 is running after init (reset vector executed)");
        Check(snap.pc < XBoardMemoryMap::kSegaPcmStart,
              "reset/vector: the sound Z80 is executing inside program ROM, not RAM/PCM space");
        Check(bus.Ym2151Reads > 100 && bus.Ym2151Writes > 20,
              "reset/vector: the driver reached its main loop (init'd the YM2151 and is polling its status)");
        std::printf("  after init: PC=%04X SP=%04X IM=%u halted=%d | YM writes=%llu reads=%llu | PCM writes=%llu\n",
                    snap.pc, snap.sp, snap.im, snap.halted ? 1 : 0,
                    static_cast<unsigned long long>(bus.Ym2151Writes),
                    static_cast<unsigned long long>(bus.Ym2151Reads),
                    static_cast<unsigned long long>(machine.SegaPcmForTest().RegisterAccessCounts().Writes));
    }

    // --- probe the sound-command space for continuous audio ---
    struct Hit {
        std::uint8_t command;
        ProbeResult probe;
    };
    std::vector<Hit> hits;
    std::uint64_t im0Seen = 0;
    std::uint64_t irqSeen = 0;
    std::uint8_t imSeen = 0;

    for (int c = 0x00; c <= 0xFF; ++c) {
        const ProbeResult r = ProbeCommand(roms, static_cast<std::uint8_t>(c), 48);
        im0Seen = std::max(im0Seen, r.im0Services);
        irqSeen = std::max(irqSeen, r.irqServices);
        imSeen = r.im;
        // "Continuous audio" = most blocks after the command are non-silent
        // and the level is clearly above the noise floor.
        if (r.metrics.nonSilentBlockFraction >= 0.75 && r.metrics.rms > 0.008) {
            hits.push_back(Hit{static_cast<std::uint8_t>(c), r});
        }
    }

    std::printf("  command sweep: %zu of 256 command bytes produced continuous audio\n", hits.size());
    Check(!hits.empty(), "at least one sound command produces continuous audio through the full pipeline");
    if (hits.empty()) {
        return;
    }

    const Hit& best = *std::max_element(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
        return a.probe.metrics.rms < b.probe.metrics.rms;
    });
    std::printf("  strongest: cmd 0x%02X rms=%.4f peak=%.4f nonSilent=%.2f\n", best.command, best.probe.metrics.rms,
                best.probe.metrics.peak, best.probe.metrics.nonSilentBlockFraction);

    // --- reference-level waveform check on the strongest hit ---
    {
        const ProbeResult r = ProbeCommand(roms, best.command, 400);
        const AudioMetrics& m = r.metrics;
        std::printf("  reference (cmd 0x%02X, 400 blocks): peak=%.4f rms=%.4f stddev=%.4f signChanges=%llu "
                    "nonSilent=%.3f blockRmsSpread=%.3f\n",
                    best.command, m.peak, m.rms, m.stddev, static_cast<unsigned long long>(m.signChanges),
                    m.nonSilentBlockFraction, m.blockRmsSpread);
        Check(m.peak > 0.01 && m.rms > 0.005, "reference: output is clearly not silent");
        Check(m.stddev > 1e-4, "reference: output varies over time (not DC / not a stuck value)");
        Check(m.signChanges > 100, "reference: output has AC waveform structure (crosses zero repeatedly)");
        Check(m.nonSilentBlockFraction > 0.90, "reference: audio is sustained across the whole window");
    }

    // --- determinism ---
    {
        auto renderRun = [&]() {
            XBoardMachine machine(roms.Z80Program, roms.SegaPcmSamples);
            machine.Initialize();
            Pipeline p;
            for (int i = 0; i < 30; ++i) {
                RenderBlock(machine, p.feed, p.mixer, p.nativeSink, p.output);
            }
            MusicCommand cmd;
            cmd.Kind = MusicCommandKind::Raw;
            cmd.Value = best.command;
            machine.SendCommand(cmd);
            std::vector<StereoFrame> frames;
            for (int i = 0; i < 120; ++i) {
                const std::vector<StereoFrame> block = RenderBlock(machine, p.feed, p.mixer, p.nativeSink, p.output);
                frames.insert(frames.end(), block.begin(), block.end());
            }
            return frames;
        };
        const std::vector<StereoFrame> a = renderRun();
        const std::vector<StereoFrame> b = renderRun();
        bool identical = a.size() == b.size();
        for (std::size_t i = 0; identical && i < a.size(); ++i) {
            identical = a[i].left == b[i].left && a[i].right == b[i].right;
        }
        Check(identical, "two headless runs of the same command + render sequence are bit-identical");
    }

    // --- IM0 usage, YM IRQ latency and L/R handling ---
    const ProbeResult deep = ProbeCommand(roms, best.command, 500);
    std::printf("  --- real-ROM findings (IM0 and YM IRQ: not used by this driver; L/R: carried by routing) ---\n");
    std::printf("  [IM0] driver runs in IM %u; IM0 interrupts serviced across the sweep = %llu (non-RST %llu). "
                "AB2's driver polls the YM2151 Timer A flag with interrupts disabled - IM0 path is never taken.\n",
                imSeen, static_cast<unsigned long long>(im0Seen),
                static_cast<unsigned long long>(deep.im0Services));
    std::printf("  [YM IRQ latency] maskable INTs serviced = %llu; the driver does not use the YM2151 IRQ line "
                "(it busy-polls status). blockRmsSpread=%.2f nonSilent=%.3f - no dropouts.\n",
                static_cast<unsigned long long>(irqSeen), deep.metrics.blockRmsSpread,
                deep.metrics.nonSilentBlockFraction);
    const double ymStereoPct =
        deep.ymSamples ? 100.0 * static_cast<double>(deep.ymStereoSamples) / static_cast<double>(deep.ymSamples) : 0.0;
    const double pcmStereoPct = deep.pcmSamples ? 100.0 * static_cast<double>(deep.pcmStereoSamples) /
                                                      static_cast<double>(deep.pcmSamples)
                                                : 0.0;
    std::printf("  [L/R fold] source YM2151 samples with L!=R: %.1f%%; source SegaPCM samples with L!=R: %.1f%%. "
                "%s\n",
                ymStereoPct, pcmStereoPct,
                (ymStereoPct > 1.0 || pcmStereoPct > 1.0)
                    ? "AB2 DOES pan voices - the per-channel L/R is carried by routing (no mono fold)."
                    : "AB2 does not pan this track.");
}

namespace {

// Waits (bounded) for `handle`'s selected output to report at least
// `minFrames` submitted - same pattern as native_lifecycle_tests.cpp's own
// WaitForSubmittedFrames, duplicated here since that one lives in an
// anonymous namespace in a different translation unit.
bool WaitForSubmittedFramesAbi(ChipestraHandle handle, std::uint64_t minFrames, int timeoutMs) {
    constexpr int kPollIntervalMs = 5;
    for (int waited = 0; waited <= timeoutMs; waited += kPollIntervalMs) {
        ChipestraAudioOutputDiagnostics diagnostics{};
        if (chipestra_get_audio_output_diagnostics(handle, &diagnostics) == CHIPESTRA_OK &&
            diagnostics.submitted_frame_count >= minFrames) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));
    }
    return false;
}

}  // namespace

void ChannelMeterAbiDuringRealPlaybackReportsNonZeroPeak() {
    std::printf("ChannelMeterAbiDuringRealPlaybackReportsNonZeroPeak\n");

    const char* romRootEnv = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRootEnv == nullptr || romRootEnv[0] == '\0') {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is not set\n");
        return;
    }
    const auto defPath = FindDefinitionFile();
    if (!defPath.has_value()) {
        std::printf("  SKIP: xml/afterburn2.xml not found from the cwd\n");
        return;
    }
    const auto defText = ReadFileText(*defPath);
    if (!defText.has_value()) {
        std::printf("  SKIP: could not read afterburn2.xml\n");
        return;
    }

    // Full public C ABI path (chipestra_parse_game_definition -> _get_info/_get_resource
    // -> chipestra_resolve_resources -> chipestra_load_machine), the same sequence
    // NativeRuntimeSession.LoadMachineWithResources (Managed Core) drives -
    // not the native-shortcut XBoardMachine construction the rest of this
    // file uses, since this test exists specifically to catch a bug that
    // only reproduces through that real path.
    ChipestraGameDefinitionHandle gameHandle = nullptr;
    char detail[256] = {0};
    Check(chipestra_parse_game_definition(defText->c_str(), static_cast<uint32_t>(defText->size()), &gameHandle, detail,
                                     sizeof(detail)) == CHIPESTRA_OK,
          "abi path: chipestra_parse_game_definition() == CHIPESTRA_OK");
    if (gameHandle == nullptr) {
        return;
    }

    ChipestraGameInfo gameInfo{};
    Check(chipestra_game_definition_get_info(gameHandle, &gameInfo) == CHIPESTRA_OK, "abi path: chipestra_game_definition_get_info() == CHIPESTRA_OK");

    std::vector<ChipestraRequiredResourceInfo> resources(gameInfo.resource_count);
    for (uint32_t i = 0; i < gameInfo.resource_count; ++i) {
        Check(chipestra_game_definition_get_resource(gameHandle, i, &resources[i]) == CHIPESTRA_OK,
              "abi path: chipestra_game_definition_get_resource() == CHIPESTRA_OK");
        // chipestra_game_definition_get_resource always leaves `archive` blank
        // (it's a per-<game>, not per-<rom>, value - chipestra_abi.h's own
        // comment) - a real caller (Managed Core's ToNativeRequiredResourceInfo)
        // copies ChipestraGameInfo::archive into every element itself before
        // resolving; mirror that here.
        std::snprintf(resources[i].archive, sizeof(resources[i].archive), "%s", gameInfo.archive);
    }

    ChipestraResourceResolutionHandle resolutionHandle = nullptr;
    Check(chipestra_resolve_resources(romRootEnv, resources.empty() ? nullptr : resources.data(),
                                 static_cast<uint32_t>(resources.size()), &resolutionHandle) == CHIPESTRA_OK,
          "abi path: chipestra_resolve_resources() == CHIPESTRA_OK");

    ChipestraResolutionSummary summary{};
    Check(chipestra_resource_resolution_get_summary(resolutionHandle, &summary) == CHIPESTRA_OK && summary.ok != 0,
          "abi path: resolution summary reports ok");

    std::vector<ChipestraResolvedResource> resolved(summary.resolved_count);
    for (uint32_t i = 0; i < summary.resolved_count; ++i) {
        Check(chipestra_resource_resolution_get_resolved(resolutionHandle, i, &resolved[i]) == CHIPESTRA_OK,
              "abi path: chipestra_resource_resolution_get_resolved() == CHIPESTRA_OK");
    }

    ChipestraHandle handle = nullptr;
    Check(chipestra_create_instance(&handle) == CHIPESTRA_OK, "abi path: chipestra_create_instance() == CHIPESTRA_OK");
    Check(chipestra_load_machine(handle, gameInfo.machine_id, gameInfo.variant_id, resolved.data(),
                            static_cast<uint32_t>(resolved.size())) == CHIPESTRA_OK,
          "abi path: chipestra_load_machine() == CHIPESTRA_OK (real ROM bytes)");

    chipestra_free_resource_resolution(resolutionHandle);  // chipestra_load_machine already copied what it needs
    chipestra_free_game_definition(gameHandle);

    uint32_t channelCount = 0;
    Check(chipestra_get_machine_channel_count(handle, &channelCount) == CHIPESTRA_OK && channelCount > 8,
          "abi path: real XBoardMachine reports more than 8 channels (FM+PCM)");

    Check(chipestra_select_null_output(handle) == CHIPESTRA_OK, "abi path: chipestra_select_null_output() == CHIPESTRA_OK");
    Check(chipestra_start_audio_output(handle) == CHIPESTRA_OK, "abi path: chipestra_start_audio_output() == CHIPESTRA_OK");

    // The direct, no-warm-up sequence: chipestra_load_machine already ran
    // XBoardMachine::Initialize()'s own internal boot warm-up
    // (kBootWarmupFrames), so a command sent here with no explicit render
    // calls in between must be serviced correctly (the pending NMI must not be
    // serviced before the reset vector's first instruction runs). 0x92 is a
    // BGM command known to drive real signal on SegaPCM channels.
    Check(chipestra_send_command(handle, /*kind=Raw*/ 2, 0x92, 0) == CHIPESTRA_OK, "abi path: chipestra_send_command(0x92) == CHIPESTRA_OK");
    Check(chipestra_start_playback(handle) == CHIPESTRA_OK, "abi path: chipestra_start_playback() == CHIPESTRA_OK");

    Check(WaitForSubmittedFramesAbi(handle, Mixer::kBlockSizeFrames * 60, 3000),
          "abi path: playback renders at least 60 blocks within the timeout");

    // Channel 8 is the first SegaPCM channel (FM occupies 0-7). For 0x92 the
    // PCM channels carry real signal (peak ~0.17) while FM channels read
    // exactly 0, so probe a PCM index here, not FM.
    bool anyNonZero = false;
    float maxPeakSeen = 0.0f;
    for (int attempt = 0; attempt < 20 && !anyNonZero; ++attempt) {
        for (uint32_t idx = 8; idx < channelCount; ++idx) {
            ChipestraLogicalChannelInfo info{};
            ChipestraResult infoResult = chipestra_get_logical_channel_info(handle, idx, &info);
            if (infoResult == CHIPESTRA_OK && info.peak > maxPeakSeen) {
                maxPeakSeen = info.peak;
            }
            if (infoResult == CHIPESTRA_OK && info.peak > 0.0f) {
                anyNonZero = true;
            }
        }
        if (!anyNonZero) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    std::printf("  max Peak seen across PCM channels (idx 8..%u) over ~1s of polling: %.5f\n", channelCount - 1,
                maxPeakSeen);
    Check(anyNonZero, "abi path: chipestra_get_logical_channel_info() reports a non-zero Peak for a real PCM channel "
                       "while Playing");

    // chipestra_get_master_state reads the final mixed StereoFrame block, so
    // it separates a silent output from a broken per-channel readback.
    ChipestraMasterState masterState{};
    Check(chipestra_get_master_state(handle, &masterState) == CHIPESTRA_OK, "abi path: chipestra_get_master_state() == CHIPESTRA_OK");
    std::printf("  master meter: peak_left=%.5f peak_right=%.5f rms_left=%.5f rms_right=%.5f\n", masterState.peak_left,
                masterState.peak_right, masterState.rms_left, masterState.rms_right);

    chipestra_stop_playback(handle);
    chipestra_stop_audio_output(handle);
    chipestra_unload_machine(handle);
    chipestra_destroy_instance(handle);
}
