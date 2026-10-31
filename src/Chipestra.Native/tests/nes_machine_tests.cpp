// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// NesMachine native tests. The synthetic program below is hand-written; what each routine must leave
// behind is worked out here from the instructions, not read back from the Machine.

#include "nes_machine_tests.h"

#include "default_machine_catalog.h"
#include "definition_xml_locator.h"
#include "devices/nintendo/nes/nes_cartridge.h"
#include "devices/nintendo/nes/nes_driver_config.h"
#include "devices/nintendo/nes/nes_machine.h"
#include "machine_registry.h"
#include "resource_resolver.h"
#include "test_support.h"
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
#include <stdexcept>
#include <string>
#include <vector>

namespace {

// Routine addresses in the synthetic program ROM.
constexpr std::uint16_t kInit1 = 0x8000;   // LDA #$11; STA $0300; RTS
constexpr std::uint16_t kInit2 = 0x8010;   // LDA $0300; CLC; ADC #1; STA $0301; RTS   (needs init1 to have run)
constexpr std::uint16_t kInit3 = 0x8020;   // LDA #7; STA $4015; RTS                  (enable pulse 1, pulse 2, triangle)
constexpr std::uint16_t kFrame1 = 0x8100;  // INC $0310; RTS                          (counts frames)
constexpr std::uint16_t kFrame2 = 0x8110;  // LDA $0310; STA $0311; RTS               (sees the count of the same frame)
constexpr std::uint16_t kFrame3 = 0x8120;  // song: if $95 != 0, pulse 1 plays period $20 + $95
constexpr std::uint16_t kFrame4 = 0x8140;  // effect: if $8C != 0, the triangle plays period $40 + $8C
constexpr std::uint16_t kFrame5 = 0x8160;  // touches the picture chip and the controllers once each way
constexpr std::uint16_t kFrame6 = 0x8180;  // song: if $95 != 0, pulse 2 plays period $40 + $95

void Put(std::vector<std::uint8_t>& prg, std::uint16_t address, std::initializer_list<std::uint8_t> bytes) {
    std::size_t i = address & 0x7FFF;
    for (std::uint8_t b : bytes) prg[i++] = b;
}

NesCartridge BuildSyntheticCartridge() {
    NesCartridge cart;
    cart.prg.assign(32768, 0xEA);  // NOP
    Put(cart.prg, kInit1, {0xA9, 0x11, 0x8D, 0x00, 0x03, 0x60});
    Put(cart.prg, kInit2, {0xAD, 0x00, 0x03, 0x18, 0x69, 0x01, 0x8D, 0x01, 0x03, 0x60});
    Put(cart.prg, kInit3, {0xA9, 0x07, 0x8D, 0x15, 0x40, 0x60});
    Put(cart.prg, kFrame1, {0xEE, 0x10, 0x03, 0x60});
    Put(cart.prg, kFrame2, {0xAD, 0x10, 0x03, 0x8D, 0x11, 0x03, 0x60});
    // LDA $95; BEQ +16; CLC; ADC #$20; STA $4002; LDA #$BF; STA $4000; LDA #$08; STA $4003; RTS
    Put(cart.prg, kFrame3, {0xA5, 0x95, 0xF0, 0x10, 0x18, 0x69, 0x20, 0x8D, 0x02, 0x40, 0xA9, 0xBF, 0x8D, 0x00, 0x40,
                            0xA9, 0x08, 0x8D, 0x03, 0x40, 0x60});
    // LDA $8C; BEQ +16; CLC; ADC #$40; STA $400A; LDA #$FF; STA $4008; LDA #$08; STA $400B; RTS
    Put(cart.prg, kFrame4, {0xA5, 0x8C, 0xF0, 0x10, 0x18, 0x69, 0x40, 0x8D, 0x0A, 0x40, 0xA9, 0xFF, 0x8D, 0x08, 0x40,
                            0xA9, 0x08, 0x8D, 0x0B, 0x40, 0x60});
    // LDA $95; BEQ +16; CLC; ADC #$40; STA $4006; LDA #$BF; STA $4004; LDA #$08; STA $4007; RTS
    Put(cart.prg, kFrame6, {0xA5, 0x95, 0xF0, 0x10, 0x18, 0x69, 0x40, 0x8D, 0x06, 0x40, 0xA9, 0xBF, 0x8D, 0x04, 0x40,
                            0xA9, 0x08, 0x8D, 0x07, 0x40, 0x60});
    // LDA $2002; STA $2000; LDA $4016; STA $4016; RTS
    Put(cart.prg, kFrame5, {0xAD, 0x02, 0x20, 0x8D, 0x00, 0x20, 0xAD, 0x16, 0x40, 0x8D, 0x16, 0x40, 0x60});
    return cart;
}

NesDriverConfig BuildSyntheticDriver() {
    NesDriverConfig cfg;
    cfg.ramClear = std::pair<std::uint16_t, std::uint16_t>{0x0080, 0x03FF};
    cfg.init = {kInit1, kInit2, kInit3};
    cfg.frame = {kFrame1, kFrame2, kFrame3, kFrame4};
    cfg.bgmVariable = 0x95;
    cfg.sfxVariable = 0x8C;
    return cfg;
}

std::unique_ptr<NesMachine> MakeMachine(NesDriverConfig cfg = BuildSyntheticDriver()) {
    auto machine = std::make_unique<NesMachine>(BuildSyntheticCartridge(), std::move(cfg));
    machine->Initialize();
    return machine;
}

using Channels = std::vector<std::vector<float>>;

// One second of output is 192000 ticks.
Channels RenderChannels(NesMachine& machine, std::uint64_t ticks, LogicalChannelSink& sink) {
    machine.Render(EmuDuration(ticks), &sink);
    Channels out;
    for (const LogicalChannel& channel : machine.LogicalChannels()) {
        const LogicalChannelBuffer& buffer = sink.ChannelBuffer(channel.Id);
        out.emplace_back(buffer.Data(), buffer.Data() + buffer.Size());
    }
    sink.ClearAll();
    return out;
}

float PeakOf(const std::vector<float>& samples) {
    float peak = 0.0f;
    for (float s : samples) peak = std::max(peak, std::fabs(s));
    return peak;
}

MusicCommand Cmd(MusicCommandKind kind, std::uint8_t value) {
    MusicCommand c;
    c.Kind = kind;
    c.Value = value;
    return c;
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

std::optional<std::string> ReadFileText(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::nullopt;
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

struct ApuWrite {
    std::uint64_t frame;
    std::uint8_t reg;
    std::uint8_t value;
    bool operator==(const ApuWrite& o) const { return frame == o.frame && reg == o.reg && value == o.value; }
};

std::vector<ApuWrite> ReadReferenceLog(const std::filesystem::path& path) {
    std::vector<ApuWrite> writes;
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)) {
        unsigned frame = 0, reg = 0, value = 0;
        std::istringstream fields(line);
        fields >> std::dec >> frame >> std::hex >> reg >> value;
        if (fields.fail()) continue;
        if (reg == 0x14 || reg >= 0x16) continue;
        writes.push_back({frame, static_cast<std::uint8_t>(reg), static_cast<std::uint8_t>(value)});
    }
    return writes;
}

}  // namespace

void NesMachineRegisteredInDefaultCatalog() {
    std::printf("NesMachineRegisteredInDefaultCatalog\n");
    const MachineDefinition* def = DefaultMachineRegistry().Find(MachineId("NINTENDO_NES"));
    Check(def != nullptr, "catalog: NINTENDO_NES is registered");
    if (def == nullptr) return;
    Check(def->Category == MachineCategory::Console, "catalog: NINTENDO_NES is a Console");
    Check(def->Manufacturer == "Nintendo" && def->DisplayName == "NES", "catalog: manufacturer Nintendo, system NES");
    Check(def->Variants.size() == 1 && def->Variants[0].Id == VariantId("NES_BASE"), "catalog: one variant, NES_BASE");
    std::unique_ptr<IMachine> machine =
        DefaultMachineRegistry().Create(MachineId("NINTENDO_NES"), VariantId("NES_BASE"), MachineResources{});
    Check(machine != nullptr, "catalog: a Machine can be created with no resources (registered, not playable)");
    if (machine) machine->Initialize();
}

void NesMachineExposesFiveLogicalChannels() {
    std::printf("NesMachineExposesFiveLogicalChannels\n");
    auto machine = MakeMachine();
    const std::vector<LogicalChannel>& channels = machine->LogicalChannels();
    Check(channels.size() == 5, "5 LogicalChannels");
    static const char* const kNames[5] = {"Pulse 1", "Pulse 2", "Triangle", "Noise", "DMC"};
    bool names = true, rates = true, gains = true;
    for (std::size_t c = 0; c < channels.size(); ++c) {
        names = names && channels[c].DeviceName == "RP2A03" && channels[c].ChannelName == kNames[c] && channels[c].Id == c;
        rates = rates && channels[c].NativeSampleRate == NesMachine::kApuSampleRateHz;
        gains = gains && channels[c].RoutingGain == NesMachine::kRoutingGain;
    }
    Check(names, "device RP2A03, channels Pulse 1 / Pulse 2 / Triangle / Noise / DMC with ids 0-4");
    Check(rates, "NativeSampleRate is the APU stream rate (CPU clock / 4)");
    Check(gains, "RoutingGain is 1.8 (MAME's 0.90 mono route gain times the Mixer's centre factor of 2)");
}

void NesMachineStartupClearsRamAndRunsInitRoutinesInOrder() {
    std::printf("NesMachineStartupClearsRamAndRunsInitRoutinesInOrder\n");
    auto machine = MakeMachine();
    Check(machine->RamForTest(0x0010) == 0xFF, "RAM outside ram_clear is filled with 0xFF");
    Check(machine->RamForTest(0x0350) == 0x00, "RAM inside ram_clear is zero");
    Check(machine->RamForTest(0x0300) == 0x11, "init routine 1 ran (stored 0x11) after the clear");
    Check(machine->RamForTest(0x0301) == 0x12, "init routine 2 ran after routine 1 (read 0x11, stored 0x12)");
    Check(machine->RamForTest(0x0310) == 1, "the driver has run exactly one frame of its frame routines before any song");
    Check(machine->RamForTest(0x0311) == 1, "frame routine 2 saw the count frame routine 1 had just made");
    Check(machine->FramesStartedForTest() == 0, "frame numbering starts at zero for the first rendered frame");
    const auto cpu = machine->CpuSnapshotForTest();
    Check(cpu.pc >= NesMachine::kStartCodeAddress && cpu.pc < NesMachine::kStartCodeAddress + 0x100,
          "after start-up the CPU waits in the generated code");
}

void NesMachineFrameRoutinesRunOncePerFrameInOrder() {
    std::printf("NesMachineFrameRoutinesRunOncePerFrameInOrder\n");
    auto machine = MakeMachine();
    LogicalChannelSink sink;
    RenderChannels(*machine, 192000, sink);
    // Frames start at k * 29780.5 cycles, k = 0, 1, ...; one second is 1789772 cycles, so k = 0..60 start (61 frames),
    // on top of the one start-up frame.
    Check(machine->RamForTest(0x0310) == 62, "61 frames started in one second (plus the start-up frame)");
    Check(machine->RamForTest(0x0311) == 62, "frame routine 2 follows routine 1 in every frame");
    Check(machine->FramesStartedForTest() == 61, "FramesStartedForTest counts the frames of the rendered time");
    // Time is exact whatever the block size.
    auto split = MakeMachine();
    LogicalChannelSink sink2;
    for (int i = 0; i < 750; ++i) RenderChannels(*split, 256, sink2);
    Check(split->RamForTest(0x0310) == 62 && split->FramesStartedForTest() == 61, "the same frame count with 256-tick blocks");
}

void NesMachineSendCommandWritesTheDriverVariables() {
    std::printf("NesMachineSendCommandWritesTheDriverVariables\n");
    auto machine = MakeMachine();
    machine->SendCommand(Cmd(MusicCommandKind::Raw, 7));
    Check(machine->RamForTest(0x95) == 7, "a Raw command writes the song number to the bgm variable");
    Check(machine->RamForTest(0x8C) == 0x00, "the effect variable is untouched (zero from the ram_clear range)");

    machine->SendCommand(Cmd(MusicCommandKind::Effect, 4));
    Check(machine->RamForTest(0x8C) == 4, "an Effect command writes the effect number to the sfx variable");
    Check(machine->RamForTest(0x95) == 0x00, "the second command started from a freshly initialized state (bgm variable back to zero)");
    Check(machine->RamForTest(0x0310) == 1, "the reset ran the start-up again (one start-up frame)");
}

void NesMachineAcceptsOnlyTheKindsItCanPlay() {
    std::printf("NesMachineAcceptsOnlyTheKindsItCanPlay\n");
    auto machine = MakeMachine();
    Check(machine->AcceptsCommand(Cmd(MusicCommandKind::Raw, 1)), "Raw accepted");
    Check(machine->AcceptsCommand(Cmd(MusicCommandKind::Effect, 1)), "Effect accepted");
    Check(machine->AcceptsCommand(Cmd(MusicCommandKind::Stop, 0)), "Stop accepted");
    Check(!machine->AcceptsCommand(Cmd(MusicCommandKind::Wsg, 1)), "Wsg rejected");

    NesDriverConfig noVariables = BuildSyntheticDriver();
    noVariables.bgmVariable.reset();
    noVariables.sfxVariable.reset();
    auto bare = MakeMachine(noVariables);
    Check(!bare->AcceptsCommand(Cmd(MusicCommandKind::Raw, 1)), "Raw rejected when the game XML names no bgm variable");
    Check(!bare->AcceptsCommand(Cmd(MusicCommandKind::Effect, 1)), "Effect rejected when the game XML names no sfx variable");
}

void NesMachineSoundRoutinesTouchPpuAndInputStandIns() {
    std::printf("NesMachineSoundRoutinesTouchPpuAndInputStandIns\n");
    NesDriverConfig cfg = BuildSyntheticDriver();
    cfg.frame.push_back(kFrame5);
    auto machine = MakeMachine(cfg);
    // The start-up frame already ran frame routine 5 once.
    const NesMachine::BusStats before = machine->BusStatsForTest();
    Check(before.ppuReads == 1 && before.ppuWrites == 1 && before.inputReads == 1 && before.inputWrites == 1,
          "the start-up frame made one picture-chip read/write and one controller read/write");
    LogicalChannelSink sink;
    RenderChannels(*machine, 192000, sink);
    const NesMachine::BusStats after = machine->BusStatsForTest();
    Check(after.ppuReads == 62 && after.ppuWrites == 62 && after.inputReads == 62 && after.inputWrites == 62,
          "61 more frames: one access each way per frame, all counted");
    Check(machine->RamForTest(0x0310) == 62, "the stand-ins did not disturb the other routines");
}

void NesMachineSwitchIsIdenticalToAFreshStart() {
    std::printf("NesMachineSwitchIsIdenticalToAFreshStart\n");
    auto fresh = MakeMachine();
    fresh->SendCommand(Cmd(MusicCommandKind::Raw, 3));
    LogicalChannelSink freshSink;
    const Channels expected = RenderChannels(*fresh, 192000, freshSink);
    Check(PeakOf(expected[0]) > 0.01f, "the synthetic song sounds on pulse 1");

    auto switched = MakeMachine();
    switched->SendCommand(Cmd(MusicCommandKind::Raw, 9));
    LogicalChannelSink switchedSink;
    RenderChannels(*switched, 57600, switchedSink);
    switched->SendCommand(Cmd(MusicCommandKind::Effect, 2));
    RenderChannels(*switched, 19200, switchedSink);
    switched->SendCommand(Cmd(MusicCommandKind::Raw, 3));
    const Channels actual = RenderChannels(*switched, 192000, switchedSink);
    Check(actual == expected, "song 3 after other songs and an effect equals song 3 on a fresh machine, sample for sample");

    // A Stop command resets as well: nothing sounds afterwards.
    switched->SendCommand(Cmd(MusicCommandKind::Stop, 0));
    const Channels silent = RenderChannels(*switched, 38400, switchedSink);
    Check(PeakOf(silent[0]) < 1e-6f && PeakOf(silent[2]) < 1e-6f, "after Stop the channels are silent");
}

void NesMachineMuteRecomputesTheMixer() {
    std::printf("NesMachineMuteRecomputesTheMixer\n");
    // Pulse 1 and pulse 2 share one mixer term, so muting one changes the other's share of the mix.
    NesDriverConfig both = BuildSyntheticDriver();
    both.frame = {kFrame1, kFrame2, kFrame3, kFrame6};
    const MusicCommand song = Cmd(MusicCommandKind::Raw, 3);

    auto all = MakeMachine(both);
    all->SendCommand(song);
    LogicalChannelSink sinkAll;
    const Channels together = RenderChannels(*all, 96000, sinkAll);
    Check(PeakOf(together[0]) > 0.01f && PeakOf(together[1]) > 0.01f, "pulse 1 and pulse 2 both sound");

    auto muted = MakeMachine(both);
    muted->LogicalChannels()[1].Mute = true;
    muted->SendCommand(song);
    LogicalChannelSink sinkMuted;
    const Channels withoutPulse2 = RenderChannels(*muted, 96000, sinkMuted);
    Check(withoutPulse2[1] == together[1],
          "the muted channel's own samples are delivered unchanged (the Mixer applies the mute; meters and auto-stop see the source)");
    Check(PeakOf(withoutPulse2[0]) > 0.01f, "the other channel still sounds");
    Check(withoutPulse2[0] != together[0], "the other channel's share is recalculated without the muted one (the mixer is not linear)");

    // The recalculated share equals what a machine in which pulse 2 never sounded produces for pulse 1.
    NesDriverConfig onlyPulse1 = both;
    onlyPulse1.frame = {kFrame1, kFrame2, kFrame3};
    auto solo = MakeMachine(onlyPulse1);
    solo->SendCommand(song);
    LogicalChannelSink sinkSolo;
    const Channels soloOut = RenderChannels(*solo, 96000, sinkSolo);
    Check(soloOut[0] == withoutPulse2[0], "muting pulse 2 gives pulse 1 exactly what it gives when pulse 2 is silent");
}

void NesMachineIsDeterministicAcrossInstances() {
    std::printf("NesMachineIsDeterministicAcrossInstances\n");
    auto a = MakeMachine();
    auto b = MakeMachine();
    a->SendCommand(Cmd(MusicCommandKind::Raw, 6));
    b->SendCommand(Cmd(MusicCommandKind::Raw, 6));
    LogicalChannelSink sinkA, sinkB;
    bool same = true;
    for (int block = 0; block < 40; ++block) {
        const Channels x = RenderChannels(*a, 4800, sinkA);
        const Channels y = RenderChannels(*b, 4800, sinkB);
        same = same && x == y;
    }
    Check(same, "two machines given the same command render identical samples");
}

void NesMachineRejectsBadCartridgesAndDriverSettings() {
    std::printf("NesMachineRejectsBadCartridgesAndDriverSettings\n");
    std::vector<std::uint8_t> file(16 + 32768 + 8192, 0);
    file[0] = 'N';
    file[1] = 'E';
    file[2] = 'S';
    file[3] = 0x1A;
    file[4] = 2;     // 32 KB
    file[5] = 1;     // 8 KB CHR
    file[6] = 0x30;  // mapper 3
    for (std::size_t i = 0; i < 32768; ++i) file[16 + i] = static_cast<std::uint8_t>(i * 7);
    NesCartridge ok;
    bool threw = false;
    try {
        ok = ParseInesCartridge(file.data(), file.size());
    } catch (...) {
        threw = true;
    }
    Check(!threw && ok.mapper == 3 && ok.prg.size() == 32768 && ok.chr.size() == 8192, "a CNROM file image parses");

    auto fails = [](const std::vector<std::uint8_t>& bytes) {
        try {
            ParseInesCartridge(bytes.data(), bytes.size());
        } catch (const std::runtime_error&) {
            return true;
        }
        return false;
    };
    std::vector<std::uint8_t> noMagic = file;
    noMagic[0] = 'X';
    Check(fails(noMagic), "a file without the NES 1A header is rejected");
    std::vector<std::uint8_t> mapper4 = file;
    mapper4[6] = 0x10;
    Check(fails(mapper4), "mapper 1 is rejected");
    std::vector<std::uint8_t> shortFile(file.begin(), file.end() - 100);
    Check(fails(shortFile), "a file shorter than its header announces is rejected");
    std::vector<std::uint8_t> tooBig = file;
    tooBig[4] = 8;
    Check(fails(tooBig), "a PRG size other than 16 or 32 KB is rejected");

    std::vector<std::uint8_t> small(16 + 16384, 0xEA);
    std::fill(small.begin(), small.begin() + 16, std::uint8_t{0});
    small[0] = 'N';
    small[1] = 'E';
    small[2] = 'S';
    small[3] = 0x1A;
    small[4] = 1;
    small[5] = 0;
    NesCartridge sixteen;
    threw = false;
    try {
        sixteen = ParseInesCartridge(small.data(), small.size());
    } catch (...) {
        threw = true;
    }
    Check(!threw && sixteen.prg.size() == 16384 && sixteen.chr.empty(), "a 16 KB NROM image with CHR RAM parses");

    // Assembling: PRG/CHR checks against the driver's CRC-32 values.
    const std::string driverXml =
        "<driver ram_clear=\"0-0x7FF\" init=\"0x8000\" frame=\"0x8100\" bgm=\"0x95\" prg_crc32=\"00000001\"/>";
    MachineResources resources;
    resources.Add(ResolvedResource("cart", file));
    resources.Add(ResolvedResource("driver_xml", std::vector<std::uint8_t>(driverXml.begin(), driverXml.end())));
    threw = false;
    std::string what;
    try {
        AssembleNesMachineInputs(resources);
    } catch (const std::runtime_error& e) {
        threw = true;
        what = e.what();
    }
    Check(threw && what.find("PRG CRC-32") != std::string::npos, "a PRG that does not match prg_crc32 is rejected, naming the check");

    MachineResources missingDriver;
    missingDriver.Add(ResolvedResource("cart", file));
    threw = false;
    try {
        AssembleNesMachineInputs(missingDriver);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    Check(threw, "a cartridge without the game's <driver> settings is rejected");
}

void NesDriverConfigParsesAndRejects() {
    std::printf("NesDriverConfigParsesAndRejects\n");
    NesDriverConfig cfg;
    std::string error;
    const bool ok = ParseNesDriverConfig(
        "<driver ram_clear=\"0x0000-0x07FF\" init=\"0xB666, 0xB809\" frame=\"46736 0xB82A\" bgm=\"0x95\" sfx=\"140\" "
        "prg_crc32=\"e6c4ec06\" chr_crc32=\"67651e57\"/>",
        cfg, &error);
    Check(ok, "a complete <driver> parses");
    Check(cfg.ramClear && cfg.ramClear->first == 0 && cfg.ramClear->second == 0x7FF, "ram_clear range");
    Check(cfg.init == std::vector<std::uint16_t>({0xB666, 0xB809}), "init routines (comma separated)");
    Check(cfg.frame == std::vector<std::uint16_t>({46736, 0xB82A}), "frame routines (decimal and hex mixed)");
    Check(cfg.bgmVariable == 0x95 && cfg.sfxVariable == 140, "bgm and sfx variables");
    Check(cfg.prgCrc32 == 0xe6c4ec06u && cfg.chrCrc32 == 0x67651e57u, "CRC-32 values are hexadecimal");

    auto rejects = [](const std::string& xml) {
        NesDriverConfig c;
        std::string e;
        return !ParseNesDriverConfig(xml, c, &e) && !e.empty();
    };
    Check(rejects("<driver init=\"0x10000\"/>"), "an address above 0xFFFF is rejected");
    Check(rejects("<driver bgm=\"0x2000\"/>"), "a variable outside RAM is rejected");
    Check(rejects("<driver ram_clear=\"0x0800-0x0100\"/>"), "an empty ram_clear range is rejected");
    Check(rejects("<driver prg_crc32=\"xyz\"/>"), "a malformed CRC-32 is rejected");
    Check(rejects("<other/>"), "a missing <driver> element is rejected");
    std::string many = "<driver init=\"";
    for (int i = 0; i < 61; ++i) many += "0x8000 ";
    many += "\"/>";
    Check(rejects(many), "more than 60 routines are rejected");

    const std::string extracted =
        ExtractDriverElementXml("<chipestra><game id=\"x\"><driver init=\"0x8000\" bgm=\"0x95\"/><tracks/></game></chipestra>");
    NesDriverConfig fromExtract;
    Check(ParseNesDriverConfig(extracted, fromExtract, &error) && fromExtract.bgmVariable == 0x95,
          "ExtractDriverElementXml returns the <driver> element in a form ParseNesDriverConfig reads");
    Check(ExtractDriverElementXml("<chipestra><game id=\"x\"/></chipestra>").empty(), "no <driver> gives an empty string");
}

// Real ROM (CHIPESTRA_ROM_ROOT): starsoldier.xml is read, resolved and played exactly as the application
// does. With CHIPESTRA_REFERENCE_DIR also set (its nes/starsoldier folder holds the register logs of a MAME run, named
// stream_bgm<N>_u.txt and stream_sfx<N>_u.txt: "<frame> <register> <value>", frames counted from the request),
// the first 1500 frames of APU writes of every song and effect must equal that log.
void NesMachineViaDefinitionXmlRealRomPlaysAllSongs() {
    std::printf("NesMachineViaDefinitionXmlRealRomPlaysAllSongs\n");
    const char* romRootEnv = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRootEnv == nullptr || romRootEnv[0] == '\0') {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is not set - real-ROM bring-up not exercised\n");
        return;
    }
    const auto path = FindDefinitionXml("starsoldier.xml");
    Check(path.has_value(), "xml bring-up: starsoldier.xml is present (run from the repo root)");
    if (!path) return;
    const auto text = ReadFileText(*path);
    Check(text.has_value(), "xml bring-up: file is readable");
    if (!text) return;
    const XmlParseResult parsed = ParseGameDefinition(*text);
    Check(parsed.Status == XmlParseStatus::Ok, "xml bring-up: starsoldier.xml parses cleanly");
    if (parsed.Status != XmlParseStatus::Ok) return;
    const GameDefinition& def = parsed.Definition;
    Check(def.Machine == "NINTENDO_NES" && def.Variant == "NES_BASE", "xml bring-up: machine NINTENDO_NES / variant NES_BASE");

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
    ResourceResolutionResult res = resolver.Resolve(required);
    Check(res.Ok, "xml bring-up: the cartridge resolves under CHIPESTRA_ROM_ROOT (archive \"Star Soldier (Japan)\")");
    if (!res.Ok) {
        for (const auto& f : res.Failures)
            std::printf("    resolve fail '%s' code %d (%s)\n", f.LogicalName.c_str(), static_cast<int>(f.Result), f.Detail.c_str());
        return;
    }
    const std::string driverXml = ExtractDriverElementXml(*text);
    Check(!driverXml.empty(), "xml bring-up: the <driver> element is present");
    res.Resources.Add(ResolvedResource("driver_xml", std::vector<std::uint8_t>(driverXml.begin(), driverXml.end())));

    // The reference logs live in one folder per game below CHIPESTRA_REFERENCE_DIR: nes/starsoldier/.
    const char* referenceRoot = GetEnvVar("CHIPESTRA_REFERENCE_DIR");
    const bool haveReference = referenceRoot != nullptr && referenceRoot[0] != '\0';
    const std::filesystem::path referenceDir = haveReference ? std::filesystem::path(referenceRoot) / "nes" / "starsoldier" : std::filesystem::path();
    if (!haveReference) std::printf("  SKIP: CHIPESTRA_REFERENCE_DIR is not set - the APU write sequences are not compared with the MAME logs\n");

    constexpr std::uint64_t kFrames = 1500;
    // 1500 frames of 29780.5 CPU cycles, in 192 kHz ticks.
    const std::uint64_t ticks = kFrames * 59561ull / 2 * 192000ull / NesMachine::kCpuClockHz + 1;

    auto create = [&]() {
        std::unique_ptr<IMachine> m = DefaultMachineRegistry().Create(MachineId(def.Machine), VariantId(def.Variant), res.Resources);
        if (m) m->Initialize();
        return m;
    };

    // The driver's songs and effects by number (the numbers the logs are named by), whatever the XML lists: songs
    // 1-10 (10 is nearly silent and left out of the XML) and effects 1-12.
    struct Request {
        bool isEffect;
        unsigned number;
    };
    std::vector<Request> requests;
    for (unsigned n = 1; n <= 10; ++n) requests.push_back({false, n});
    for (unsigned n = 1; n <= 12; ++n) requests.push_back({true, n});
    for (const TrackDefinition& track : def.Tracks) {
        if (track.IsControlCommand) continue;
        const unsigned number = static_cast<unsigned>(std::strtoul(track.Command.c_str(), nullptr, 0));
        const bool listed = std::any_of(requests.begin(), requests.end(), [&](const Request& r) { return r.isEffect == track.IsEffect && r.number == number; });
        Check(listed, ("xml bring-up: track " + track.Id + " is one of the driver's numbered requests").c_str());
    }

    std::uint64_t totalPpu = 0, totalInput = 0;
    for (const Request& request : requests) {
        const bool isEffect = request.isEffect;
        const unsigned number = request.number;
        const std::string label = std::string(isEffect ? "sfx " : "bgm ") + std::to_string(number);

        std::unique_ptr<IMachine> base = create();
        Check(base != nullptr, (label + ": the Machine is created from the XML's own settings").c_str());
        if (!base) return;
        NesMachine& machine = static_cast<NesMachine&>(*base);

        std::vector<ApuWrite> writes;
        machine.SetApuWriteObserverForTest([&](std::uint64_t frame, std::uint8_t reg, std::uint8_t value) {
            if (frame < kFrames && reg < 0x16) writes.push_back({frame, reg, value});
        });
        const MusicCommand command = Cmd(isEffect ? MusicCommandKind::Effect : MusicCommandKind::Raw, static_cast<std::uint8_t>(number));
        Check(machine.AcceptsCommand(command), (label + ": accepted by the Machine").c_str());
        machine.SendCommand(command);
        LogicalChannelSink sink;
        const Channels channels = RenderChannels(machine, ticks, sink);
        float peak = 0.0f;
        for (const auto& c : channels) peak = std::max(peak, PeakOf(c));
        const NesMachine::BusStats stats = machine.BusStatsForTest();
        totalPpu += stats.ppuReads + stats.ppuWrites;
        totalInput += stats.inputReads + stats.inputWrites;
        std::printf("  %s: %zu APU writes, peak %.3f, picture-chip accesses %llu (reads %llu), controller accesses %llu\n",
                    label.c_str(), writes.size(), peak, static_cast<unsigned long long>(stats.ppuReads + stats.ppuWrites),
                    static_cast<unsigned long long>(stats.ppuReads), static_cast<unsigned long long>(stats.inputReads + stats.inputWrites));
        if (isEffect || number != 10) Check(peak > 0.001f, (label + ": sounds").c_str());  // song 10 is nearly silent

        if (haveReference) {
            const std::filesystem::path log = referenceDir / ("stream_" + std::string(isEffect ? "sfx" : "bgm") + std::to_string(number) + "_u.txt");
            if (!std::filesystem::exists(log)) {
                std::printf("    (no reference log for %s)\n", label.c_str());
                continue;
            }
            std::vector<ApuWrite> expected = ReadReferenceLog(log);
            expected.erase(std::remove_if(expected.begin(), expected.end(), [](const ApuWrite& w) { return w.frame >= kFrames; }),
                           expected.end());
            // The MAME run also executes the game's own main program, which calls the driver's sound initialization
            // when the screen changes (a $4011 write): Chipestra runs only the routines named in the XML, so its
            // sequence has to be the MAME sequence up to that point, and nothing else may differ.
            std::size_t i = 0;
            while (i < expected.size() && i < writes.size() && expected[i] == writes[i]) ++i;
            const bool same = i == writes.size() && (writes.size() == expected.size() || expected[writes.size()].reg == 0x11);
            if (!same) {
                std::printf("    first difference at write %zu of %zu (reference) / %zu (Chipestra)", i, expected.size(), writes.size());
                if (i < expected.size())
                    std::printf("; reference frame %llu reg %02X value %02X", static_cast<unsigned long long>(expected[i].frame),
                                expected[i].reg, expected[i].value);
                if (i < writes.size())
                    std::printf("; Chipestra frame %llu reg %02X value %02X", static_cast<unsigned long long>(writes[i].frame),
                                writes[i].reg, writes[i].value);
                std::printf("\n");
            } else {
                std::printf("    all %zu writes equal the MAME log (the log goes on for %zu more, from frame %llu)\n", writes.size(),
                            expected.size() - writes.size(),
                            static_cast<unsigned long long>(writes.size() < expected.size() ? expected[writes.size()].frame : 0));
            }
            Check(same, (label + ": the APU write sequence equals the MAME log (frame, register, value)").c_str());
        }
    }
    std::printf("  sound routines' picture-chip accesses in total: %llu, controller accesses in total: %llu\n",
                static_cast<unsigned long long>(totalPpu), static_cast<unsigned long long>(totalInput));

    // Switch check on the real driver: song 3 after song 1 and effect 2 equals song 3 on a fresh machine.
    auto play = [&](const std::vector<MusicCommand>& commands) {
        std::unique_ptr<IMachine> m = create();
        LogicalChannelSink s;
        Channels out;
        for (std::size_t n = 0; n < commands.size(); ++n) {
            m->SendCommand(commands[n]);
            const bool last = n + 1 == commands.size();
            m->Render(EmuDuration(last ? 192000 * 4 : 96000), &s);
            if (!last) s.ClearAll();
        }
        for (const LogicalChannel& channel : m->LogicalChannels())
            out.emplace_back(s.ChannelBuffer(channel.Id).Data(), s.ChannelBuffer(channel.Id).Data() + s.ChannelBuffer(channel.Id).Size());
        return out;
    };
    const Channels fresh = play({Cmd(MusicCommandKind::Raw, 3)});
    const Channels switched = play({Cmd(MusicCommandKind::Raw, 1), Cmd(MusicCommandKind::Effect, 2), Cmd(MusicCommandKind::Raw, 3)});
    Check(fresh == switched, "real driver: song 3 after song 1 and effect 2 equals song 3 on a fresh machine, sample for sample");

    // Music Stop (the control track): silent from the first frame, and a song chosen afterwards sounds again.
    const auto stopTrack = std::find_if(def.Tracks.begin(), def.Tracks.end(), [](const TrackDefinition& t) { return t.IsControlCommand; });
    Check(stopTrack != def.Tracks.end() && stopTrack->Command == "0x00", "Music Stop: the XML has a control track with command 0x00");
    if (stopTrack != def.Tracks.end()) {
        std::unique_ptr<IMachine> m = create();
        LogicalChannelSink s;
        m->SendCommand(Cmd(MusicCommandKind::Raw, 2));
        m->Render(EmuDuration(192000), &s);
        s.ClearAll();
        m->SendCommand(Cmd(MusicCommandKind::Raw, static_cast<std::uint8_t>(std::strtoul(stopTrack->Command.c_str(), nullptr, 0))));
        m->Render(EmuDuration(3195), &s);  // the first frame after Music Stop
        float firstFrame = 0;
        for (const LogicalChannel& channel : m->LogicalChannels())
            firstFrame = std::max(firstFrame, PeakOf(std::vector<float>(s.ChannelBuffer(channel.Id).Data(),
                                                                         s.ChannelBuffer(channel.Id).Data() + s.ChannelBuffer(channel.Id).Size())));
        s.ClearAll();
        m->Render(EmuDuration(192000), &s);
        float later = 0;
        for (const LogicalChannel& channel : m->LogicalChannels())
            later = std::max(later, PeakOf(std::vector<float>(s.ChannelBuffer(channel.Id).Data(),
                                                               s.ChannelBuffer(channel.Id).Data() + s.ChannelBuffer(channel.Id).Size())));
        s.ClearAll();
        Check(firstFrame < 1e-6f && later < 1e-6f, "Music Stop: every channel is silent from the first frame on");
        m->SendCommand(Cmd(MusicCommandKind::Raw, 3));
        m->Render(EmuDuration(192000), &s);
        float after = 0;
        for (const LogicalChannel& channel : m->LogicalChannels())
            after = std::max(after, PeakOf(std::vector<float>(s.ChannelBuffer(channel.Id).Data(),
                                                               s.ChannelBuffer(channel.Id).Data() + s.ChannelBuffer(channel.Id).Size())));
        Check(after > 0.001f, "Music Stop: a song chosen after it sounds");
    }
}
