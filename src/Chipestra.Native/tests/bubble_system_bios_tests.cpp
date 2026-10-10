// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Bubble System BIOS-only configuration tests. All ROM bytes below are
// hand-written synthetic programs (no real Bubble System ROM data).
//
// Synthetic sound Z80 ROM shared by the synthetic tests:
//   0x0000  LD SP,7F00h     ; 31 00 7F
//   0x0003  IM 1            ; ED 56
//   0x0005  EI              ; FB       - sets IFF1, ends Initialize()'s adaptive warm-up
//   0x0006  HALT            ; 76       - waits for an NMI / IRQ
//   0x0007  JR 0x0006       ; 18 FD    - back to HALT after a handler returns
//   0x0038  IM 1's IRQ vector - the command-latch handler:
//     PUSH AF ; LD A,(E001h) ; LD (4101h),A ; POP AF ; EI ; RETI
//   0x0066  NMI vector - counts every NMI in shared RAM:
//     PUSH AF ; LD A,(4100h) ; INC A ; LD (4100h),A ; POP AF ; RETN
//
// ROM handling for the real-ROM test: never hard-codes a ROM Root path and
// only prints derived metadata (page/PC counts, peak/sum levels), never
// ROM-derived sample data.

#include "bubble_system_bios_tests.h"

#include "default_machine_catalog.h"
#include "devices/konami/bubblesystem/bubble_system_machine.h"
#include "machine_registry.h"
#include "mixer.h"
#include "resource_resolver.h"
#include "soxr_sample_rate_converter.h"
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
#include <vector>

namespace {

constexpr std::uint16_t kNmiCounterAddress = 0x4100;
constexpr std::uint16_t kLatchCopyAddress = 0x4101;

std::vector<std::uint8_t> BuildSyntheticZ80Rom() {
    std::vector<std::uint8_t> rom(0x2000, 0x00);
    std::size_t i = 0;
    rom[i++] = 0x31;  // LD SP,7F00h
    rom[i++] = 0x00;
    rom[i++] = 0x7F;
    rom[i++] = 0xED;  // IM 1
    rom[i++] = 0x56;
    rom[i++] = 0xFB;  // EI
    rom[i++] = 0x76;  // HALT
    rom[i++] = 0x18;  // JR 0x0006
    rom[i++] = 0xFD;

    i = 0x0038;
    rom[i++] = 0xF5;  // PUSH AF
    rom[i++] = 0x3A;  // LD A,(E001h)
    rom[i++] = 0x01;
    rom[i++] = 0xE0;
    rom[i++] = 0x32;  // LD (4101h),A
    rom[i++] = static_cast<std::uint8_t>(kLatchCopyAddress & 0xFF);
    rom[i++] = static_cast<std::uint8_t>(kLatchCopyAddress >> 8);
    rom[i++] = 0xF1;  // POP AF
    rom[i++] = 0xFB;  // EI
    rom[i++] = 0xED;  // RETI
    rom[i++] = 0x4D;

    i = 0x0066;
    rom[i++] = 0xF5;  // PUSH AF
    rom[i++] = 0x3A;  // LD A,(4100h)
    rom[i++] = static_cast<std::uint8_t>(kNmiCounterAddress & 0xFF);
    rom[i++] = static_cast<std::uint8_t>(kNmiCounterAddress >> 8);
    rom[i++] = 0x3C;  // INC A
    rom[i++] = 0x32;  // LD (4100h),A
    rom[i++] = static_cast<std::uint8_t>(kNmiCounterAddress & 0xFF);
    rom[i++] = static_cast<std::uint8_t>(kNmiCounterAddress >> 8);
    rom[i++] = 0xF1;  // POP AF
    rom[i++] = 0xED;  // RETN
    rom[i++] = 0x45;
    return rom;
}

std::unique_ptr<BubbleSystemMachine> MakeSyntheticBiosMachine() {
    return std::make_unique<BubbleSystemMachine>(BuildSyntheticZ80Rom(), std::vector<std::uint8_t>(0x200, 0),
                                                  std::vector<std::uint8_t>{}, std::vector<std::uint8_t>{},
                                                  BubbleSystemBoardMode::BiosOnly);
}

void RenderTicks(BubbleSystemMachine& machine, std::uint64_t ticks) {
    constexpr std::uint64_t kBlock = Mixer::kBlockSizeFrames;
    for (std::uint64_t done = 0; done < ticks; done += kBlock) {
        machine.Render(EmuDuration{kBlock}, nullptr);
    }
}

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

std::optional<std::filesystem::path> FindDefinitionFile(const char* fileName) {
    const std::filesystem::path relative = std::filesystem::path("xml") / fileName;
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

}  // namespace

void BubbleSystemBiosVariantIsRegisteredAndNeedsOnlySoundResources() {
    std::printf("BubbleSystemBiosVariantIsRegisteredAndNeedsOnlySoundResources\n");
    const MachineDefinition* def = DefaultMachineRegistry().Find(MachineId("KONAMI_BUBBLESYSTEM_GRADIUS"));
    Check(def != nullptr, "catalog: KONAMI_BUBBLESYSTEM_GRADIUS is registered");
    if (def == nullptr) return;
    const bool hasBios = std::any_of(def->Variants.begin(), def->Variants.end(), [](const MachineVariant& v) {
        return v.Id == VariantId("BUBBLESYSTEM_BIOS_BASE");
    });
    const bool hasCartridge = std::any_of(def->Variants.begin(), def->Variants.end(), [](const MachineVariant& v) {
        return v.Id == VariantId("BUBBLESYSTEM_GRADIUS_BASE");
    });
    Check(hasBios, "catalog: BUBBLESYSTEM_BIOS_BASE variant exists (same Machine - one Library node)");
    Check(hasCartridge, "catalog: the existing BUBBLESYSTEM_GRADIUS_BASE variant is untouched");

    MachineResources soundOnly;
    soundOnly.Add(ResolvedResource("soundcpu", BuildSyntheticZ80Rom()));
    soundOnly.Add(ResolvedResource("wave0", std::vector<std::uint8_t>(0x100, 0)));
    soundOnly.Add(ResolvedResource("wave1", std::vector<std::uint8_t>(0x100, 0)));
    std::unique_ptr<IMachine> bios =
        DefaultMachineRegistry().Create(MachineId("KONAMI_BUBBLESYSTEM_GRADIUS"), VariantId("BUBBLESYSTEM_BIOS_BASE"), soundOnly);
    Check(bios != nullptr, "create: BIOS variant builds from the three sound resources alone (no bootcode/bubblememory)");

    bool cartridgeVariantRejected = false;
    try {
        std::unique_ptr<IMachine> full = DefaultMachineRegistry().Create(
            MachineId("KONAMI_BUBBLESYSTEM_GRADIUS"), VariantId("BUBBLESYSTEM_GRADIUS_BASE"), soundOnly);
        cartridgeVariantRejected = (full == nullptr);
    } catch (const std::exception&) {
        cartridgeVariantRejected = true;
    }
    Check(cartridgeVariantRejected,
          "create: the cartridge variant still refuses the same sound-only set (missing bootcode/bubblememory fails loudly)");
}

void BubbleSystemBiosModeNeverRunsMainCpuAndPulsesSoundNmiPerFrame() {
    std::printf("BubbleSystemBiosModeNeverRunsMainCpuAndPulsesSoundNmiPerFrame\n");
    auto machine = MakeSyntheticBiosMachine();
    std::uint64_t mainFetches = 0;
    machine->SetMainCpuOpcodeFetchObserverForTest([&](std::uint32_t, std::uint16_t) { ++mainFetches; });
    machine->Initialize();

    // Initialize() already rendered one kWarmupChunkFrames block; render the rest so the
    // total is exactly 100 video frames' worth of canonical ticks (+ under one more).
    constexpr std::uint64_t kFrames = 100;
    const std::uint64_t alreadyRendered = BubbleSystemMachine::kWarmupChunkFrames;
    const std::uint64_t wanted = kFrames * BubbleSystemMachine::kVblankNmiPeriodCanonicalTicks;
    RenderTicks(*machine, wanted - alreadyRendered + Mixer::kBlockSizeFrames);  // rounds up to whole blocks

    Check(machine->Z80Snapshot().iff1, "boot: the sound Z80 ran its boot code (IFF1 set) with no 68000 present");
    Check(mainFetches == 0, "main CPU: no opcode fetch ever happened (the 68000 is never run)");
    const auto mainSnap = machine->MainCpuForTest().Snapshot();
    Check(mainSnap.interruptServiceCount == 0 && mainSnap.illegalInstructionCount == 0,
          "main CPU: no interrupt serviced, no illegal instruction");
    const unsigned counted = machine->ReadMemory(kNmiCounterAddress);
    std::printf("  observed sound NMIs: %u (expected %llu)\n", counted, static_cast<unsigned long long>(kFrames));
    Check(counted == kFrames, "sound NMI: exactly one NMI per video frame (60.606Hz) reached the Z80 handler");
}

void BubbleSystemBiosModeIsDeterministic() {
    std::printf("BubbleSystemBiosModeIsDeterministic\n");
    auto run = []() {
        auto machine = MakeSyntheticBiosMachine();
        machine->Initialize();
        RenderTicks(*machine, 200'000);
        const auto snap = machine->Z80Snapshot();
        return std::vector<std::uint64_t>{machine->ReadMemory(kNmiCounterAddress), snap.pc, snap.nmiServiceCount,
                                          snap.irqServiceCount};
    };
    Check(run() == run(), "determinism: two identical BIOS-only runs end in the identical Z80/NMI state");
}

void BubbleSystemBiosModeDeliversPlayerCommandsToSoundZ80() {
    std::printf("BubbleSystemBiosModeDeliversPlayerCommandsToSoundZ80\n");
    auto machine = MakeSyntheticBiosMachine();
    machine->Initialize();
    MusicCommand command;
    command.Kind = MusicCommandKind::Raw;
    command.Value = 0x40;
    machine->SendCommand(command);
    RenderTicks(*machine, 50'000);
    Check(machine->ReadMemory(kLatchCopyAddress) == 0x40,
          "command: SendCommand(0x40) reaches the sound Z80 through the soundlatch IRQ path");
    Check(machine->Z80Snapshot().irqServiceCount >= 1, "command: the Z80 serviced the command IRQ");
}

void BubbleSystemBiosModeSendCommandRestartsBoardFromPowerOn() {
    std::printf("BubbleSystemBiosModeSendCommandRestartsBoardFromPowerOn\n");
    auto machine = MakeSyntheticBiosMachine();
    machine->Initialize();
    RenderTicks(*machine, 100 * BubbleSystemMachine::kVblankNmiPeriodCanonicalTicks);
    const unsigned before = machine->ReadMemory(kNmiCounterAddress);
    Check(before >= 99, "restart: after ~100 frames the synthetic driver has counted ~100 NMIs");
    MusicCommand command;
    command.Kind = MusicCommandKind::Raw;
    command.Value = 0x40;
    machine->SendCommand(command);
    RenderTicks(*machine, 3 * Mixer::kBlockSizeFrames);
    const unsigned after = machine->ReadMemory(kNmiCounterAddress);
    Check(after < 5, "restart: SendCommand() cold-restarted the board (shared RAM cleared, NMI pacing from zero)");
    Check(machine->ReadMemory(kLatchCopyAddress) == 0x40, "restart: the command was still delivered after the restart");
}

void BubbleSystemBiosModeStaleCommandDoesNotLeakIntoNextColdRestart() {
    std::printf("BubbleSystemBiosModeStaleCommandDoesNotLeakIntoNextColdRestart\n");
    // Player::SelectTrack on a control track (STOP) sends the command but never starts playback,
    // so no Render() runs before the NEXT selection. The first command's still-pending IRQ must
    // not survive into the next cold restart (it would be consumed as a spurious "STOP" while
    // the driver is still booting, silencing the countdown selected right after it).
    auto machine = MakeSyntheticBiosMachine();
    machine->Initialize();
    MusicCommand stop;
    stop.Kind = MusicCommandKind::Raw;
    stop.Value = 0x11;
    machine->SendCommand(stop);  // no Render() afterwards
    MusicCommand countdown;
    countdown.Kind = MusicCommandKind::Raw;
    countdown.Value = 0x40;
    machine->SendCommand(countdown);
    RenderTicks(*machine, 50'000);
    Check(machine->ReadMemory(kLatchCopyAddress) == 0x40, "stale command: the second command is the one the driver sees last");
    const auto snap = machine->Z80Snapshot();
    std::printf("  IRQ services observed: %llu (expected 1)\n", static_cast<unsigned long long>(snap.irqServiceCount));
    Check(snap.irqServiceCount == 1,
          "stale command: exactly one command IRQ reaches the driver after the restart (no leaked earlier command)");
}

void BubbleSystemPublishesOnlyTheBiosOnlyEntry() {
    std::printf("BubbleSystemPublishesOnlyTheBiosOnlyEntry\n");
    // The cartridge-carrying configuration (BUBBLESYSTEM_GRADIUS_BASE) stays in C++ but is not
    // published as a game entry (there is no xml/bs_gradius.xml): the Library's
    // "Bubble System" system must list only the BIOS-only entry.
    const auto probe = FindDefinitionFile("bubsys.xml");
    Check(probe.has_value(), "definitions: xml/bubsys.xml is present");
    if (!probe.has_value()) return;
    const std::filesystem::path dir = probe->parent_path();
    Check(!std::filesystem::exists(dir / "bs_gradius.xml"), "definitions: no bs_gradius.xml file is published");
    int bubbleEntries = 0;
    bool cartridgeVariantPublished = false;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() != ".xml") continue;
        const auto text = ReadFileText(entry.path());
        if (!text.has_value()) continue;
        const XmlParseResult parsed = ParseGameDefinition(*text);
        if (parsed.Status != XmlParseStatus::Ok) continue;
        if (parsed.Definition.Machine != "KONAMI_BUBBLESYSTEM_GRADIUS") continue;
        ++bubbleEntries;
        if (parsed.Definition.Variant == "BUBBLESYSTEM_GRADIUS_BASE") cartridgeVariantPublished = true;
    }
    Check(bubbleEntries == 1, "definitions: exactly one game entry targets the Bubble System machine (bubsys)");
    Check(!cartridgeVariantPublished, "definitions: no game entry publishes the cartridge variant BUBBLESYSTEM_GRADIUS_BASE");
    // The C++ side is deliberately kept intact.
    const MachineDefinition* def = DefaultMachineRegistry().Find(MachineId("KONAMI_BUBBLESYSTEM_GRADIUS"));
    bool cartridgeVariantStillRegistered = false;
    if (def != nullptr) {
        for (const MachineVariant& v : def->Variants) {
            if (v.Id == VariantId("BUBBLESYSTEM_GRADIUS_BASE")) cartridgeVariantStillRegistered = true;
        }
    }
    Check(cartridgeVariantStillRegistered, "catalog: the cartridge variant's C++ implementation is kept (only its XML entry is gone)");
}

void BubbleSystemBiosDefinitionXmlIsValid() {
    std::printf("BubbleSystemBiosDefinitionXmlIsValid\n");
    const auto path = FindDefinitionFile("bubsys.xml");
    Check(path.has_value(), "definition xml: xml/bubsys.xml is present (run from the repo root)");
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
    Check(def.Id == "bubsys", "definition xml: game id == bubsys");
    Check(def.Machine == "KONAMI_BUBBLESYSTEM_GRADIUS" && def.Variant == "BUBBLESYSTEM_BIOS_BASE",
          "definition xml: machine/variant select the BIOS-only configuration");
    Check(def.Archive == "bubsys", "definition xml: archive == bubsys (all three ROMs live in bubsys.zip)");
    Check(def.Resources.size() == 3, "definition xml: exactly three ROM resources (no cartridge image)");
    bool soundcpu = false, wave0 = false, wave1 = false, allHaveCrc = true, noCartridge = true;
    for (const RomResourceDefinition& rom : def.Resources) {
        soundcpu = soundcpu || rom.Name == "soundcpu";
        wave0 = wave0 || rom.Name == "wave0";
        wave1 = wave1 || rom.Name == "wave1";
        allHaveCrc = allHaveCrc && rom.Crc32.has_value();
        noCartridge = noCartridge && rom.Name != "bootcode" && rom.Name != "bubblememory";
    }
    Check(soundcpu && wave0 && wave1, "definition xml: soundcpu/wave0/wave1 resources present by logical name");
    Check(noCartridge, "definition xml: no bootcode/bubblememory resource is required");
    Check(allHaveCrc, "definition xml: every <rom> carries a crc32 (MAME-catalog metadata, not ROM content)");

    Check(def.Tracks.size() == 3, "definition xml: three tracks (STOP / Morning Music / Countdown)");
    bool stop = false, morning = false, countdown = false;
    for (const TrackDefinition& track : def.Tracks) {
        if (track.Command == "0x00" && track.IsControlCommand) stop = true;
        if (track.Command == "0x01" && !track.IsControlCommand) morning = true;
        if (track.Command == "0x40" && !track.IsControlCommand) countdown = true;
    }
    Check(stop && morning && countdown, "definition xml: 0x00 is a control STOP, 0x01 Morning Music, 0x40 Countdown");

    const MachineDefinition* machineDef = DefaultMachineRegistry().Find(MachineId(def.Machine));
    Check(machineDef != nullptr, "definition xml: the referenced machine is in the catalog");
    if (machineDef != nullptr) {
        const bool variantOk = std::any_of(machineDef->Variants.begin(), machineDef->Variants.end(),
                                            [&](const MachineVariant& v) { return v.Id == VariantId(def.Variant); });
        Check(variantOk, "definition xml: the referenced variant exists on that machine");
    }
}

namespace {

struct ChannelLevels {
    double k005289Sum = 0, vlmSum = 0, ay1Sum = 0;
    double k005289Swing = 0;
    int secondsVlmActive = 0;
};

// Renders `seconds` whole seconds, accumulating |sample| sums per chip family.
ChannelLevels RenderAndMeasure(BubbleSystemMachine& machine, LogicalChannelSink& sink, int seconds,
                               std::vector<float>* vlmRawOut = nullptr) {
    ChannelLevels out;
    const auto& channels = machine.LogicalChannels();
    const std::size_t block = Mixer::kBlockSizeFrames;
    const std::size_t blocksPerSecond = kNativeRuntimeTickRateHz / block;
    double kMin = 1e9, kMax = -1e9;
    for (int s = 0; s < seconds; ++s) {
        double vlmThisSecond = 0;
        for (std::size_t b = 0; b < blocksPerSecond; ++b) {
            sink.ClearAll();
            machine.Render(EmuDuration{block}, &sink);
            for (const LogicalChannel& channel : channels) {
                const LogicalChannelBuffer& buf = sink.ChannelBuffer(channel.Id);
                const bool isK = channel.DeviceName == "K005289";
                const bool isV = channel.DeviceName == "VLM5030";
                const bool isA = channel.DeviceName == "AY8910" && channel.ChannelName.rfind("AY1", 0) == 0;
                if (!isK && !isV && !isA) continue;
                for (std::size_t k = 0; k < buf.Size(); ++k) {
                    const double v = buf[k];
                    if (isK) {
                        out.k005289Sum += std::fabs(v);
                        kMin = std::min(kMin, v);
                        kMax = std::max(kMax, v);
                    } else if (isV) {
                        out.vlmSum += std::fabs(v);
                        if (vlmRawOut != nullptr) vlmRawOut->push_back(static_cast<float>(v));
                        vlmThisSecond += std::fabs(v);
                    } else {
                        out.ay1Sum += std::fabs(v);
                    }
                }
            }
        }
        if (vlmThisSecond > 1.0) ++out.secondsVlmActive;
    }
    out.k005289Swing = kMax - kMin;
    return out;
}

}  // namespace

void BubbleSystemBiosViaDefinitionXmlRealRomBringUp() {
    std::printf("BubbleSystemBiosViaDefinitionXmlRealRomBringUp\n");
    const char* romRootEnv = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRootEnv == nullptr || *romRootEnv == '\0') {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is not set - real-ROM bring-up not exercised (Build gate stays green "
                    "without the ROMs)\n");
        return;
    }
    const auto path = FindDefinitionFile("bubsys.xml");
    if (!path.has_value()) return;
    const auto text = ReadFileText(*path);
    if (!text.has_value()) return;
    const XmlParseResult parsed = ParseGameDefinition(*text);
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
    const ResourceResolutionResult res = ResourceResolver{std::filesystem::path(romRootEnv)}.Resolve(required);
    if (!res.Ok) {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is set but the Bubble System BIOS ROMs did not resolve under it "
                    "(archive=\"bubsys\", three files)\n");
        return;
    }
    Check(res.Resources.Count() == 3, "real ROM: exactly the three BIOS sound ROMs resolved from bubsys.zip");

    std::unique_ptr<IMachine> created =
        DefaultMachineRegistry().Create(MachineId(def.Machine), VariantId(def.Variant), res.Resources);
    Check(created != nullptr, "real ROM: DefaultMachineRegistry().Create() built the BIOS-only machine");
    auto* machine = dynamic_cast<BubbleSystemMachine*>(created.get());
    if (machine == nullptr) return;
    std::uint64_t mainFetches = 0;
    machine->SetMainCpuOpcodeFetchObserverForTest([&](std::uint32_t, std::uint16_t) { ++mainFetches; });
    machine->Initialize();
    Check(machine->Z80Snapshot().iff1, "real ROM: the BIOS sound driver booted (IFF1 set) with the 68000 held");

    LogicalChannelSink sink;
    auto send = [&](std::uint32_t value) {
        MusicCommand c;
        c.Kind = MusicCommandKind::Raw;
        c.Value = value;
        machine->SendCommand(c);
    };

    const ChannelLevels idle = RenderAndMeasure(*machine, sink, 3);
    std::printf("  idle (no command, 3s): K005289 sum=%.1f VLM5030 sum=%.1f AY1 sum=%.1f\n", idle.k005289Sum, idle.vlmSum,
                idle.ay1Sum);

    send(0x40);  // Countdown
    std::vector<float> countdownVlmRaw;
    const ChannelLevels countdown = RenderAndMeasure(*machine, sink, 20, &countdownVlmRaw);
    std::printf("  Countdown 0x40 (20s): VLM5030 sum=%.1f (active in %d of 20 seconds), K005289 sum=%.1f AY1 sum=%.1f\n",
                countdown.vlmSum, countdown.secondsVlmActive, countdown.k005289Sum, countdown.ay1Sum);
    Check(countdown.vlmSum > 100.0 && countdown.secondsVlmActive >= 3,
          "Countdown: the VLM5030 speaks (sustained, multi-second output) with the 68000 never run");

    {
        // No master clipping: run the captured native-rate VLM5030 stream through the same Stage-1 SRC the mixer
        // feed uses and apply the channel's own RoutingGain and the centre-pan 0.5 (the master sums to <= this).
        float routingGain = 0.0f;
        for (const LogicalChannel& channel : machine->LogicalChannels()) {
            if (channel.DeviceName == "VLM5030") routingGain = channel.RoutingGain;
        }
        const std::uint32_t nativeRate = 3'579'545 / 440;
        SoxrSampleRateConverter converter(nativeRate, 192000);
        const std::size_t outCount = static_cast<std::size_t>(countdownVlmRaw.size() * 192000ull / nativeRate);
        std::vector<float> converted(outCount, 0.0f);
        converter.Process(countdownVlmRaw.data(), countdownVlmRaw.size(), converted.data(), outCount);
        float maxAbs = 0.0f;
        for (float x : converted) maxAbs = std::max(maxAbs, std::fabs(x));
        const float masterPeak = maxAbs * routingGain * 0.5f;
        std::printf("  Countdown VLM5030: post-SRC peak %.3f x RoutingGain %.2f x pan 0.5 = master peak %.3f\n", maxAbs,
                    routingGain, masterPeak);
        Check(masterPeak <= 1.0f, "Countdown: the VLM5030 never reaches the master hard clamp (no clipping distortion)");
        // Waveform quality guard: correct speech peaks near MAME's ~0.26 at the chip output; an
        // unsigned wrap-around of the falling energy would pin it to the +-1.0 clamp. (countdownVlmRaw
        // is the chip's own output.)
        float rawMax = 0.0f;
        for (float x : countdownVlmRaw) rawMax = std::max(rawMax, std::fabs(x));
        std::printf("  Countdown VLM5030 chip output peak: %.3f (real MAME ~0.26)\n", rawMax);
        Check(rawMax > 0.05f && rawMax < 0.6f, "Countdown: the VLM5030 chip output is at MAME's level, not saturated at the +-1.0 clamp");
    }

    send(0x00);  // STOP
    RenderAndMeasure(*machine, sink, 2);
    send(0x01);  // Morning Music
    const ChannelLevels morning = RenderAndMeasure(*machine, sink, 20);
    std::printf("  Morning Music 0x01 (20s): K005289 sum=%.1f swing=%.3f AY1 sum=%.1f VLM5030 sum=%.1f\n",
                morning.k005289Sum, morning.k005289Swing, morning.ay1Sum, morning.vlmSum);
    Check(morning.k005289Swing > 0.1 && morning.k005289Sum > 1.0e6, "Morning Music: K005289 plays a changing melody");
    Check(morning.ay1Sum > 1.0e4, "Morning Music: AY1 carries notes too");
    Check(morning.vlmSum < countdown.vlmSum * 0.05,
          "VLM5030 is (near-)silent during Morning Music - no 68000 runaway feeding it (was un-isolatable in the "
          "68000-running experiment)");

    send(0x40);  // Countdown again, after Morning Music: every selection restarts the board
    const ChannelLevels countdownAgain = RenderAndMeasure(*machine, sink, 20);
    std::printf("  Countdown again after Morning Music (20s): VLM5030 sum=%.1f (active in %d of 20 seconds) - first run "
                "%.1f\n",
                countdownAgain.vlmSum, countdownAgain.secondsVlmActive, countdown.vlmSum);
    Check(std::fabs(countdownAgain.vlmSum - countdown.vlmSum) < countdown.vlmSum * 0.02,
          "Countdown: reproducible - a second selection (after Morning Music) speaks the same countdown");

    send(0x01);  // Morning Music again: identical to the first run
    const ChannelLevels morningAgain = RenderAndMeasure(*machine, sink, 20);
    Check(std::fabs(morningAgain.k005289Sum - morning.k005289Sum) < morning.k005289Sum * 0.001,
          "Morning Music: reproducible - replaying it after Countdown gives the same melody");

    // Player::SelectTrack(STOP) sends the command without ever rendering; the Countdown selected right
    // after it must still speak (a stale STOP IRQ must not leak into the next cold restart).
    send(0x00);
    send(0x40);
    const ChannelLevels countdownAfterStop = RenderAndMeasure(*machine, sink, 20);
    std::printf("  STOP then Countdown with no render in between (20s): VLM5030 sum=%.1f (active in %d of 20 seconds)\n",
                countdownAfterStop.vlmSum, countdownAfterStop.secondsVlmActive);
    Check(std::fabs(countdownAfterStop.vlmSum - countdown.vlmSum) < countdown.vlmSum * 0.02,
          "Countdown selected right after STOP (Player's control-track flow) speaks the same countdown");

    send(0x00);  // STOP
    RenderAndMeasure(*machine, sink, 1);
    const ChannelLevels stopped = RenderAndMeasure(*machine, sink, 4);
    std::printf("  after STOP (4s, skipping the first second): K005289 sum=%.1f AY1 sum=%.1f VLM5030 sum=%.1f\n",
                stopped.k005289Sum, stopped.ay1Sum, stopped.vlmSum);
    Check(stopped.k005289Sum < morning.k005289Sum * 0.01, "STOP: the melody stops");

    Check(mainFetches == 0, "real ROM: the main 68000 never fetched a single opcode during the whole session");
}
