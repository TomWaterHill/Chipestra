// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "dragon_spirit_machine_tests.h"

#include "default_machine_catalog.h"
#include "devices/namco/dragonspirit/dragon_spirit_machine.h"
#include "devices/namco/dragonspirit/dragon_spirit_rom_layout.h"
#include "machine_registry.h"
#include "mixer.h"
#include "resource_resolver.h"
#include "test_support.h"
#include "chipestra_abi.h"
#include "xml_definition_parser.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

// 0x12 = MC6809E NOP. A reset vector that reads back as 0x1212 lands inside this uniformly NOP-filled
// bank-0 region, so every test can leave the reset/IRQ/FIRQ/NMI vectors unset and let the CPU idle
// through NOPs. The CPU never clears the CC I bit that reset sets, so the periodic VBLANK IRQ this
// Machine asserts is never serviced/acked by this synthetic driver, which
// DragonSpiritMachineVblankIrqAssertsAndCanBeAcked below relies on.
std::vector<std::uint8_t> BuildSyntheticSoundCpuRom() {
    return std::vector<std::uint8_t>(DragonSpiritMemoryMap::kFullSoundRomSize, 0x12);
}

// Bank 0 stays NOP-filled (0x1212 reset vector safety); banks 1-7 each get
// a distinct marker byte (0xA0 + bank index) so DragonSpiritMachineBankSwitchSelectsAmongEightBanks
// can tell them apart after switching - none of these banks is executed (only
// read via ReadMemory), so they need not be valid code.
std::vector<std::uint8_t> BuildBankMarkerRom() {
    std::vector<std::uint8_t> rom(DragonSpiritMemoryMap::kFullSoundRomSize, 0x12);
    for (std::size_t bank = 1; bank < DragonSpiritMemoryMap::kBankCount; ++bank) {
        const std::uint8_t marker = static_cast<std::uint8_t>(0xA0 + bank);
        std::fill_n(rom.data() + bank * DragonSpiritMemoryMap::kBankedRomWindowSize,
                    DragonSpiritMemoryMap::kBankedRomWindowSize, marker);
    }
    return rom;
}

// A NOP-filled (opcode 0x01, HD63701's NOP - NOT M6809's 0x12) synthetic MCU internal ROM.
// A uniformly-0x01-filled buffer would put the reset vector at 0x0101, outside the
// 0xF000-0xFFFF internal ROM. Instead, a 2-byte "BRA *" self-loop (0x20 0xFE) is placed at the start
// of the ROM (physical 0xF000) and the reset vector (the buffer's last 2 bytes) points there: a stable
// infinite loop that never walks into the vector bytes, unlike a pure NOP-spin.
std::vector<std::uint8_t> BuildSyntheticMcuRom() {
    std::vector<std::uint8_t> rom(DragonSpiritMcuMemoryMap::kInternalRomSize, 0x01);
    rom[0] = 0x20;  // BRA
    rom[1] = 0xFE;  // offset -2: branches back to rom[0] (physical 0xF000)
    rom[DragonSpiritMcuMemoryMap::kInternalRomSize - 2] = 0xF0;  // reset vector high byte
    rom[DragonSpiritMcuMemoryMap::kInternalRomSize - 1] = 0x00;  // reset vector low byte -> 0xF000
    return rom;
}

// A synthetic MCU program that WRITES to DAC0/DAC1 itself, instead of the test poking DAC0/DAC1 via
// WriteMcuMemory(): a direct poke would be discarded, since Render()'s BeginBlock() clears events
// recorded before the Render() call. The program's writes happen once during Initialize()'s boot
// warm-up, and Dac8BitR2RDeviceAdapter holds the value across blocks, so a LATER Render() with a real
// sink still observes them. Opcodes: 0x86=LDAA #imm8, 0xB7=STAA extended, 0xC6=LDAB #imm8,
// 0xF7=STAB extended, 0x20 0xFE=BRA * self-loop.
std::vector<std::uint8_t> BuildDacWritingMcuRom() {
    std::vector<std::uint8_t> rom(DragonSpiritMcuMemoryMap::kInternalRomSize, 0x01);
    std::size_t offset = 0;
    auto put = [&](std::uint8_t b) { rom[offset++] = b; };
    put(0x86); put(0xFF);             // LDAA #$FF
    put(0xB7); put(0xD0); put(0x00);  // STAA $D000
    put(0xC6); put(0x00);             // LDAB #$00
    put(0xF7); put(0xD4); put(0x00);  // STAB $D400
    put(0x20); put(0xFE);             // BRA * (self-loop from here on, offset points at itself)
    rom[DragonSpiritMcuMemoryMap::kInternalRomSize - 2] = 0xF0;  // reset vector -> physical 0xF000
    rom[DragonSpiritMcuMemoryMap::kInternalRomSize - 1] = 0x00;
    return rom;
}

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

std::optional<std::filesystem::path> FindDspiritDefinitionFile() {
    const std::filesystem::path relative = std::filesystem::path("xml") / "dspirit.xml";
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

void DragonSpiritMachineRegisteredInDefaultCatalog() {
    // NAMCO_SYSTEM1/"System 1" is the board; "Dragon Spirit" is the game title in xml/dspirit.xml,
    // not the MachineId.
    const MachineDefinition* def = DefaultMachineRegistry().Find(MachineId("NAMCO_SYSTEM1"));
    Check(def != nullptr, "catalog: NAMCO_SYSTEM1 is registered in the default MachineRegistry");
    if (def != nullptr) {
        Check(def->Category == MachineCategory::Arcade, "catalog: NAMCO_SYSTEM1 Category == Arcade");
        Check(def->Manufacturer == "Namco", "catalog: NAMCO_SYSTEM1 Manufacturer == Namco");
        Check(!def->Variants.empty(), "catalog: NAMCO_SYSTEM1 has at least one Variant");
    }
}

void DragonSpiritMachineExposes18LogicalChannels() {
    // 18 channels: YM2151 8 + CUS30 8 + DAC0/DAC1 1 each.
    DragonSpiritMachine machine(BuildSyntheticSoundCpuRom());
    machine.Initialize();
    const std::vector<LogicalChannel>& channels = machine.LogicalChannels();
    Check(channels.size() == 18, "DragonSpiritMachine exposes 18 LogicalChannels (YM2151 8 + CUS30 8 + DAC0/DAC1 1 each)");
    Check(channels[0].DeviceName == "YM2151" && channels[0].ChannelName == "FM1", "channel 0 is YM2151 FM1");
    Check(channels[8].DeviceName == "CUS30" && channels[8].ChannelName == "Voice1", "channel 8 is the first CUS30 voice");
    Check(channels[0].NativeSampleRate == 3'579'545 / 64, "YM2151 NativeSampleRate uses Dragon Spirit's real clock (3,579,545Hz)");
    Check(channels[8].NativeSampleRate == 12'000, "CUS30 NativeSampleRate is the chip's own real 12,000Hz clock (kInputClocksPerSample==1)");
    Check(channels[16].DeviceName == "DAC1" && channels[16].ChannelName == "Output", "channel 16 is DAC1 (m_dac[0])");
    Check(channels[17].DeviceName == "DAC2" && channels[17].ChannelName == "Output", "channel 17 is DAC2 (m_dac[1])");
    // The DAC channels declare their real 6000Hz rate, and Render() calls Reconstruct() with a true
    // 6000Hz-rate sample count, so ResamplingMixerFeed's anti-aliased resampler (not a bare
    // passthrough) handles the 6000->192000 upsampling, like every other low-native-rate chip.
    Check(channels[16].NativeSampleRate == DragonSpiritMachine::kDacSampleRateHz,
          "DAC1 NativeSampleRate is the real-hardware-confirmed 6000Hz");
    Check(channels[17].NativeSampleRate == DragonSpiritMachine::kDacSampleRateHz,
          "DAC2 NativeSampleRate is the real-hardware-confirmed 6000Hz");
}

void DragonSpiritMachineSendCommandWritesTriRam() {
    DragonSpiritMachine machine(BuildSyntheticSoundCpuRom());
    machine.Initialize();

    MusicCommand cmd;
    cmd.Value = 0x11;
    machine.SendCommand(cmd);

    Check(machine.ReadMemory(0x7100) == 0x11, "SendCommand() writes the command byte to TRIRAM offset 0x100 (physical 0x7100)");
    Check(machine.ReadMemory(0x7101) == 0x40, "SendCommand() writes the ready-flag byte (0x40) to TRIRAM offset 0x101 (physical 0x7101)");
}

void DragonSpiritMachineSendCommandIsReentrantSafe() {
    DragonSpiritMachine machine(BuildSyntheticSoundCpuRom());
    machine.Initialize();

    MusicCommand a;
    a.Value = 0x11;
    MusicCommand b;
    b.Value = 0x16;
    machine.SendCommand(a);
    machine.SendCommand(b);
    machine.SendCommand(a);

    Check(machine.ReadMemory(0x7100) == 0x11, "repeated SendCommand() calls with no Render() between them leave TRIRAM holding the LAST command sent");
    Check(machine.ReadMemory(0x7101) == 0x40, "the ready flag stays consistent across repeated sends (no crash/corruption, DragonSpiritCommandInterface has no line to leave in a bad state)");
}

void DragonSpiritMachineVblankIrqAssertsAndCanBeAcked() {
    DragonSpiritMachine machine(BuildSyntheticSoundCpuRom());
    machine.Initialize();

    // Initialize()'s boot warm-up (kBootWarmupFrames = 6144 canonical ticks) spans more than one
    // VBLANK period (kVblankIrqPeriodCanonicalTicks = 3168), and the synthetic NOP-only driver never
    // clears CC bit I (never services, hence never acks, the interrupt) - so the line is left asserted.
    Check(machine.M6809Snapshot().irqLine, "VBLANK IRQ is asserted after warm-up (never serviced/acked by the NOP-only synthetic driver)");

    machine.WriteMemory(0xE000, 0x00);
    Check(!machine.M6809Snapshot().irqLine, "writing to physical 0xE000 acks (clears) the VBLANK IRQ line (DragonSpiritMemoryMap::SetIrqAckCallback wiring)");
}

void DragonSpiritMachineBankSwitchSelectsAmongEightBanks() {
    DragonSpiritMachine machine(BuildBankMarkerRom());
    machine.Initialize();
    Check(machine.CurrentBankForTest() == 0, "bank 0 is selected at reset (sound_bankswitch_w's own power-on default)");

    for (std::uint32_t bank = 1; bank < DragonSpiritMemoryMap::kBankCount; ++bank) {
        machine.WriteMemory(0xC000, static_cast<std::uint8_t>(bank << 4));
        Check(machine.CurrentBankForTest() == bank, "writing (bank<<4) to physical 0xC000 selects that bank");
        const std::uint8_t expectedMarker = static_cast<std::uint8_t>(0xA0 + bank);
        Check(machine.ReadMemory(0x0000) == expectedMarker, "the banked window (0x0000-0x3FFF) shows the newly-selected bank's own data");
        Check(machine.ReadMemory(0xC000) == 0x12, "the fixed window (0xC000-0xFFFF) always reads bank 0's own data, independent of the banked-window selection");
    }
}

void DragonSpiritMachineDevicesReachableViaMemoryMap() {
    DragonSpiritMachine machine(BuildSyntheticSoundCpuRom());
    machine.Initialize();

    // YM2151 status read (0x4000/0x4001): real YM2151 returns the status byte for any read on its range.
    machine.WriteMemory(0x4000, 0x08);  // key-on register address (harmless, no data byte follows)
    Check(machine.Ym2151ForTest().ReadStatus() == machine.ReadMemory(0x4000),
          "reading YM2151's status via the memory map reaches the same Ym2151DeviceAdapter instance");

    // CUS30 waveram (0x5000-0x53FF local offset 0x000-0x0FF): a write through the memory map must reach
    // the SAME device instance, not be silently dropped or routed elsewhere.
    machine.WriteMemory(0x5000, 0xAB);
    Check(machine.Cus30ForTest().Read8(0) == 0xAB, "a CUS30 waveram write via the memory map reaches the real device instance");
    Check(machine.ReadMemory(0x5000) == 0xAB, "reading it back via the memory map returns the same byte");
}

void DragonSpiritMachineRenderIsDeterministicAcrossInstances() {
    auto runSequence = [](DragonSpiritMachine& machine) -> std::vector<float> {
        machine.Initialize();
        MusicCommand cmd;
        cmd.Value = 0x11;
        machine.SendCommand(cmd);
        LogicalChannelSink sink;
        for (int block = 0; block < 8; ++block) {
            machine.Render(EmuDuration{256}, &sink);
        }
        std::vector<float> combined;
        for (const LogicalChannel& channel : machine.LogicalChannels()) {
            if (!sink.HasChannel(channel.Id)) continue;
            const LogicalChannelBuffer& buffer = sink.ChannelBuffer(channel.Id);
            combined.insert(combined.end(), buffer.Data(), buffer.Data() + buffer.Size());
        }
        return combined;
    };

    DragonSpiritMachine a(BuildSyntheticSoundCpuRom());
    DragonSpiritMachine b(BuildSyntheticSoundCpuRom());
    const std::vector<float> outA = runSequence(a);
    const std::vector<float> outB = runSequence(b);
    Check(!outA.empty(), "the deterministic-run comparison actually rendered samples");
    Check(outA.size() == outB.size(), "two independently-constructed instances render the same sample count for the same input");
    bool identical = outA.size() == outB.size();
    for (std::size_t i = 0; identical && i < outA.size(); ++i) {
        identical = outA[i] == outB[i];
    }
    Check(identical, "two independently-constructed instances render bit-identical output for the same input");
}

// ---- MCU integration ----

void DragonSpiritMachineMcuBootsAlongsideAudiocpu() {
    DragonSpiritMachine machine(BuildSyntheticSoundCpuRom(), BuildSyntheticMcuRom());
    machine.Initialize();
    Check(machine.McuForTest().Snapshot().pc == 0xF000,
          "MCU is running its own synthetic self-loop (physical 0xF000) after boot warm-up, alongside audiocpu");
    Check(machine.McuForTest().Snapshot().irq1Line,
          "MCU's own VBLANK IRQ1 is asserted after warm-up (never serviced/acked by the BRA-only synthetic driver, "
          "same convention as DragonSpiritMachineVblankIrqAssertsAndCanBeAcked's own audiocpu check)");
}

void DragonSpiritMachineTriRamIsSharedBetweenAudiocpuAndMcu() {
    DragonSpiritMachine machine(BuildSyntheticSoundCpuRom(), BuildSyntheticMcuRom());
    machine.Initialize();

    // audiocpu-local 0x7000-0x77FF and MCU-local 0xC000-0xC7FF must be the SAME physical TRIRAM array
    // (real hardware's single shared RAM chip).
    machine.WriteMemory(0x7010, 0xAB);
    Check(machine.ReadMcuMemory(0xC010) == 0xAB, "a TRIRAM write via audiocpu's own local address is visible at the MCU's own local address");

    machine.WriteMcuMemory(0xC020, 0xCD);
    Check(machine.ReadMemory(0x7020) == 0xCD, "a TRIRAM write via the MCU's own local address is visible at audiocpu's own local address");

    // The voice-trigger mailbox itself - not exercised by any
    // real driver code here (the synthetic MCU ROM is just a self-loop), but the shared-memory plumbing
    // it depends on is the same plumbing just proven above.
    machine.WriteMemory(0x702D, 0x01);
    machine.WriteMemory(0x702E, 0x40);
    Check(machine.ReadMcuMemory(0xC02D) == 0x01 && machine.ReadMcuMemory(0xC02E) == 0x40,
          "the voice-trigger mailbox (TRIRAM offset 0x02D/0x02E) is reachable from both CPUs' own local addresses");
}

void DragonSpiritMachineMcuVblankIrq1AssertsAndCanBeAcked() {
    DragonSpiritMachine machine(BuildSyntheticSoundCpuRom(), BuildSyntheticMcuRom());
    machine.Initialize();
    Check(machine.McuForTest().Snapshot().irq1Line, "MCU's VBLANK IRQ1 is asserted after warm-up");

    machine.WriteMcuMemory(DragonSpiritMcuMemoryMap::kIrqAckAddress, 0x00);
    Check(!machine.McuForTest().Snapshot().irq1Line,
          "writing to physical 0xF000 acks (clears) the MCU's own VBLANK IRQ1 line (DragonSpiritMcuMemoryMap::SetIrqAckCallback wiring)");
}

void DragonSpiritMachineMcuBankSwitchSelectsAmongTwentyFourBanks() {
    DragonSpiritMachine machine(BuildSyntheticSoundCpuRom(), BuildSyntheticMcuRom());
    machine.Initialize();
    Check(machine.McuCurrentBankForTest() == 0, "bank 0 is selected at reset (mcu_bankswitch_w's own power-on default)");

    // mcu_bankswitch_w's decode (DecodeVoiceBank), confirmed
    // against a real-hardware trace of the bank sequence a real voice sample triggers -
    // one known-answer case per chip-select group,
    // spanning the full 0-23 bank range.
    const struct { std::uint8_t data; std::uint32_t expectedBank; } kCases[] = {
        {0xFB, 1}, {0xF8, 2}, {0xF4, 4}, {0xEC, 8}, {0xDC, 12}, {0xBC, 16}, {0x7C, 20}, {0x7F, 23},
    };
    for (const auto& c : kCases) {
        machine.WriteMcuMemory(DragonSpiritMcuMemoryMap::kBankSelectAddress, c.data);
        Check(machine.McuCurrentBankForTest() == c.expectedBank,
              "writing the real-hardware-confirmed byte to physical 0xD800 selects the expected voice-ROM bank");
    }
}

void DragonSpiritMachineDac0Dac1ReceiveValuesViaMcuBusWrites() {
    // The MCU's own code (executed during Initialize()'s
    // boot warm-up) writes these values; see BuildDacWritingMcuRom().
    DragonSpiritMachine machine(BuildSyntheticSoundCpuRom(), BuildDacWritingMcuRom());
    machine.Initialize();

    LogicalChannelSink sink;
    machine.Render(EmuDuration{256}, &sink);

    Check(sink.HasChannel(DragonSpiritMachine::kDac0Base), "DAC0's own LogicalChannel is registered");
    Check(sink.HasChannel(DragonSpiritMachine::kDac1Base), "DAC1's own LogicalChannel is registered");
    const LogicalChannelBuffer& dac0Buf = sink.ChannelBuffer(DragonSpiritMachine::kDac0Base);
    const LogicalChannelBuffer& dac1Buf = sink.ChannelBuffer(DragonSpiritMachine::kDac1Base);
    // DAC0/DAC1 reconstruct at their own true 6000Hz native rate (8 samples per
    // 256-canonical-tick block, 256*6000/192000), not one sample per canonical tick, so
    // ResamplingMixerFeed's anti-aliased resampler does the 6000->192000 upsampling.
    Check(dac0Buf.Size() == 8 && dac1Buf.Size() == 8, "both DAC channels produced a full block of native-rate (6000Hz) samples");

    bool dac0Correct = true, dac1Correct = true;
    for (std::size_t i = 0; i < dac0Buf.Size(); ++i) {
        if (dac0Buf[i] != (255.0f - 128.0f) / 128.0f) dac0Correct = false;
    }
    for (std::size_t i = 0; i < dac1Buf.Size(); ++i) {
        if (dac1Buf[i] != (0.0f - 128.0f) / 128.0f) dac1Correct = false;
    }
    Check(dac0Correct, "a raw byte written to physical 0xD000 (via the MCU's own bus) reaches DAC0's own LogicalChannel, held for the whole block");
    Check(dac1Correct, "a raw byte written to physical 0xD400 reaches DAC1's own LogicalChannel independently, held for the whole block");
}

// ---- AssembleDragonSpiritSoundRoms (mcu/voice ROM resource wiring) ----

void DragonSpiritAssembleSoundRomsThrowsWhenRequiredResourceMissing() {
    MachineResources resources;
    resources.Add(ResolvedResource("soundcpu0", std::vector<std::uint8_t>(0x10000, 0x12)));
    resources.Add(ResolvedResource("soundcpu1", std::vector<std::uint8_t>(0x10000, 0x12)));
    // "mcu"/"voice0"-"voice4" deliberately omitted: missing required resources must fail explicitly.
    bool threw = false;
    try {
        AssembleDragonSpiritSoundRoms(resources);
    } catch (const std::runtime_error& e) {
        threw = true;
        const std::string what = e.what();
        Check(what.find("mcu") != std::string::npos, "the thrown message names the missing 'mcu' resource");
        Check(what.find("voice0") != std::string::npos, "the thrown message names the missing 'voice0' resource");
    }
    Check(threw, "AssembleDragonSpiritSoundRoms throws when resources are non-empty but mcu/voice ROMs are missing");
}

void DragonSpiritAssembleSoundRomsMirrorsVoice0AcrossChip0() {
    MachineResources resources;
    resources.Add(ResolvedResource("soundcpu0", std::vector<std::uint8_t>(0x10000, 0x12)));
    resources.Add(ResolvedResource("soundcpu1", std::vector<std::uint8_t>(0x10000, 0x12)));
    resources.Add(ResolvedResource("mcu", std::vector<std::uint8_t>(0x1000, 0x01)));
    std::vector<std::uint8_t> voice0(0x10000);
    for (std::size_t i = 0; i < voice0.size(); ++i) voice0[i] = static_cast<std::uint8_t>(i);  // distinguishable pattern
    resources.Add(ResolvedResource("voice0", voice0));
    resources.Add(ResolvedResource("voice1", std::vector<std::uint8_t>(0x20000, 0xA1)));
    resources.Add(ResolvedResource("voice2", std::vector<std::uint8_t>(0x20000, 0xA2)));
    resources.Add(ResolvedResource("voice3", std::vector<std::uint8_t>(0x20000, 0xA3)));
    resources.Add(ResolvedResource("voice4", std::vector<std::uint8_t>(0x20000, 0xA4)));

    const DragonSpiritSoundRoms roms = AssembleDragonSpiritSoundRoms(resources);
    Check(roms.VoiceRom.size() == 0xC0000, "VoiceRom is the full 0xC0000 6-chip region");

    // real hardware's ROM_LOAD_HS (namcos1.cpp): ROM_LOAD at offset 0, then ROM_RELOAD at
    // start+length - the SAME 0x10000 bytes mirrored into the second half of chip 0's own 0x20000 slot.
    bool firstHalfMatches = true, secondHalfMatches = true;
    for (std::size_t i = 0; i < 0x10000; ++i) {
        if (roms.VoiceRom[i] != voice0[i]) firstHalfMatches = false;
        if (roms.VoiceRom[0x10000 + i] != voice0[i]) secondHalfMatches = false;
    }
    Check(firstHalfMatches, "voice0's own bytes land at offset 0x00000 (ROM_LOAD)");
    Check(secondHalfMatches, "voice0's own bytes are ALSO mirrored at offset 0x10000 (ROM_RELOAD(start+length,length))");
}

void DragonSpiritAssembleSoundRomsPlacesVoiceChipsAtCorrectOffsets() {
    MachineResources resources;
    resources.Add(ResolvedResource("soundcpu0", std::vector<std::uint8_t>(0x10000, 0x12)));
    resources.Add(ResolvedResource("soundcpu1", std::vector<std::uint8_t>(0x10000, 0x12)));
    resources.Add(ResolvedResource("mcu", std::vector<std::uint8_t>(0x1000, 0x01)));
    resources.Add(ResolvedResource("voice0", std::vector<std::uint8_t>(0x10000, 0x00)));
    resources.Add(ResolvedResource("voice1", std::vector<std::uint8_t>(0x20000, 0xA1)));
    resources.Add(ResolvedResource("voice2", std::vector<std::uint8_t>(0x20000, 0xA2)));
    resources.Add(ResolvedResource("voice3", std::vector<std::uint8_t>(0x20000, 0xA3)));
    resources.Add(ResolvedResource("voice4", std::vector<std::uint8_t>(0x20000, 0xA4)));

    const DragonSpiritSoundRoms roms = AssembleDragonSpiritSoundRoms(resources);
    Check(roms.VoiceRom[0x20000] == 0xA1, "voice1 (physical chip 1) lands at offset 0x20000");
    Check(roms.VoiceRom[0x40000] == 0xA2, "voice2 (physical chip 2) lands at offset 0x40000");
    Check(roms.VoiceRom[0x60000] == 0xA3, "voice3 (physical chip 3) lands at offset 0x60000");
    Check(roms.VoiceRom[0x80000] == 0xA4, "voice4 (physical chip 4) lands at offset 0x80000");
    Check(roms.VoiceRom[0xA0000] == 0x00,
          "physical chip 5 (0xA0000-0xBFFFF) stays zero-filled - dspirit's own ROM_START has no ROM_LOAD there");
    Check(roms.McuProgram.size() == 0x1000 && roms.McuProgram[0] == 0x01, "McuProgram is populated from the 'mcu' resource");
}

// Regression guard: command 0x15 (FINAL SCENE)'s DAC1 voice content, built from
// xml/dspirit.xml's own declared resources (not a shortcut path).
//
// The MCU's idle-loop code (bank 1)
// does `CMPA #$A6` against TRIRAM offset 0 before it will leave that loop for the rest of its
// dispatch code (where the voice-trigger mailbox at offset 0x02D/0x02E is read) - a gate only the real
// main CPU (not emulated by this Machine) satisfies, by writing 0xA6 there.
// Without DragonSpiritMachine::Render() re-asserting this byte every
// VBLANK, the MCU never leaves that loop and the voice sample never plays; reverting that makes
// this test fail.
//
// DAC0 is NOT asserted here: on real hardware DAC0 only starts carrying content
// ~19.27s after command 0x15 is sent (DAC1 plays first, from ~13.45s), a separate mechanism
// that is not reproduced, so asserting on DAC0 would be a no-op or block this guard.
void DragonSpiritViaDefinitionXmlRealRomBringUp() {
    std::printf("DragonSpiritViaDefinitionXmlRealRomBringUp\n");

    const char* romRootEnv = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRootEnv == nullptr || romRootEnv[0] == '\0') {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is not set - real-ROM bring-up not exercised\n");
        return;
    }

    const auto path = FindDspiritDefinitionFile();
    Check(path.has_value(), "xml bring-up: xml/dspirit.xml is present (run from the repo root)");
    if (!path.has_value()) return;
    const auto text = ReadFileText(*path);
    Check(text.has_value(), "xml bring-up: file is readable");
    if (!text.has_value()) return;
    const XmlParseResult parsed = ParseGameDefinition(*text);
    Check(parsed.Status == XmlParseStatus::Ok, "xml bring-up: dspirit.xml parses cleanly");
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
    Check(res.Ok, "xml bring-up: every resource dspirit.xml declares resolves under CHIPESTRA_ROM_ROOT (archive=\"dspirit\")");
    if (!res.Ok) {
        for (const auto& f : res.Failures) {
            std::printf("    resolve fail '%s' code %d (%s)\n", f.LogicalName.c_str(), static_cast<int>(f.Result),
                        f.Detail.c_str());
        }
        return;
    }

    std::unique_ptr<IMachine> machine = DefaultMachineRegistry().Create(MachineId(def.Machine), VariantId(def.Variant), res.Resources);
    Check(machine != nullptr,
          "xml bring-up: DefaultMachineRegistry().Create() builds a Machine from the XML's own machine/variant/resources");
    if (!machine) return;
    machine->Initialize();

    const auto trackIt = std::find_if(def.Tracks.begin(), def.Tracks.end(),
                                       [](const TrackDefinition& t) { return t.Title.rfind("FINAL SCENE", 0) == 0; });
    Check(trackIt != def.Tracks.end(), "xml bring-up: \"FINAL SCENE\" track is declared");
    if (trackIt == def.Tracks.end()) return;
    const std::uint8_t commandByte = static_cast<std::uint8_t>(std::stoul(trackIt->Command, nullptr, 0));

    MusicCommand cmd;
    cmd.Kind = MusicCommandKind::Raw;
    cmd.Value = commandByte;
    machine->SendCommand(cmd);

    // 20s covers real hardware's ~13.4s DAC1 onset with comfortable margin.
    LogicalChannelSink sink;
    constexpr int kMeasureBlocks = (192000 / static_cast<int>(Mixer::kBlockSizeFrames)) * 20;
    double dac1Peak = 0.0;
    for (int i = 0; i < kMeasureBlocks; ++i) {
        sink.ClearAll();
        machine->Render(EmuDuration{Mixer::kBlockSizeFrames}, &sink);
        if (sink.HasChannel(DragonSpiritMachine::kDac1Base)) {
            const LogicalChannelBuffer& buf = sink.ChannelBuffer(DragonSpiritMachine::kDac1Base);
            for (std::size_t s = 0; s < buf.Size(); ++s) dac1Peak = std::max(dac1Peak, static_cast<double>(std::fabs(buf[s])));
        }
    }
    std::printf("  cmd 0x%02X \"%s\" via XML-declared resources: DAC1 peak=%.6f\n", commandByte, trackIt->Title.c_str(), dac1Peak);
    constexpr double kSilenceFloor = 0.01;
    Check(dac1Peak > kSilenceFloor,
          "xml bring-up: DAC1 is genuinely audible when the Machine is built from dspirit.xml's OWN declared resources - "
          "regression guard for the MCU idle-loop TRIRAM-readiness fix");
}

// --- Reset on song switch -------------------------------------------------------------------------
// This driver has no "silence everything" command (a new command swaps the FM voices but leaves the
// WSG/DAC playing), so DragonSpiritMachine::SendCommand() calls Reset() before any command that follows
// an earlier one (0x00 included).

void DragonSpiritFirstCommandAfterInitializeDoesNotReset() {
    std::printf("DragonSpiritFirstCommandAfterInitializeDoesNotReset\n");
    DragonSpiritMachine machine(BuildSyntheticSoundCpuRom());
    machine.Initialize();
    const std::uint32_t before = machine.ResetCountForTest();

    MusicCommand cmd;
    cmd.Value = 0x11;
    machine.SendCommand(cmd);
    Check(machine.ResetCountForTest() == before, "the first command after Initialize() does not reset");

    machine.Reset();
    const std::uint32_t afterReset = machine.ResetCountForTest();
    machine.SendCommand(cmd);
    Check(machine.ResetCountForTest() == afterReset, "the first command after Reset() does not reset either");
}

void DragonSpiritLaterCommandsReset() {
    std::printf("DragonSpiritLaterCommandsReset\n");
    DragonSpiritMachine machine(BuildSyntheticSoundCpuRom());
    machine.Initialize();
    MusicCommand cmd;
    cmd.Value = 0x11;
    machine.SendCommand(cmd);
    const std::uint32_t n = machine.ResetCountForTest();

    cmd.Value = 0x7F;
    machine.SendCommand(cmd);
    Check(machine.ResetCountForTest() == n + 1, "a second command resets once first");
    Check(machine.ReadMemory(0x7100) == 0x7F, "...and the new command still lands in TRIRAM");

    cmd.Value = 0x00;  // STOP counts as 'a command was sent'
    machine.SendCommand(cmd);
    Check(machine.ResetCountForTest() == n + 2, "STOP (0x00) after a command also resets");
    cmd.Value = 0x11;
    machine.SendCommand(cmd);
    Check(machine.ResetCountForTest() == n + 3, "a command after a STOP resets again (STOP counts as sent)");
}

void DragonSpiritResetKeepsChannelMuteGainPan() {
    std::printf("DragonSpiritResetKeepsChannelMuteGainPan\n");
    DragonSpiritMachine machine(BuildSyntheticSoundCpuRom());
    machine.Initialize();
    std::vector<LogicalChannel>& channels = machine.LogicalChannels();
    channels[0].Mute = true;
    channels[1].Gain = 0.25f;
    channels[2].Pan = -0.5f;
    channels[2].PanOverridden = true;

    MusicCommand cmd;
    cmd.Value = 0x11;
    machine.SendCommand(cmd);
    cmd.Value = 0x7F;
    machine.SendCommand(cmd);  // resets
    Check(machine.ResetCountForTest() >= 1, "(precondition) the second command reset the machine");

    std::vector<LogicalChannel>& after = machine.LogicalChannels();
    Check(after[0].Mute, "Mute survives the reset");
    Check(after[1].Gain == 0.25f, "Gain survives the reset");
    Check(after[2].Pan == -0.5f && after[2].PanOverridden, "a UI-set Pan survives the reset");
}

namespace {

std::unique_ptr<IMachine> BuildRealDspiritMachine() {
    const char* romRootEnv = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRootEnv == nullptr || romRootEnv[0] == '\0') return nullptr;
    const auto path = FindDspiritDefinitionFile();
    if (!path.has_value()) return nullptr;
    const auto text = ReadFileText(*path);
    if (!text.has_value()) return nullptr;
    const XmlParseResult parsed = ParseGameDefinition(*text);
    if (parsed.Status != XmlParseStatus::Ok) return nullptr;
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
    if (!res.Ok) return nullptr;
    std::unique_ptr<IMachine> machine = DefaultMachineRegistry().Create(MachineId(def.Machine), VariantId(def.Variant), res.Resources);
    if (machine) machine->Initialize();
    return machine;
}

void SendRaw(IMachine& machine, std::uint8_t value) {
    MusicCommand cmd;
    cmd.Kind = MusicCommandKind::Raw;
    cmd.Value = value;
    machine.SendCommand(cmd);
}

// One second of output per entry: per-group RMS, plus a hash of every channel's samples (so "identical
// to a fresh machine" can be checked bit for bit).
struct SecondStats {
    double fmRms = 0.0;
    double wsgRms = 0.0;
    double dacRms = 0.0;
    std::uint64_t hash = 14695981039346656037ull;
};

std::vector<SecondStats> RenderSeconds(IMachine& machine, LogicalChannelSink& sink, int seconds) {
    constexpr int kBlocksPerSecond = 192000 / static_cast<int>(Mixer::kBlockSizeFrames);
    std::vector<SecondStats> out;
    for (int s = 0; s < seconds; ++s) {
        SecondStats st;
        double fmSq = 0.0, wsgSq = 0.0, dacSq = 0.0;
        std::size_t fmN = 0, wsgN = 0, dacN = 0;
        for (int b = 0; b < kBlocksPerSecond; ++b) {
            sink.ClearAll();
            machine.Render(EmuDuration{Mixer::kBlockSizeFrames}, &sink);
            for (std::uint32_t id = 0; id < DragonSpiritMachine::kChannelCount; ++id) {
                if (!sink.HasChannel(id)) continue;
                const LogicalChannelBuffer& buf = sink.ChannelBuffer(id);
                for (std::size_t i = 0; i < buf.Size(); ++i) {
                    const float v = buf[i];
                    std::uint32_t bits;
                    static_assert(sizeof(bits) == sizeof(v));
                    std::memcpy(&bits, &v, sizeof(bits));
                    st.hash = (st.hash ^ bits) * 1099511628211ull;
                    const double sq = static_cast<double>(v) * v;
                    if (id < DragonSpiritMachine::kCus30Base) { fmSq += sq; ++fmN; }
                    else if (id < DragonSpiritMachine::kDac0Base) { wsgSq += sq; ++wsgN; }
                    else { dacSq += sq; ++dacN; }
                }
            }
        }
        st.fmRms = fmN ? std::sqrt(fmSq / static_cast<double>(fmN)) : 0.0;
        st.wsgRms = wsgN ? std::sqrt(wsgSq / static_cast<double>(wsgN)) : 0.0;
        st.dacRms = dacN ? std::sqrt(dacSq / static_cast<double>(dacN)) : 0.0;
        out.push_back(st);
    }
    return out;
}

double MaxRmsDiff(const std::vector<SecondStats>& a, const std::vector<SecondStats>& b) {
    double d = 0.0;
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
        d = std::max({d, std::fabs(a[i].fmRms - b[i].fmRms), std::fabs(a[i].wsgRms - b[i].wsgRms),
                      std::fabs(a[i].dacRms - b[i].dacRms)});
    }
    return d;
}

bool SameHashes(const std::vector<SecondStats>& a, const std::vector<SecondStats>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].hash != b[i].hash) return false;
    }
    return true;
}

double SumField(const std::vector<SecondStats>& v, double SecondStats::*field) {
    double s = 0.0;
    for (const SecondStats& st : v) s += st.*field;
    return s;
}

}  // namespace

// Real-ROM checks (CHIPESTRA_ROM_ROOT only): each "switched" run must match a fresh machine playing the
// new command alone, so nothing of the earlier song (WSG, DAC voice, FM) is left over.
void DragonSpiritSwitchingSongsMatchesAFreshMachine() {
    std::printf("DragonSpiritSwitchingSongsMatchesAFreshMachine\n");
    if (BuildRealDspiritMachine() == nullptr) {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is not set (or dspirit.zip not found) - real-ROM switch checks not exercised\n");
        return;
    }
    constexpr int kSeconds = 8;

    auto freshRun = [&](std::uint8_t command) {
        auto m = BuildRealDspiritMachine();
        LogicalChannelSink sink;
        SendRaw(*m, command);
        return RenderSeconds(*m, sink, kSeconds);
    };
    auto switchedRun = [&](std::uint8_t first, int firstSeconds, std::uint8_t second) {
        auto m = BuildRealDspiritMachine();
        LogicalChannelSink sink;
        SendRaw(*m, first);
        RenderSeconds(*m, sink, firstSeconds);
        SendRaw(*m, second);
        return RenderSeconds(*m, sink, kSeconds);
    };

    // ENDING (0x7f): WSG-heavy. After 6s the WSG is playing; switch to OPENING (0x11).
    const auto fresh11 = freshRun(0x11);
    {
        const auto sw = switchedRun(0x7F, 6, 0x11);
        std::printf("  0x7f -> 0x11: fresh WSG sum=%.6f switched WSG sum=%.6f, max per-second RMS diff=%.9f\n",
                    SumField(fresh11, &SecondStats::wsgRms), SumField(sw, &SecondStats::wsgRms), MaxRmsDiff(fresh11, sw));
        Check(SameHashes(fresh11, sw), "ENDING (0x7f) -> OPENING (0x11): output is bit-identical to 0x11 alone (no WSG left over)");
    }

    // FINAL SCENE (0x15): its voice (DAC1) starts ~13.4s in (the music ~7s). Switch while the voice is sounding.
    {
        {
            auto m = BuildRealDspiritMachine();
            LogicalChannelSink sink;
            SendRaw(*m, 0x15);
            const auto first = RenderSeconds(*m, sink, 16);
            Check(first.back().dacRms > 0.0, "(precondition) 0x15's voice is sounding at the 16s switch point");
        }
        const auto sw = switchedRun(0x15, 16, 0x11);
        std::printf("  0x15 -> 0x11: fresh DAC sum=%.6f switched DAC sum=%.6f, max per-second RMS diff=%.9f\n",
                    SumField(fresh11, &SecondStats::dacRms), SumField(sw, &SecondStats::dacRms), MaxRmsDiff(fresh11, sw));
        Check(SameHashes(fresh11, sw), "FINAL SCENE (0x15) -> OPENING (0x11): output is bit-identical to 0x11 alone (no DAC voice left over)");
    }

    // Stop -> Play: the same 0x7f sent again after it has been playing (the WSG is sounding by 6s).
    {
        const auto fresh7f = freshRun(0x7F);
        const auto again = switchedRun(0x7F, 6, 0x7F);
        std::printf("  0x7f -> 0x7f (Stop->Play): fresh WSG sum=%.6f again WSG sum=%.6f, max per-second RMS diff=%.9f\n",
                    SumField(fresh7f, &SecondStats::wsgRms), SumField(again, &SecondStats::wsgRms), MaxRmsDiff(fresh7f, again));
        Check(SumField(fresh7f, &SecondStats::wsgRms) > 0.0, "(precondition) 0x7f alone drives the WSG");
        Check(SameHashes(fresh7f, again), "0x7f re-sent while playing restarts from the top: FM and WSG match 0x7f alone");
    }

    // STOP as a control track: 0x00 sent with no rendering after it; everything must then be silent.
    {
        auto m = BuildRealDspiritMachine();
        LogicalChannelSink sink;
        SendRaw(*m, 0x7F);
        RenderSeconds(*m, sink, 6);
        SendRaw(*m, 0x00);
        const auto after = RenderSeconds(*m, sink, 2);
        std::printf("  0x7f -> STOP(0x00): after-STOP RMS sums FM=%.9f WSG=%.9f DAC=%.9f\n", SumField(after, &SecondStats::fmRms),
                    SumField(after, &SecondStats::wsgRms), SumField(after, &SecondStats::dacRms));
        Check(SumField(after, &SecondStats::fmRms) == 0.0 && SumField(after, &SecondStats::wsgRms) == 0.0 &&
                  SumField(after, &SecondStats::dacRms) == 0.0,
              "STOP (0x00) sent without rendering silences FM, WSG and DAC");
    }
}

// --- WSG sound-effect requests (MusicCommandKind::Wsg, <track type="wsg">) -------------------------

namespace {

MusicCommand WsgCommand(std::uint8_t number) {
    MusicCommand cmd;
    cmd.Kind = MusicCommandKind::Wsg;
    cmd.Value = number;
    return cmd;
}

// The sound-test numbers xml/dspirit.xml lists as type="wsg": 0x00..0x24 except the six known to be silent.
std::vector<std::uint8_t> ListedWsgNumbers() {
    std::vector<std::uint8_t> out;
    for (std::uint8_t n = 0; n <= DragonSpiritMachine::kMaxWsgNumber; ++n) {
        if (n == 0x02 || n == 0x06 || n == 0x10 || n == 0x11 || n == 0x12 || n == 0x13) continue;
        out.push_back(n);
    }
    return out;
}

}  // namespace

void DragonSpiritWsgWritesTheRequestTableAndLeavesTriRamAlone() {
    std::printf("DragonSpiritWsgWritesTheRequestTableAndLeavesTriRamAlone\n");
    DragonSpiritMachine machine(BuildSyntheticSoundCpuRom());
    machine.Initialize();

    constexpr std::uint16_t kBase = DragonSpiritMachine::kWsgRequestTableOffset;
    constexpr std::uint16_t kEnd = kBase + DragonSpiritMachine::kWsgRequestTableSize;
    // Dirty the whole table and both neighbours so the clear and its edges are observable.
    Cus30DeviceAdapter& cus30 = machine.Cus30ForTest();
    for (std::uint16_t off = kBase - 1; off <= kEnd; ++off) cus30.Write8(off, 0x7F);
    const std::uint8_t mailbox0 = machine.ReadMemory(0x7100);
    const std::uint8_t mailbox1 = machine.ReadMemory(0x7101);

    machine.SendCommand(WsgCommand(0x00));
    Check(cus30.Read8(kBase) == 0x01, "Wsg 0x00 sets slot 0 (offset 0x240) to 0x01");
    bool restClear = true;
    for (std::uint16_t off = kBase + 1; off < kEnd; ++off) restClear = restClear && cus30.Read8(off) == 0x00;
    Check(restClear, "...and the rest of 0x240..0x2DF was cleared first");
    Check(cus30.Read8(kBase - 1) == 0x7F && cus30.Read8(kEnd) == 0x7F, "...bytes just outside the table are not touched");
    Check(machine.ReadMemory(0x7100) == mailbox0 && machine.ReadMemory(0x7101) == mailbox1,
          "TRIRAM 0x100/0x101 (the BGM mailbox) is not touched by a Wsg request");

    machine.SendCommand(WsgCommand(DragonSpiritMachine::kMaxWsgNumber));
    Check(cus30.Read8(kBase + 2u * DragonSpiritMachine::kMaxWsgNumber) == 0x01, "Wsg 0x24 sets slot 0x24 (offset 0x288)");
    Check(cus30.Read8(kBase) == 0x00, "...and the earlier request's slot was cleared (no replay)");
}

void DragonSpiritAcceptsWsgOnlyUpToTheLastNumber() {
    std::printf("DragonSpiritAcceptsWsgOnlyUpToTheLastNumber\n");
    DragonSpiritMachine machine(BuildSyntheticSoundCpuRom());
    machine.Initialize();
    Check(machine.AcceptsCommand(WsgCommand(0x00)), "Wsg 0x00 is accepted");
    Check(machine.AcceptsCommand(WsgCommand(0x24)), "Wsg 0x24 is accepted");
    Check(!machine.AcceptsCommand(WsgCommand(0x25)), "Wsg 0x25 is not accepted");
    Check(!machine.AcceptsCommand(WsgCommand(0xFF)), "Wsg 0xFF is not accepted");
    MusicCommand raw;
    raw.Kind = MusicCommandKind::Raw;
    raw.Value = 0x11;
    Check(machine.AcceptsCommand(raw), "Raw commands are accepted as before");
}

void DragonSpiritWsgAfterACommandResetsFirst() {
    std::printf("DragonSpiritWsgAfterACommandResetsFirst\n");
    DragonSpiritMachine machine(BuildSyntheticSoundCpuRom());
    machine.Initialize();
    MusicCommand raw;
    raw.Kind = MusicCommandKind::Raw;
    raw.Value = 0x11;
    machine.SendCommand(raw);
    const std::uint32_t n = machine.ResetCountForTest();
    machine.SendCommand(WsgCommand(0x01));
    Check(machine.ResetCountForTest() == n + 1, "a Wsg request after a BGM command resets the machine first");
    machine.SendCommand(raw);
    Check(machine.ResetCountForTest() == n + 2, "a BGM command after a Wsg request resets the machine first");
}

// Real ROM (CHIPESTRA_ROM_ROOT only): every WSG number xml/dspirit.xml lists must sound; a WSG request
// replaces a playing BGM; a BGM after a WSG request leaves nothing of it.
void DragonSpiritWsgRealRomPlaysEveryListedNumber() {
    std::printf("DragonSpiritWsgRealRomPlaysEveryListedNumber\n");
    if (BuildRealDspiritMachine() == nullptr) {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is not set (or dspirit.zip not found) - real-ROM WSG checks not exercised\n");
        return;
    }
    constexpr int kBlocksPerSecond = 192000 / static_cast<int>(Mixer::kBlockSizeFrames);
    constexpr int kSeconds = 14;
    constexpr float kAudible = 0.001f;

    std::printf("  WSG number: audible, last audible second, first audible second\n");
    for (std::uint8_t n : ListedWsgNumbers()) {
        auto m = BuildRealDspiritMachine();
        LogicalChannelSink sink;
        MusicCommand cmd = WsgCommand(n);
        m->SendCommand(cmd);
        double first = -1.0, last = -1.0;
        for (int b = 0; b < kBlocksPerSecond * kSeconds; ++b) {
            sink.ClearAll();
            m->Render(EmuDuration{Mixer::kBlockSizeFrames}, &sink);
            float peak = 0.0f;
            for (std::uint32_t v = 0; v < Cus30DeviceAdapter::kVoiceCount; ++v) {
                const std::uint32_t id = DragonSpiritMachine::kCus30Base + v;
                if (!sink.HasChannel(id)) continue;
                const LogicalChannelBuffer& buf = sink.ChannelBuffer(id);
                for (std::size_t i = 0; i < buf.Size(); ++i) peak = std::max(peak, std::fabs(buf[i]));
            }
            if (peak >= kAudible) {
                const double t = static_cast<double>(b + 1) / kBlocksPerSecond;
                if (first < 0.0) first = t - 1.0 / kBlocksPerSecond;
                last = t;
            }
        }
        std::printf("  WSG 0x%02X: %s last=%.2fs first=%.2fs\n", n, last >= 0.0 ? "yes" : "NO", last, first);
        char label[96];
        std::snprintf(label, sizeof(label), "WSG 0x%02X sounds on the CUS30 (real ROM)", n);
        Check(last >= 0.0, label);
    }

    // BGM playing -> WSG request: the BGM stops, only the WSG sounds.
    {
        auto m = BuildRealDspiritMachine();
        LogicalChannelSink sink;
        SendRaw(*m, 0x11);
        const auto bgm = RenderSeconds(*m, sink, 4);
        m->SendCommand(WsgCommand(0x00));
        const auto after = RenderSeconds(*m, sink, 4);
        Check(SumField(bgm, &SecondStats::fmRms) > 0.0, "(precondition) the BGM 0x11 is sounding on the FM chip before the switch");
        Check(SumField(after, &SecondStats::fmRms) == 0.0, "BGM -> WSG request: the BGM (FM) is gone");
        Check(SumField(after, &SecondStats::wsgRms) > 0.0, "BGM -> WSG request: the WSG effect sounds");
    }

    // WSG request -> BGM: nothing of the WSG is left, the BGM is exactly the BGM alone.
    {
        auto fresh = BuildRealDspiritMachine();
        LogicalChannelSink freshSink;
        SendRaw(*fresh, 0x11);
        const auto alone = RenderSeconds(*fresh, freshSink, 6);

        auto m = BuildRealDspiritMachine();
        LogicalChannelSink sink;
        m->SendCommand(WsgCommand(0x00));
        RenderSeconds(*m, sink, 2);
        SendRaw(*m, 0x11);
        const auto after = RenderSeconds(*m, sink, 6);
        Check(SameHashes(alone, after), "WSG request -> BGM 0x11: output is bit-identical to 0x11 alone (no WSG left over)");
    }
}
