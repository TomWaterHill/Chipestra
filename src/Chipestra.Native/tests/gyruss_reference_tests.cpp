// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Real Gyruss ROM bring-up native tests.
//
// This file never hard-codes a ROM Root path, and only ever prints derived
// metadata (peak levels, register values) - never ROM-derived sample data.
// The crc32/size/filename constants in xml/gyruss.xml are MAME's own public
// ROM-catalog metadata (ROM_START(gyruss)), not ROM byte content.
//
// A single command's nonzero/time-varying peak can be a universal
// command-independent side effect, so this file compares TWO distinct
// commands' AY8910 tone-period register trajectories over several seconds
// and requires them to diverge - direct, command-specific evidence that the
// driver dispatches different content per command.

#include "gyruss_reference_tests.h"

#include "default_machine_catalog.h"
#include "devices/konami/gyruss/gyruss_machine.h"
#include "machine_registry.h"
#include "mixer.h"
#include "resource_resolver.h"
#include "test_support.h"
#include "chipestra_abi.h"
#include "xml_definition_parser.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
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

std::optional<std::filesystem::path> FindGyrussDefinitionFile() {
    const std::filesystem::path relative = std::filesystem::path("xml") / "gyruss.xml";
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

bool IsTimeVarying(const LogicalChannelSink& sink, ChannelId id) {
    if (!sink.HasChannel(id)) return false;
    const LogicalChannelBuffer& buffer = sink.ChannelBuffer(id);
    if (buffer.Size() == 0) return false;
    float lo = buffer[0], hi = buffer[0];
    for (std::size_t i = 1; i < buffer.Size(); ++i) {
        lo = std::min(lo, buffer[i]);
        hi = std::max(hi, buffer[i]);
    }
    return (hi - lo) > 1e-4f;
}

// Snapshot of all 5 AY8910 instances' tone-period registers (fine+coarse,
// channels A/B/C = 6 bytes per chip, 30 bytes total) - a coarse but direct
// fingerprint of "what note(s) are currently playing", cheap enough to
// sample once per second across a multi-second window.
struct TonePeriodFingerprint {
    std::uint8_t regs[5][6] = {};

    bool operator==(const TonePeriodFingerprint& other) const {
        for (int chip = 0; chip < 5; ++chip)
            for (int r = 0; r < 6; ++r)
                if (regs[chip][r] != other.regs[chip][r]) return false;
        return true;
    }
};

struct CommandProbeResult {
    double peak = 0.0;
    bool varying = false;
};

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

TonePeriodFingerprint SnapshotTonePeriods(GyrussMachine& machine) {
    TonePeriodFingerprint fp;
    for (int chip = 0; chip < 5; ++chip) {
        for (int r = 0; r < 6; ++r) {
            fp.regs[chip][r] = machine.Ay8910ForTest(chip).RegisterForTest(static_cast<std::uint8_t>(r));
        }
    }
    return fp;
}

}  // namespace

void GyrussDefinitionXmlIsValid() {
    std::printf("GyrussDefinitionXmlIsValid\n");

    const auto path = FindGyrussDefinitionFile();
    Check(path.has_value(), "definition xml: xml/gyruss.xml is present (run from the repo root)");
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
    Check(def.Machine == "KONAMI_GYRUSS", "definition xml: machine == KONAMI_GYRUSS");
    Check(def.Variant == "GYRUSS_BASE", "definition xml: variant == GYRUSS_BASE");
    Check(def.Resources.size() == 3, "definition xml: three ROM resources declared (soundcpu_lo/hi + audio2)");

    bool haveLo = false, haveHi = false, haveAudio2 = false, allHaveCrc = true;
    for (const RomResourceDefinition& rom : def.Resources) {
        haveLo = haveLo || rom.Name == "soundcpu_lo";
        haveHi = haveHi || rom.Name == "soundcpu_hi";
        haveAudio2 = haveAudio2 || rom.Name == "audio2";
        allHaveCrc = allHaveCrc && rom.Crc32.has_value();
    }
    Check(haveLo && haveHi && haveAudio2,
          "definition xml: soundcpu_lo/soundcpu_hi/audio2 resources present by logical name (matches "
          "AssembleGyrussSoundRoms's own expectations)");
    Check(allHaveCrc, "definition xml: every <rom> carries a crc32 (MAME-catalog metadata, not ROM content)");
    Check(!def.Tracks.empty(), "definition xml: at least one <track> is declared");

    const MachineDefinition* machineDef = DefaultMachineRegistry().Find(MachineId(def.Machine));
    Check(machineDef != nullptr, "definition xml: the referenced machine (KONAMI_GYRUSS) is in the catalog");
    if (machineDef != nullptr) {
        const bool variantOk = std::any_of(machineDef->Variants.begin(), machineDef->Variants.end(),
                                            [&](const MachineVariant& v) { return v.Id == VariantId(def.Variant); });
        Check(variantOk, "definition xml: the referenced variant (GYRUSS_BASE) exists on that machine");
    }
}

void GyrussViaDefinitionXmlRealRomBringUp() {
    std::printf("GyrussViaDefinitionXmlRealRomBringUp\n");

    const char* romRootEnv = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRootEnv == nullptr || romRootEnv[0] == '\0') {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is not set - real-ROM bring-up not exercised\n");
        return;
    }

    const auto path = FindGyrussDefinitionFile();
    Check(path.has_value(), "xml bring-up: xml/gyruss.xml is present (run from the repo root)");
    if (!path.has_value()) return;
    const auto text = ReadFileText(*path);
    Check(text.has_value(), "xml bring-up: file is readable");
    if (!text.has_value()) return;
    const XmlParseResult parsed = ParseGameDefinition(*text);
    Check(parsed.Status == XmlParseStatus::Ok, "xml bring-up: gyruss.xml parses cleanly");
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
    Check(res.Ok, "xml bring-up: every resource gyruss.xml declares resolves under CHIPESTRA_ROM_ROOT (archive=\"gyruss\")");
    if (!res.Ok) {
        for (const auto& f : res.Failures) {
            std::printf("    resolve fail '%s' code %d (%s)\n", f.LogicalName.c_str(), static_cast<int>(f.Result),
                        f.Detail.c_str());
        }
        return;
    }

    std::unique_ptr<IMachine> machineBase =
        DefaultMachineRegistry().Create(MachineId(def.Machine), VariantId(def.Variant), res.Resources);
    Check(machineBase != nullptr,
          "xml bring-up: DefaultMachineRegistry().Create() builds a Machine from the XML's own machine/variant/resources");
    if (!machineBase) return;
    machineBase->Initialize();

    auto* gyruss = dynamic_cast<GyrussMachine*>(machineBase.get());
    Check(gyruss != nullptr, "xml bring-up: the created Machine is actually a GyrussMachine");
    if (gyruss == nullptr) return;

    LogicalChannelSink sink;
    constexpr int kBlocksPerSecond = 750;  // 750*256/192000 = 1.000s exactly
    constexpr int kMeasureSeconds = 4;

    auto measureTrackByTitle = [&](const char* title) -> std::pair<CommandProbeResult, std::vector<TonePeriodFingerprint>> {
        const auto trackIt = std::find_if(def.Tracks.begin(), def.Tracks.end(),
                                           [&](const TrackDefinition& t) { return t.Title == title; });
        Check(trackIt != def.Tracks.end(), (std::string("bring-up: \"") + title + "\" track is declared").c_str());
        if (trackIt == def.Tracks.end()) {
            return {};
        }
        const std::uint8_t cmdByte = static_cast<std::uint8_t>(std::stoul(trackIt->Command, nullptr, 0));

        MusicCommand cmd;
        cmd.Kind = MusicCommandKind::Raw;
        cmd.Value = cmdByte;
        gyruss->SendCommand(cmd);

        CommandProbeResult overall;
        double perChannelPeak[16] = {};
        bool perChannelVarying[16] = {};
        std::vector<TonePeriodFingerprint> fingerprints;
        for (int second = 0; second < kMeasureSeconds; ++second) {
            sink.ClearAll();
            for (int i = 0; i < kBlocksPerSecond; ++i) machineBase->Render(EmuDuration{Mixer::kBlockSizeFrames}, &sink);
            for (std::uint32_t ch = 0; ch < 16; ++ch) {
                const double p = PeakAbs(sink, ch);
                const bool v = IsTimeVarying(sink, ch);
                perChannelPeak[ch] = std::max(perChannelPeak[ch], p);
                perChannelVarying[ch] = perChannelVarying[ch] || v;
                overall.peak = std::max(overall.peak, p);
                overall.varying = overall.varying || v;
            }
            fingerprints.push_back(SnapshotTonePeriods(*gyruss));
        }
        std::printf("  cmd 0x%02X \"%s\": peak=%.4f varying=%d\n", cmdByte, title, overall.peak, overall.varying ? 1 : 0);
        for (std::uint32_t ch = 0; ch < 16; ++ch) {
            std::printf("    ch%02u (%s): peak=%.4f varying=%d\n", ch, machineBase->LogicalChannels()[ch].ChannelName.c_str(),
                        perChannelPeak[ch], perChannelVarying[ch] ? 1 : 0);
        }
        for (int chip = 0; chip < 2; ++chip) {
            std::printf("    ay%d Port B values seen:", chip + 1);
            for (std::uint8_t v : gyruss->AyPortBValuesForTest(chip)) std::printf(" %02X", v);
            std::printf("  (filter switches now: %d %d %d)\n", gyruss->AyFilterSwitchesForTest(chip, 0),
                        gyruss->AyFilterSwitchesForTest(chip, 1), gyruss->AyFilterSwitchesForTest(chip, 2));
        }
        const auto z80Snap = gyruss->Z80Snapshot();
        std::printf("    DIAG after: z80 PC=%04X halted=%d irqServiceCount=%llu\n", z80Snap.pc, z80Snap.halted ? 1 : 0,
                    (unsigned long long)z80Snap.irqServiceCount);
        for (int chip = 0; chip < 5; ++chip) {
            std::printf("    ay%d regs: tone=%02X%02X %02X%02X %02X%02X enable=%02X volA=%02X\n", chip + 1,
                        gyruss->Ay8910ForTest(chip).RegisterForTest(1), gyruss->Ay8910ForTest(chip).RegisterForTest(0),
                        gyruss->Ay8910ForTest(chip).RegisterForTest(3), gyruss->Ay8910ForTest(chip).RegisterForTest(2),
                        gyruss->Ay8910ForTest(chip).RegisterForTest(5), gyruss->Ay8910ForTest(chip).RegisterForTest(4),
                        gyruss->Ay8910ForTest(chip).RegisterForTest(7), gyruss->Ay8910ForTest(chip).RegisterForTest(8));
        }
        return {overall, fingerprints};
    };

    // Settle first (long enough to clear ay3's Port A "heartbeat" gate before
    // any command is sent).
    for (int i = 0; i < 300; ++i) machineBase->Render(EmuDuration{Mixer::kBlockSizeFrames}, nullptr);

    const auto [bgm1Probe, bgm1Fingerprints] = measureTrackByTitle("Main BGM 1");
    const auto [bgm2Probe, bgm2Fingerprints] = measureTrackByTitle("Main BGM 2");

    Check(bgm1Probe.peak > 0.0 && bgm1Probe.varying, "bring-up: \"Main BGM 1\" produces nonzero, time-varying output");
    Check(bgm2Probe.peak > 0.0 && bgm2Probe.varying, "bring-up: \"Main BGM 2\" produces nonzero, time-varying output");

    // Command-specificity check: do the two tracks' tone-period register
    // trajectories actually differ, or is the driver replaying the same
    // fixed content regardless of which command was sent?
    bool anyFingerprintDiffers = false;
    if (bgm1Fingerprints.size() == bgm2Fingerprints.size()) {
        for (std::size_t i = 0; i < bgm1Fingerprints.size(); ++i) {
            if (!(bgm1Fingerprints[i] == bgm2Fingerprints[i])) {
                anyFingerprintDiffers = true;
                break;
            }
        }
    }
    // The driver writes ay1/ay2's Port B (the RC filter selection) while the songs play: more than the
    // start-up 0xFF has to show up, and the filter setting follows it.
    bool portBWritten = false;
    for (int chip = 0; chip < 2; ++chip) {
        for (std::uint8_t v : gyruss->AyPortBValuesForTest(chip)) portBWritten = portBWritten || v != 0xFF;
    }
    Check(portBWritten, "bring-up: ay1/ay2 Port B is written by the driver during playback (RC filter selection)");
    Check(anyFingerprintDiffers,
          "bring-up: \"Main BGM 1\" and \"Main BGM 2\" produce DIFFERENT AY8910 tone-period register content "
          "(command-specific dispatch, not a universal fixed side effect)");

    // Also confirm each track's own tone-period content changes across its
    // own 4-second window (an evolving melody, not a single held note).
    bool bgm1Evolves = false;
    for (std::size_t i = 1; i < bgm1Fingerprints.size(); ++i) {
        if (!(bgm1Fingerprints[i] == bgm1Fingerprints[0])) {
            bgm1Evolves = true;
            break;
        }
    }
    Check(bgm1Evolves, "bring-up: \"Main BGM 1\"'s own tone-period registers change over its own 4-second window "
                        "(an evolving melody, not one held note)");
}

void GyrussChannelMeterAbiDuringRealPlaybackReportsPerChannelPeak() {
    std::printf("GyrussChannelMeterAbiDuringRealPlaybackReportsPerChannelPeak\n");

    const char* romRootEnv = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRootEnv == nullptr || romRootEnv[0] == '\0') {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is not set - real-ABI-path bring-up not exercised\n");
        return;
    }
    const auto defPath = FindGyrussDefinitionFile();
    if (!defPath.has_value()) {
        std::printf("  SKIP: xml/gyruss.xml not found from the cwd\n");
        return;
    }
    const auto defText = ReadFileText(*defPath);
    if (!defText.has_value()) {
        std::printf("  SKIP: could not read gyruss.xml\n");
        return;
    }

    // Full public C ABI path (chipestra_parse_game_definition -> _get_info/_get_resource
    // -> chipestra_resolve_resources -> chipestra_load_machine -> chipestra_start_playback), the
    // SAME sequence NativeRuntimeSession/Player (Managed Core, driven by the
    // real GUI) uses - not GyrussViaDefinitionXmlRealRomBringUp's
    // native-shortcut GyrussMachine::Render() construction above, which never
    // exercises the ABI/Mixer/playback pipeline.
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
        std::snprintf(resources[i].archive, sizeof(resources[i].archive), "%s", gameInfo.archive);
    }

    ChipestraResourceResolutionHandle resolutionHandle = nullptr;
    Check(chipestra_resolve_resources(romRootEnv, resources.empty() ? nullptr : resources.data(),
                                 static_cast<uint32_t>(resources.size()), &resolutionHandle) == CHIPESTRA_OK,
          "abi path: chipestra_resolve_resources() == CHIPESTRA_OK");

    ChipestraResolutionSummary summary{};
    Check(chipestra_resource_resolution_get_summary(resolutionHandle, &summary) == CHIPESTRA_OK && summary.ok != 0,
          "abi path: resolution summary reports ok");
    if (summary.ok == 0) {
        chipestra_free_resource_resolution(resolutionHandle);
        chipestra_free_game_definition(gameHandle);
        return;
    }

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

    chipestra_free_resource_resolution(resolutionHandle);
    chipestra_free_game_definition(gameHandle);

    uint32_t channelCount = 0;
    Check(chipestra_get_machine_channel_count(handle, &channelCount) == CHIPESTRA_OK && channelCount == 16,
          "abi path: real GyrussMachine reports 16 channels (5x AY8910x3 + DAC)");

    // Use the null output (not WASAPI) so this runs headless in CI/normal
    // test runs - chipestra_start_playback's own render lambda (the same lambda a
    // WASAPI-backed run uses to fill its ring buffer) still executes either
    // way, so this still exercises the exact same Mixer/RoutingGain/
    // LogicalChannel pipeline the user's real WASAPI GUI session goes
    // through; only the final device write differs.
    Check(chipestra_select_null_output(handle) == CHIPESTRA_OK, "abi path: chipestra_select_null_output() == CHIPESTRA_OK");
    Check(chipestra_start_audio_output(handle) == CHIPESTRA_OK, "abi path: chipestra_start_audio_output() == CHIPESTRA_OK");

    // "Main BGM 1" == command 0x27 (xml/gyruss.xml), the same
    // track GyrussViaDefinitionXmlRealRomBringUp measured as nonzero and
    // time-varying via the native shortcut above. No explicit warm-up
    // render calls here beyond what chipestra_load_machine's own
    // GyrussMachine::Initialize() already performs internally (sending a
    // command with zero renders in between must still be serviced correctly).
    Check(chipestra_send_command(handle, /*kind=Raw*/ 2, 0x27, 0) == CHIPESTRA_OK, "abi path: chipestra_send_command(0x27) == CHIPESTRA_OK");
    Check(chipestra_start_playback(handle) == CHIPESTRA_OK, "abi path: chipestra_start_playback() == CHIPESTRA_OK");

    Check(WaitForSubmittedFramesAbi(handle, Mixer::kBlockSizeFrames * 750, 5000),
          "abi path: playback renders at least 750 blocks (~1s) within the timeout");

    // Poll every channel individually for a few seconds, tracking the max
    // Peak and whether it ever moves (mirrors the native-shortcut test's own
    // per-channel breakdown above, but reading it back through the real
    // public C ABI's ChipestraLogicalChannelInfo instead of the Mixer-internal
    // LogicalChannelSink).
    double perChannelMaxPeak[16] = {};
    float perChannelFirstPeak[16] = {};
    bool perChannelSeenFirst[16] = {};
    bool perChannelVarying[16] = {};
    bool anyNonZero = false;
    for (int attempt = 0; attempt < 60; ++attempt) {
        for (uint32_t idx = 0; idx < channelCount && idx < 16; ++idx) {
            ChipestraLogicalChannelInfo info{};
            const ChipestraResult infoResult = chipestra_get_logical_channel_info(handle, idx, &info);
            if (infoResult != CHIPESTRA_OK) continue;
            if (!perChannelSeenFirst[idx]) {
                perChannelFirstPeak[idx] = info.peak;
                perChannelSeenFirst[idx] = true;
            } else if (info.peak != perChannelFirstPeak[idx]) {
                perChannelVarying[idx] = true;
            }
            perChannelMaxPeak[idx] = std::max(perChannelMaxPeak[idx], static_cast<double>(info.peak));
            if (info.peak > 0.0f) anyNonZero = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::printf("  cmd 0x27 \"Main BGM 1\" via real C ABI (null output, same Mixer pipeline as WASAPI):\n");
    for (uint32_t idx = 0; idx < channelCount && idx < 16; ++idx) {
        ChipestraLogicalChannelInfo info{};
        const char* name = "?";
        if (chipestra_get_logical_channel_info(handle, idx, &info) == CHIPESTRA_OK) name = info.channel_name;
        std::printf("    ch%02u (%s): maxPeak=%.4f varyingOverPolls=%d muted=%d\n", idx, name, perChannelMaxPeak[idx],
                    perChannelVarying[idx] ? 1 : 0, info.muted ? 1 : 0);
    }
    Check(anyNonZero, "abi path: chipestra_get_logical_channel_info() reports a non-zero Peak for at least one channel "
                       "while Playing");

    // chipestra_get_master_state reads the actual final mixed
    // StereoFrame block (the same block a WASAPI-backed run would submit to
    // the device), independent of any per-channel LogicalChannelInfo
    // bookkeeping.
    ChipestraMasterState masterState{};
    Check(chipestra_get_master_state(handle, &masterState) == CHIPESTRA_OK, "abi path: chipestra_get_master_state() == CHIPESTRA_OK");
    std::printf("  master meter: peak_left=%.5f peak_right=%.5f rms_left=%.5f rms_right=%.5f\n", masterState.peak_left,
                masterState.peak_right, masterState.rms_left, masterState.rms_right);
    Check(masterState.peak_left > 0.0f || masterState.peak_right > 0.0f,
          "abi path: chipestra_get_master_state() reports nonzero final mixed output (the final mixed signal, "
          "not a per-channel bookkeeping value)");

    chipestra_stop_playback(handle);
    chipestra_stop_audio_output(handle);
    chipestra_unload_machine(handle);
    chipestra_destroy_instance(handle);
}
