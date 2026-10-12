// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// SfMachine native tests. The synthetic ROM programs below are hand-assembled Z80 code (no real ROM
// data). The real-ROM tests run only when CHIPESTRA_ROM_ROOT is set.

#include "sf_machine_tests.h"

#include "default_machine_catalog.h"
#include "devices/capcom/sf/sf_machine.h"
#include "devices/capcom/sf/sf_rom_layout.h"
#include "machine_registry.h"
#include "mixer.h"
#include "resampling_mixer_feed.h"
#include "resource_resolver.h"
#include "test_support.h"
#include "chipestra_abi.h"
#include "xml_definition_parser.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
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

void Emit(std::vector<std::uint8_t>& rom, std::size_t& at, std::initializer_list<std::uint8_t> bytes) {
    for (std::uint8_t b : bytes) rom[at++] = b;
}

// Music Z80: sets up YM2151 channel 0 (all four operators, full attack, key on), then spins. The NMI handler
// (0x0066) reads the command latch; any value other than 0xFF is stored in RAM at 0xC001 and also written to
// the YM2151's key-code register, so the command changes the sound.
std::vector<std::uint8_t> BuildMusicRom() {
    std::vector<std::uint8_t> rom(SfSoundMemoryMap::kMusicRomSize, 0x00);
    std::size_t at = 0;
    Emit(rom, at, {0x31, 0xFF, 0xC7});  // LD SP,0xC7FF
    const std::uint8_t regs[][2] = {{0x20, 0xC7}, {0x80, 0x1F}, {0x88, 0x1F}, {0x90, 0x1F}, {0x98, 0x1F}, {0x28, 0x4A}, {0x08, 0x78}};
    for (const auto& pair : regs) {
        Emit(rom, at, {0x3E, pair[0], 0x32, 0x00, 0xE0, 0x3E, pair[1], 0x32, 0x01, 0xE0});  // YM address, then data
    }
    Emit(rom, at, {0x18, 0xFE});  // JR $
    at = 0x66;
    Emit(rom, at, {0x3A, 0x00, 0xC8,                    // LD A,(0xC800)
                   0xFE, 0xFF,                          // CP 0xFF
                   0x28, 0x0D,                          // JR Z,+13 (to RETN)
                   0x32, 0x01, 0xC0,                    // LD (0xC001),A
                   0x47,                                // LD B,A
                   0x3E, 0x28, 0x32, 0x00, 0xE0,        // YM address = key code
                   0x78, 0x32, 0x01, 0xE0,              // YM data = the command
                   0xED, 0x45});                        // RETN
    return rom;
}

// Sample Z80: IM 1, interrupts on, wait. Every IRQ (0x0038) writes the next 4-bit value to both MSM5205s
// and goes back to the wait loop. There is no RAM, so the pushed return address is dropped (never used).
std::vector<std::uint8_t> BuildSampleImage() {
    std::vector<std::uint8_t> image(SfSoundMemoryMap::kSampleImageSize, 0x00);
    std::size_t at = 0;
    Emit(image, at, {0xED, 0x56, 0x3E, 0x00, 0xFB, 0x76, 0x18, 0xFD});  // IM 1; LD A,0; EI; HALT; JR -3
    at = 0x38;
    Emit(image, at, {0xD3, 0x00, 0xD3, 0x01, 0x3C, 0xE6, 0x0F, 0xFB, 0xC3, 0x05, 0x00});  // OUT,OUT,INC A,AND 0F,EI,JP 5
    return image;
}

// Sample Z80 program for the "no RAM" check: writes 0x55 to 0xC000, reads 0xC000 back and sends the result to
// the bank register. 0xC000 is ROM data (0x99) in bank 0, so the bank register ends at 0x99, not 0x55.
std::vector<std::uint8_t> BuildNoRamSampleImage() {
    std::vector<std::uint8_t> image(SfSoundMemoryMap::kSampleImageSize, 0x00);
    std::size_t at = 0;
    Emit(image, at, {0x3E, 0x55, 0x32, 0x00, 0xC0, 0x3A, 0x00, 0xC0, 0xD3, 0x02, 0x18, 0xFE});
    image[0xC000] = 0x99;
    return image;
}

// Sample Z80 program that writes one value to each MSM5205 and spins.
std::vector<std::uint8_t> BuildTwoWritesSampleImage() {
    std::vector<std::uint8_t> image(SfSoundMemoryMap::kSampleImageSize, 0x00);
    std::size_t at = 0;
    Emit(image, at, {0x3E, 0x07, 0xD3, 0x00, 0xD3, 0x01, 0x18, 0xFE});
    return image;
}

std::unique_ptr<SfMachine> MakeMachine(std::vector<std::uint8_t> music, std::vector<std::uint8_t> samples) {
    auto machine = std::make_unique<SfMachine>(std::move(music), std::move(samples));
    machine->Initialize();
    return machine;
}

void SendRaw(IMachine& machine, std::uint8_t value) {
    MusicCommand command;
    command.Kind = MusicCommandKind::Raw;
    command.Value = value;
    machine.SendCommand(command);
}

constexpr std::uint64_t kTicksPerSecond = kNativeRuntimeTickRateHz;

struct RunStats {
    std::uint64_t hash = 1469598103934665603ull;  // FNV-1a over every channel's samples, block by block
    double ymPeak = 0.0;
    double msmPeak = 0.0;
    std::uint64_t firstAudibleTick = ~0ull;
};

void Mix(std::uint64_t& hash, std::uint32_t bits) {
    for (int i = 0; i < 4; ++i) {
        hash ^= (bits >> (8 * i)) & 0xFF;
        hash *= 1099511628211ull;
    }
}

// Renders `ticks` through a sink in 256-tick blocks; `startTick` is only used to report firstAudibleTick.
RunStats RenderTicks(IMachine& machine, LogicalChannelSink& sink, std::uint64_t ticks, std::uint64_t startTick = 0) {
    RunStats stats;
    const std::vector<LogicalChannel>& channels = machine.LogicalChannels();
    for (std::uint64_t done = 0; done < ticks; done += Mixer::kBlockSizeFrames) {
        sink.ClearAll();
        machine.Render(EmuDuration{Mixer::kBlockSizeFrames}, &sink);
        bool audible = false;
        for (const LogicalChannel& channel : channels) {
            if (!sink.HasChannel(channel.Id)) continue;
            const LogicalChannelBuffer& buffer = sink.ChannelBuffer(channel.Id);
            for (std::size_t i = 0; i < buffer.Size(); ++i) {
                std::uint32_t bits;
                const float value = buffer[i];
                std::memcpy(&bits, &value, sizeof(bits));
                Mix(stats.hash, bits);
                const double magnitude = std::fabs(static_cast<double>(value));
                if (channel.Id < SfMachine::kMsm5205Base) {
                    stats.ymPeak = std::max(stats.ymPeak, magnitude);
                } else {
                    stats.msmPeak = std::max(stats.msmPeak, magnitude);
                }
                if (magnitude > 1e-4) audible = true;
            }
        }
        if (audible && stats.firstAudibleTick == ~0ull) stats.firstAudibleTick = startTick + done;
    }
    return stats;
}

std::optional<std::filesystem::path> FindSfDefinitionFile() {
    const std::filesystem::path relative = std::filesystem::path("xml") / "sf.xml";
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

bool SwitchedEqual(const RunStats& a, const RunStats& b) { return a.hash == b.hash; }

// Resolves sf.xml's resources under `romRoot` (the same lookup the application does).
bool ResolveRealRom(const std::filesystem::path& romRoot, GameDefinition& definition, ResourceResolutionResult& result) {
    const auto path = FindSfDefinitionFile();
    if (!path.has_value()) return false;
    const auto text = ReadFileText(*path);
    if (!text.has_value()) return false;
    const XmlParseResult parsed = ParseGameDefinition(*text);
    if (parsed.Status != XmlParseStatus::Ok) return false;
    definition = parsed.Definition;
    std::vector<RequiredResource> required;
    for (const RomResourceDefinition& rom : definition.Resources) {
        RequiredResource r;
        r.LogicalName = rom.Name;
        r.AcceptedNames = {rom.File};
        r.SizeBytes = rom.SizeBytes;
        r.Crc32 = rom.Crc32;
        r.Sha1 = rom.Sha1;
        if (!definition.Archive.empty()) r.Archive = definition.Archive;
        required.push_back(std::move(r));
    }
    const ResourceResolver resolver{romRoot};
    result = resolver.Resolve(required);
    return true;
}

std::unique_ptr<IMachine> BuildRealMachine() {
    const char* romRootEnv = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRootEnv == nullptr || romRootEnv[0] == '\0') return nullptr;
    GameDefinition definition;
    ResourceResolutionResult result;
    if (!ResolveRealRom(std::filesystem::path(romRootEnv), definition, result) || !result.Ok) return nullptr;
    std::unique_ptr<IMachine> machine =
        DefaultMachineRegistry().Create(MachineId(definition.Machine), VariantId(definition.Variant), result.Resources);
    if (machine) machine->Initialize();
    return machine;
}

}  // namespace

void SfMachineRegisteredInDefaultCatalog() {
    std::printf("SfMachineRegisteredInDefaultCatalog\n");
    const MachineDefinition* def = DefaultMachineRegistry().Find(MachineId("CAPCOM_68000"));
    Check(def != nullptr, "catalog: CAPCOM_68000 is registered in the default MachineRegistry");
    if (def == nullptr) return;
    Check(def->Category == MachineCategory::Arcade, "catalog: CAPCOM_68000 Category == Arcade");
    Check(def->Manufacturer == "Capcom", "catalog: CAPCOM_68000 Manufacturer == Capcom");
    Check(def->DisplayName == "68000 Based", "catalog: CAPCOM_68000 DisplayName == 68000 Based");
    Check(def->Variants.size() == 1 && def->Variants[0].Id == VariantId("SF_BASE"), "catalog: the only variant is SF_BASE");

    std::unique_ptr<IMachine> machine = DefaultMachineRegistry().Create(MachineId("CAPCOM_68000"), VariantId("SF_BASE"), MachineResources{});
    Check(machine != nullptr, "catalog: Create() builds a Machine even with no resources");
    if (!machine) return;
    machine->Initialize();
    Check(machine->LogicalChannels().size() == SfMachine::kChannelCount, "catalog: 10 logical channels (8 YM2151 + 2 MSM5205)");
}

void SfRomLayoutAssemblesTheSampleImage() {
    std::printf("SfRomLayoutAssemblesTheSampleImage\n");
    MachineResources resources;
    resources.Add(ResolvedResource("soundcpu", std::vector<std::uint8_t>(0x8000, 0x11)));
    resources.Add(ResolvedResource("samples0", std::vector<std::uint8_t>(0x20000, 0x22)));
    resources.Add(ResolvedResource("samples1", std::vector<std::uint8_t>(0x20000, 0x33)));
    const SfSoundRoms roms = AssembleSfSoundRoms(resources);
    Check(roms.AudioCpuProgram.size() == 0x8000 && roms.AudioCpuProgram[0] == 0x11, "layout: soundcpu is copied as is");
    Check(roms.SamplesProgram.size() == 0x40000, "layout: the sample image is 0x40000 bytes");
    Check(roms.SamplesProgram[0] == 0x22 && roms.SamplesProgram[0x1FFFF] == 0x22, "layout: samples0 fills 0x00000-0x1FFFF");
    Check(roms.SamplesProgram[0x20000] == 0x33 && roms.SamplesProgram[0x3FFFF] == 0x33, "layout: samples1 fills 0x20000-0x3FFFF");

    MachineResources onlyOne;
    onlyOne.Add(ResolvedResource("samples1", std::vector<std::uint8_t>(0x20000, 0x44)));
    const SfSoundRoms partial = AssembleSfSoundRoms(onlyOne);
    Check(partial.SamplesProgram.size() == 0x40000 && partial.SamplesProgram[0] == 0 && partial.SamplesProgram[0x20000] == 0x44,
          "layout: a missing half stays zero");
    Check(partial.AudioCpuProgram.empty(), "layout: a missing soundcpu stays empty");

    const SfSoundRoms none = AssembleSfSoundRoms(MachineResources{});
    Check(none.SamplesProgram.empty(), "layout: with no sample resource the image stays empty");
}

void SfSamplesCpuHasNoRam() {
    std::printf("SfSamplesCpuHasNoRam\n");
    auto machine = MakeMachine(BuildMusicRom(), BuildNoRamSampleImage());
    Check(machine->SampleBankForTest() == 0x99,
          "no RAM: a write to 0xC000 is dropped and the read-back returns the ROM byte (0x99), not the written 0x55");
}

void SfSamplesCpuBankIsSelectedByIoPort() {
    std::printf("SfSamplesCpuBankIsSelectedByIoPort\n");
    auto machine = MakeMachine(BuildMusicRom(), BuildNoRamSampleImage());
    // The program above set bank 0x99 (153): far past the 256KB image.
    Check(machine->ReadSamplesMemoryForTest(0x8000) == SfSoundMemoryMap::kPastImageByte,
          "bank: a bank past the sample image reads 0xFF");
    machine->Reset();
    Check(machine->SampleBankForTest() == 0x99, "bank: the program selects the same bank again after a Reset()");
}

void SfSamplesIrqRuns8000TimesPerSecondAsAHoldLine() {
    std::printf("SfSamplesIrqRuns8000TimesPerSecondAsAHoldLine\n");
    auto machine = MakeMachine(BuildMusicRom(), BuildSampleImage());
    LogicalChannelSink sink;
    const std::uint64_t irqBefore = machine->SamplesIrqCountForTest();
    const std::uint64_t servicedBefore = machine->SamplesCpuSnapshot().irqServiceCount;
    RenderTicks(*machine, sink, kTicksPerSecond);
    const std::uint64_t irqs = machine->SamplesIrqCountForTest() - irqBefore;
    const std::uint64_t serviced = machine->SamplesCpuSnapshot().irqServiceCount - servicedBefore;
    std::printf("  IRQs raised in 1s: %llu, serviced: %llu\n", static_cast<unsigned long long>(irqs), static_cast<unsigned long long>(serviced));
    Check(irqs >= 7999 && irqs <= 8001, "irq: 8000 IRQs are raised per second");
    Check(serviced >= 7999 && serviced <= 8001,
          "irq: the CPU serviced each one once - the line clears itself on acceptance (HOLD_LINE), it does not stay asserted");
    Check(machine->Msm5205ForTest(0).SignalForTest() != 0 || machine->Msm5205ForTest(0).StepForTest() != 0,
          "irq: the IRQ handler's OUT writes reached MSM5205 #1");
    Check(machine->Msm5205ForTest(1).RenderedClocks() == machine->Msm5205ForTest(0).RenderedClocks(),
          "irq: both MSM5205s are advanced to the same time");
}

void SfMsmWritesFromTheSamplesCpuReachBothChips() {
    std::printf("SfMsmWritesFromTheSamplesCpuReachBothChips\n");
    auto machine = MakeMachine(BuildMusicRom(), BuildTwoWritesSampleImage());
    Check(machine->MsmWriteCountForTest() == 2, "msm: OUT 0 and OUT 1 are one write each");
    Check(machine->Msm5205ForTest(0).SignalForTest() > 0, "msm: port 0 decodes into MSM5205 #1");
    Check(machine->Msm5205ForTest(1).SignalForTest() > 0, "msm: port 1 decodes into MSM5205 #2");
}

void SfCommandLatchAndNmiAt60Hz() {
    std::printf("SfCommandLatchAndNmiAt60Hz\n");
    auto machine = MakeMachine(BuildMusicRom(), BuildSampleImage());
    LogicalChannelSink sink;

    const std::uint64_t nmiBefore = machine->MusicCpuSnapshot().nmiServiceCount;
    RenderTicks(*machine, sink, kTicksPerSecond);
    const std::uint64_t nmis = machine->MusicCpuSnapshot().nmiServiceCount - nmiBefore;
    std::printf("  NMIs in 1s: %llu\n", static_cast<unsigned long long>(nmis));
    Check(nmis >= 59 && nmis <= 61, "nmi: one NMI per frame, 60 per second");
    Check(machine->LatchValueForTest() == 0xFF, "latch: idle frames write 0xFF");
    Check(machine->ReadMusicMemoryForTest(0xC001) == 0x00, "latch: the NMI handler saw no command yet");

    SendRaw(*machine, 0x2B);
    int framesWithCommand = 0;
    int framesTotal = 0;
    for (int i = 0; i < 12; ++i) {
        RenderTicks(*machine, sink, SfMachine::kFramePeriodTicks);
        ++framesTotal;
        if (machine->LatchValueForTest() == 0x2B) ++framesWithCommand;
    }
    Check(framesWithCommand == 1, "latch: the command sits in exactly one frame's latch write");
    Check(machine->LatchValueForTest() == 0xFF, "latch: the frames after the command are idle (0xFF) again");
    Check(machine->ReadMusicMemoryForTest(0xC001) == 0x2B, "latch: the music Z80's NMI handler read the command from 0xC800");
    Check(framesTotal == 12, "(sanity) rendered 12 frames");
}

void SfSwitchingSongsMatchesAFreshMachine() {
    std::printf("SfSwitchingSongsMatchesAFreshMachine\n");
    auto fresh = MakeMachine(BuildMusicRom(), BuildSampleImage());
    LogicalChannelSink freshSink;
    SendRaw(*fresh, 0x30);
    const RunStats freshStats = RenderTicks(*fresh, freshSink, kTicksPerSecond * 2);

    auto other = MakeMachine(BuildMusicRom(), BuildSampleImage());
    LogicalChannelSink otherSink;
    SendRaw(*other, 0x2A);
    const RunStats otherStats = RenderTicks(*other, otherSink, kTicksPerSecond * 2);

    auto switched = MakeMachine(BuildMusicRom(), BuildSampleImage());
    LogicalChannelSink switchedSink;
    SendRaw(*switched, 0x2A);
    RenderTicks(*switched, switchedSink, kTicksPerSecond * 3 / 2);
    const std::uint32_t resetsBefore = switched->ResetCountForTest();
    SendRaw(*switched, 0x30);
    Check(switched->ResetCountForTest() == resetsBefore + 1, "switch: a command after an earlier one resets the Machine");
    const RunStats switchedStats = RenderTicks(*switched, switchedSink, kTicksPerSecond * 2);

    std::printf("  YM2151 peak=%.4f MSM peak=%.4f\n", freshStats.ymPeak, freshStats.msmPeak);
    Check(freshStats.ymPeak > 0.001 && freshStats.msmPeak > 0.001, "(precondition) the synthetic programs sound on both chips");
    Check(freshStats.hash != otherStats.hash, "(precondition) the command changes the sound");
    Check(switchedStats.hash == freshStats.hash, "switch: the output after the switch is bit-identical to a fresh Machine playing the same command");

    auto first = MakeMachine(BuildMusicRom(), BuildSampleImage());
    const std::uint32_t before = first->ResetCountForTest();
    SendRaw(*first, 0x30);
    Check(first->ResetCountForTest() == before, "switch: the first command after Initialize() does not reset");
}

void SfMusicStopIsAResetAndNothingIsSent() {
    std::printf("SfMusicStopIsAResetAndNothingIsSent\n");
    auto fresh = MakeMachine(BuildMusicRom(), BuildSampleImage());
    LogicalChannelSink freshSink;
    const RunStats freshStats = RenderTicks(*fresh, freshSink, kTicksPerSecond);

    auto stopped = MakeMachine(BuildMusicRom(), BuildSampleImage());
    LogicalChannelSink stoppedSink;
    SendRaw(*stopped, 0x2A);
    RenderTicks(*stopped, stoppedSink, kTicksPerSecond);
    const std::uint32_t resetsBefore = stopped->ResetCountForTest();
    SendRaw(*stopped, 0x00);
    Check(stopped->ResetCountForTest() == resetsBefore + 1, "stop: command 0x00 resets the Machine");
    const RunStats stoppedStats = RenderTicks(*stopped, stoppedSink, kTicksPerSecond);
    Check(stoppedStats.hash == freshStats.hash, "stop: after the stop, output matches a fresh Machine with no command (no command was sent)");

    // A command after the stop is the first command since the reset: no second reset.
    const std::uint32_t resetsAfterStop = stopped->ResetCountForTest();
    SendRaw(*stopped, 0x30);
    Check(stopped->ResetCountForTest() == resetsAfterStop, "stop: the next command needs no further reset");
}

void SfResetKeepsChannelMuteGainPan() {
    std::printf("SfResetKeepsChannelMuteGainPan\n");
    auto machine = MakeMachine(BuildMusicRom(), BuildSampleImage());
    std::vector<LogicalChannel>& channels = machine->LogicalChannels();
    channels[3].Mute = true;
    channels[3].Gain = 0.25f;
    channels[3].Pan = 0.5f;
    channels[3].PanOverridden = true;
    SendRaw(*machine, 0x2A);
    SendRaw(*machine, 0x2B);
    Check(channels[3].Mute && channels[3].Gain == 0.25f && channels[3].Pan == 0.5f && channels[3].PanOverridden,
          "reset: Mute/Gain/Pan set by the user survive a Reset()");
}

void SfAcceptsRawButNotWsgCommands() {
    std::printf("SfAcceptsRawButNotWsgCommands\n");
    auto machine = MakeMachine(BuildMusicRom(), BuildSampleImage());
    MusicCommand raw;
    raw.Kind = MusicCommandKind::Raw;
    raw.Value = 0x28;
    MusicCommand wsg;
    wsg.Kind = MusicCommandKind::Wsg;
    wsg.Value = 0x01;
    Check(machine->AcceptsCommand(raw), "commands: Raw is accepted");
    Check(!machine->AcceptsCommand(wsg), "commands: Wsg is not accepted (this board has no WSG)");
}

void SfStereoRoutingFollowsTheYm2151OutputEnables() {
    std::printf("SfStereoRoutingFollowsTheYm2151OutputEnables\n");
    auto machine = MakeMachine(BuildMusicRom(), BuildSampleImage());
    LogicalChannelSink sink;
    RenderTicks(*machine, sink, kTicksPerSecond / 4);
    const std::vector<LogicalChannel>& channels = machine->LogicalChannels();
    // The program sets channel 0's connection register to 0xC7: left and right on.
    Check(channels[0].Pan == 0.0f && std::fabs(channels[0].RoutingGain - 2.0f * SfMachine::kYm2151RoutingGain) < 1e-6f,
          "stereo: a channel enabled on both sides is centred at the chip's route gain on each side (0.60 each)");
    Check(channels[1].RoutingGain == 0.0f, "stereo: a channel the driver never enabled contributes nothing");
    Check(std::fabs(channels[SfMachine::kMsm5205Base].RoutingGain - 2.0f) < 1e-6f && channels[SfMachine::kMsm5205Base].Pan == 0.0f,
          "stereo: each MSM5205 goes to both speakers at 1.0");
}

// ---------------------------------------------------------------------------------------------------
// Real ROM (sf.zip). SKIPped without CHIPESTRA_ROM_ROOT.

void SfViaDefinitionXmlRealRomBringUp() {
    std::printf("SfViaDefinitionXmlRealRomBringUp\n");
    const char* romRootEnv = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRootEnv == nullptr || romRootEnv[0] == '\0') {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is not set - real-ROM bring-up not exercised\n");
        return;
    }
    GameDefinition definition;
    ResourceResolutionResult result;
    const bool parsed = ResolveRealRom(std::filesystem::path(romRootEnv), definition, result);
    Check(parsed, "xml bring-up: xml/sf.xml is present and parses (run from the repo root)");
    if (!parsed) return;
    Check(result.Ok, "xml bring-up: every resource sf.xml declares resolves under CHIPESTRA_ROM_ROOT (archive=\"sf\")");
    if (!result.Ok) {
        for (const auto& f : result.Failures) {
            std::printf("    resolve fail '%s' code %d (%s)\n", f.LogicalName.c_str(), static_cast<int>(f.Result), f.Detail.c_str());
        }
        return;
    }
    Check(definition.Machine == "CAPCOM_68000" && definition.Variant == "SF_BASE", "xml bring-up: sf.xml names CAPCOM_68000 / SF_BASE");

    // The ROM search finds the sf.zip in the Capcom folder: resolving from that folder alone gives the same bytes,
    // and from a folder without it nothing is found.
    const std::filesystem::path capcomDir = std::filesystem::path(romRootEnv) / "Arcade" / "Capcom" / "68000Based";
    std::error_code ec;
    if (std::filesystem::exists(capcomDir / "sf.zip", ec)) {
        GameDefinition def2;
        ResourceResolutionResult fromCapcom;
        ResolveRealRom(capcomDir, def2, fromCapcom);
        Check(fromCapcom.Ok, "ROM search: sf.zip is found in Arcade/Capcom/68000Based");
        bool same = fromCapcom.Ok;
        for (const char* name : {"soundcpu", "samples0", "samples1"}) {
            const ResolvedResource* a = result.Resources.Find(name);
            const ResolvedResource* b = fromCapcom.Resources.Find(name);
            same = same && a != nullptr && b != nullptr && a->Size() == b->Size() && std::memcmp(a->Data(), b->Data(), a->Size()) == 0;
        }
        Check(same, "ROM search: the bytes found from the ROM root are the bytes of that sf.zip");
        GameDefinition def3;
        ResourceResolutionResult fromElsewhere;
        ResolveRealRom(std::filesystem::path(romRootEnv) / "Arcade" / "Sega", def3, fromElsewhere);
        Check(!fromElsewhere.Ok, "ROM search: a folder without sf.zip finds nothing");
    } else {
        std::printf("  SKIP: %s has no sf.zip - location checks not exercised\n", capcomDir.string().c_str());
    }

    std::unique_ptr<IMachine> machine =
        DefaultMachineRegistry().Create(MachineId(definition.Machine), VariantId(definition.Variant), result.Resources);
    Check(machine != nullptr, "xml bring-up: the Machine is built from the XML's own machine/variant/resources");
    if (!machine) return;
    machine->Initialize();

    // The first command right after Initialize() sounds (no 68000 start-up writes are needed).
    LogicalChannelSink sink;
    SendRaw(*machine, 0x28);
    const RunStats bgm = RenderTicks(*machine, sink, kTicksPerSecond * 6);
    std::printf("  0x28 (RETSU): YM2151 peak=%.4f, MSM peak=%.4f, first audible after %.3f s\n", bgm.ymPeak, bgm.msmPeak,
                static_cast<double>(bgm.firstAudibleTick) / static_cast<double>(kTicksPerSecond));
    Check(bgm.ymPeak > 0.01, "xml bring-up: BGM 0x28 sounds on the YM2151");
    Check(bgm.msmPeak < 1e-6, "xml bring-up: BGM 0x28 does not touch the MSM5205s");

    SendRaw(*machine, 0xCA);
    const RunStats voice = RenderTicks(*machine, sink, kTicksPerSecond * 3);
    std::printf("  0xCA: YM2151 peak=%.4f, MSM peak=%.4f\n", voice.ymPeak, voice.msmPeak);
    Check(voice.msmPeak > 0.01, "xml bring-up: a sample command (0xCA) sounds on the MSM5205s");
    Check(voice.ymPeak < 1e-6, "xml bring-up: ... and nothing else is left over from the earlier BGM");
}

void SfRealRomSwitchAndStopMatchAFreshMachine() {
    std::printf("SfRealRomSwitchAndStopMatchAFreshMachine\n");
    if (BuildRealMachine() == nullptr) {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is not set (or sf.zip not found) - real-ROM switch checks not exercised\n");
        return;
    }
    constexpr std::uint64_t kSeconds = 6;
    auto freshRun = [&](std::uint8_t command) {
        auto m = BuildRealMachine();
        LogicalChannelSink sink;
        SendRaw(*m, command);
        return RenderTicks(*m, sink, kTicksPerSecond * kSeconds);
    };
    auto switchedRun = [&](std::uint8_t first, std::uint64_t firstSeconds, std::uint8_t second) {
        auto m = BuildRealMachine();
        LogicalChannelSink sink;
        SendRaw(*m, first);
        RenderTicks(*m, sink, kTicksPerSecond * firstSeconds);
        SendRaw(*m, second);
        return RenderTicks(*m, sink, kTicksPerSecond * kSeconds);
    };

    const RunStats fresh30 = freshRun(0x30);
    Check(fresh30.ymPeak > 0.01, "(precondition) 0x30 sounds");
    Check(SwitchedEqual(fresh30, switchedRun(0x28, 4, 0x30)), "BGM -> BGM: the second song is bit-identical to playing it alone");

    // A sample (MSM5205) then a BGM: no sample left over.
    Check(SwitchedEqual(fresh30, switchedRun(0xCA, 1, 0x30)), "sample -> BGM: bit-identical to the BGM alone (no sample left over)");

    // The same command again (Stop -> Play).
    Check(SwitchedEqual(fresh30, switchedRun(0x30, 4, 0x30)), "same command twice (Stop -> Play): bit-identical to the first play");

    // Music Stop after a BGM and a sample, with nothing rendered afterwards: everything is silent.
    {
        auto m = BuildRealMachine();
        LogicalChannelSink sink;
        SendRaw(*m, 0x28);
        RenderTicks(*m, sink, kTicksPerSecond * 3);
        SendRaw(*m, 0x00);
        const RunStats silent = RenderTicks(*m, sink, kTicksPerSecond);
        std::printf("  after Music Stop: YM2151 peak=%.6f MSM peak=%.6f\n", silent.ymPeak, silent.msmPeak);
        Check(silent.ymPeak < 0.005 && silent.msmPeak < 1e-6, "Music Stop: the YM2151 and the MSM5205s are silent");
    }

    // The silence between a command and the first sound after a reset, for the report.
    for (std::uint8_t command : {std::uint8_t{0x28}, std::uint8_t{0x2E}, std::uint8_t{0x0F}, std::uint8_t{0x34}}) {
        auto m = BuildRealMachine();
        LogicalChannelSink sink;
        SendRaw(*m, 0x2A);
        RenderTicks(*m, sink, kTicksPerSecond * 2);
        SendRaw(*m, command);  // reset + command
        const RunStats after = RenderTicks(*m, sink, kTicksPerSecond * 4);
        const RunStats alone = freshRun(command);
        std::printf("  0x%02X: first sound %.3f s after the command (after a reset), %.3f s (fresh machine)\n", command,
                    after.firstAudibleTick == ~0ull ? -1.0 : static_cast<double>(after.firstAudibleTick) / static_cast<double>(kTicksPerSecond),
                    alone.firstAudibleTick == ~0ull ? -1.0 : static_cast<double>(alone.firstAudibleTick) / static_cast<double>(kTicksPerSecond));
    }
}

void SfResamplingFeedCompensatesYm2151AndMsm5205GroupDelay() {
    std::printf("SfResamplingFeedCompensatesYm2151AndMsm5205GroupDelay\n");
    // The two chip families of this board have very different resampler delays (YM2151: a small downsample,
    // MSM5205 at 384kHz: a 2x upsample). A step on both at the same native-time instant must reach the
    // canonical stream within a few milliseconds of each other.
    std::vector<LogicalChannel> channels(2);
    channels[0].Id = 0;
    channels[0].NativeSampleRate = static_cast<std::uint32_t>(SfMachine::kYm2151ClockRate.hz / Ym2151DeviceAdapter::kInputClocksPerSample);
    channels[1].Id = 1;
    channels[1].NativeSampleRate = Msm5205DeviceAdapter::kSlaveOutputSampleRateHz;
    const std::size_t canonicalFrames = Mixer::kBlockSizeFrames;

    struct FeedPacer {
        std::uint32_t rate;
        std::uint64_t totalFedSoFar = 0;
        std::size_t NextBlockCount(std::size_t blockIndexFromOne) {
            const std::uint64_t targetTotal = static_cast<std::uint64_t>(std::llround(
                static_cast<double>(blockIndexFromOne) * static_cast<double>(Mixer::kBlockSizeFrames) *
                static_cast<double>(rate) / 192000.0));
            const std::size_t count = static_cast<std::size_t>(targetTotal - totalFedSoFar);
            totalFedSoFar = targetTotal;
            return count;
        }
    };
    FeedPacer pacer0{channels[0].NativeSampleRate};
    FeedPacer pacer1{channels[1].NativeSampleRate};

    ResamplingMixerFeed feed;
    LogicalChannelSink native;
    native.RegisterChannel(0);
    native.RegisterChannel(1);

    std::size_t blockIndex = 0;
    for (int i = 0; i < 2250; ++i) {
        ++blockIndex;
        native.ClearAll();
        std::vector<float> silence0(pacer0.NextBlockCount(blockIndex), 0.0f);
        std::vector<float> silence1(pacer1.NextBlockCount(blockIndex), 0.0f);
        native.WriteSamples(0, silence0.data(), silence0.size());
        native.WriteSamples(1, silence1.data(), silence1.size());
        feed.Resample(native, channels, canonicalFrames);
    }
    std::vector<float> collected0;
    std::vector<float> collected1;
    for (int i = 0; i < 150; ++i) {
        ++blockIndex;
        native.ClearAll();
        std::vector<float> tone0(pacer0.NextBlockCount(blockIndex), 1.0f);
        std::vector<float> tone1(pacer1.NextBlockCount(blockIndex), 1.0f);
        native.WriteSamples(0, tone0.data(), tone0.size());
        native.WriteSamples(1, tone1.data(), tone1.size());
        const LogicalChannelSink& canonical = feed.Resample(native, channels, canonicalFrames);
        const LogicalChannelBuffer& buf0 = canonical.ChannelBuffer(0);
        const LogicalChannelBuffer& buf1 = canonical.ChannelBuffer(1);
        collected0.insert(collected0.end(), buf0.Data(), buf0.Data() + buf0.Size());
        collected1.insert(collected1.end(), buf1.Data(), buf1.Data() + buf1.Size());
    }
    auto findOnset = [](const std::vector<float>& v) -> std::size_t {
        for (std::size_t i = 0; i < v.size(); ++i) {
            if (v[i] > 0.5f) return i;
        }
        return v.size();
    };
    const std::size_t onset0 = findOnset(collected0);
    const std::size_t onset1 = findOnset(collected1);
    Check(onset0 < collected0.size() && onset1 < collected1.size(), "group delay: both channels cross the step threshold");
    const std::size_t difference = onset0 > onset1 ? onset0 - onset1 : onset1 - onset0;
    std::printf("  onset difference after compensation: %zu samples = %.3f ms (YM2151 onset %zu, MSM5205 onset %zu)\n", difference,
                static_cast<double>(difference) / 192.0, onset0, onset1);
    Check(difference < 2048, "group delay: the YM2151 and MSM5205 onsets land within ~11ms of each other after compensation");
}
