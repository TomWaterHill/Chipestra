// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// VaporTrailMachine native tests. See vapor_trail_machine_tests.h for why a
// NOP-filled synthetic ROM is sufficient here (every test observes wiring through the Machine's own test
// accessors, not by running a hand-crafted driver's interrupt handler).

#include "vapor_trail_machine_tests.h"

#include "default_machine_catalog.h"
#include "devices/dataeast/vaportra/vapor_trail_machine.h"
#include "machine_registry.h"
#include "test_support.h"
#include "chipestra_abi.h"

#include <cstdint>
#include <vector>

namespace {

// 0xEA = H6280/65C02 NOP (2 clocks). A vector that reads 0xEAEA (unset, since the whole ROM is NOP-filled)
// still lands back inside the same NOP-filled 8KB physical page 0 window mmr[7]==0 maps at reset, so
// the reset/IRQ1/IRQ2/NMI vectors unset and simply let the H6280 idle through NOPs whenever Execute() runs.
std::vector<std::uint8_t> BuildSyntheticSoundCpuRom() {
    return std::vector<std::uint8_t>(0x10000, 0xEA);
}

void SendCommandByte(VaporTrailMachine& machine, std::uint8_t value) {
    MusicCommand cmd;
    cmd.Value = value;
    machine.SendCommand(cmd);
}

}  // namespace

void VaporTrailMachineRegisteredInDefaultCatalog() {
    const MachineDefinition* def = DefaultMachineRegistry().Find(MachineId("DATAEAST_VAPORTRAIL"));
    Check(def != nullptr, "catalog: DATAEAST_VAPORTRAIL is registered in the default MachineRegistry");
    if (def != nullptr) {
        Check(def->Category == MachineCategory::Arcade, "catalog: DATAEAST_VAPORTRAIL Category == Arcade");
        Check(def->Manufacturer == "Data East", "catalog: DATAEAST_VAPORTRAIL Manufacturer == Data East");
        Check(!def->Variants.empty(), "catalog: DATAEAST_VAPORTRAIL has at least one Variant");
    }
}

void VaporTrailMachineExposes22LogicalChannels() {
    VaporTrailMachine machine(BuildSyntheticSoundCpuRom());
    machine.Initialize();
    const std::vector<LogicalChannel>& channels = machine.LogicalChannels();
    Check(channels.size() == 22, "VaporTrailMachine exposes 22 LogicalChannels (YM2203 6 + YM2151 8 + OKI1 4 + OKI2 4)");
    Check(channels[0].DeviceName == "YM2203" && channels[0].ChannelName == "FM1", "channel 0 is YM2203 FM1");
    Check(channels[6].DeviceName == "YM2151", "channel 6 is the first YM2151 channel");
    Check(channels[14].DeviceName == "OKIM6295 #1", "channel 14 is the first OKI1 voice");
    Check(channels[18].DeviceName == "OKIM6295 #2", "channel 18 is the first OKI2 voice");
    Check(channels[7].NativeSampleRate == 3'580'000 / 64, "YM2151 NativeSampleRate uses Vapor Trail's real clock (3,580,000Hz), not X-Board's 4MHz placeholder");
    Check(channels[0].NativeSampleRate == 4'027'500 / 12, "YM2203 NativeSampleRate uses Vapor Trail's real clock (4,027,500Hz), not Darius's 4MHz placeholder");
    for (const LogicalChannel& channel : channels) {
        Check(channel.Pan == 0.0f, "every channel starts hard-centered (real hardware is mono)");
    }
}

void VaporTrailMachineBootHandshakeLeavesNoSongActive() {
    VaporTrailMachine machine(BuildSyntheticSoundCpuRom());
    machine.Initialize();
    Check(!machine.SongActiveForTest(),
          "Initialize()'s own boot handshake ends with the driver's STOP (0x00), so no song is left active "
          "before the user's first real SendCommand()");

    machine.Reset();
    Check(!machine.SongActiveForTest(), "Reset() repeats the same boot handshake and leaves the same post-STOP state");
}

void VaporTrailMachineSendCommandAssertsIrq1AndLatchesByte() {
    VaporTrailMachine machine(BuildSyntheticSoundCpuRom());
    machine.Initialize();
    // The boot handshake (SendMasterBootSequence()) ends by SENDING the driver's own STOP, asserting
    // IRQ1 - it does NOT itself read the latch (that is real driver code's job, once real ROM is loaded;
    // this test's synthetic NOP-filled ROM never reads 0x140000, so nothing consumes it here). Draining it
    // first keeps this test focused on SendCommand()'s own IRQ1 behavior rather than the boot sequence's.
    Check(machine.H6280Snapshot().irq1, "the boot handshake's own trailing STOP leaves IRQ1 asserted (nothing reads it with this synthetic ROM)");
    Check(machine.ReadMemory(0x140000) == 0x00, "draining the boot handshake's own STOP byte");
    Check(!machine.H6280Snapshot().irq1, "draining the latch clears IRQ1");

    SendCommandByte(machine, 0x2A);
    Check(machine.H6280Snapshot().irq1, "SendCommand() asserts IRQ1 (VaporTrailCommandInterface::Send)");
    Check(machine.ReadMemory(0x140000) == 0x2A, "the latched byte is readable at physical 0x140000");
}

void VaporTrailMachineReadingLatchClearsIrq1() {
    VaporTrailMachine machine(BuildSyntheticSoundCpuRom());
    machine.Initialize();
    SendCommandByte(machine, 0x77);
    Check(machine.H6280Snapshot().irq1, "IRQ1 asserted after SendCommand()");

    const std::uint8_t latched = machine.ReadMemory(0x140000);
    Check(latched == 0x77, "reading 0x140000 returns the latched command byte");
    Check(!machine.H6280Snapshot().irq1, "reading the latch clears IRQ1 (matches real generic_latch_8's data_pending)");

    // Both mapped bytes read back the same value (mirror confirmed on real MAME).
    SendCommandByte(machine, 0x55);
    Check(machine.ReadMemory(0x140001) == 0x55, "0x140001 mirrors the same latched byte as 0x140000");
}

void VaporTrailMachineYm2151IrqReachesH6280Irq2() {
    VaporTrailMachine machine(BuildSyntheticSoundCpuRom());
    machine.Initialize();
    Check(!machine.H6280Snapshot().irq2, "IRQ2 starts clear");

    // TA = 1000 -> period (1024-1000)*64 = 1536 input clocks (see Ym2151TimerAFiresAfterProgrammedPeriod in
    // ym2151_adapter_tests.cpp) - at Vapor Trail's real YM2151 clock (3,580,000Hz) this
    // is well under 1ms, so a single 256-canonical-tick Render() block is more than enough.
    Ym2151DeviceAdapter& ym2151 = machine.Ym2151ForTest();
    ym2151.WriteAddress(0x10);
    ym2151.WriteData(1000 >> 2);
    ym2151.WriteAddress(0x11);
    ym2151.WriteData(1000 & 0x03);
    ym2151.WriteAddress(0x14);
    ym2151.WriteData(0x05);  // bit0 load timer A, bit2 enable timer A IRQ

    LogicalChannelSink sink;
    machine.Render(EmuDuration{256}, &sink);
    Check(machine.H6280Snapshot().irq2, "YM2151's Timer A IRQ reaches the H6280 as IRQ2 (Initialize()'s own wiring)");
}

void VaporTrailMachineDevicesReachableViaMemoryMap() {
    VaporTrailMachine machine(BuildSyntheticSoundCpuRom());
    machine.Initialize();

    // YM2203 (0x100000/0x100001): write to an SSG register (address < 0x10, real YM2203 lets these be read
    // back - Ym2203DeviceAdapter::ReadData()'s own header comment) through the memory map, then confirm via
    // the device's own test accessor that the SAME instance actually received the write (not silently
    // dropped or routed to the wrong device).
    machine.WriteMemory(0x100000, 0x00);  // select SSG tone-period-fine (channel A)
    machine.WriteMemory(0x100001, 0x2A);
    Check(machine.Ym2203ForTest().ReadData() == 0x2A, "a YM2203 SSG register write via the memory map reaches the real device instance");

    // OKI1/OKI2 (single command/status address each): the status register's bits 4-7 are always 1 on real
    // hardware regardless of ROM/command content (Okim6295DeviceAdapter::Read()'s own header comment,
    // "naname expects bits 4-7 to be 1") - reading it through the memory map is a ROM-content-independent
    // way to confirm the read path reaches the real chip, not a genuinely unmapped/default value.
    Check((machine.ReadMemory(0x120000) & 0xF0) == 0xF0, "reading OKI1's status via the memory map reaches Okim6295DeviceAdapter::Read()");
    Check((machine.ReadMemory(0x130000) & 0xF0) == 0xF0, "reading OKI2's status via the memory map reaches Okim6295DeviceAdapter::Read()");
    // The 2-byte command write path itself must not crash the Machine even with no ROM behind it (this
    // synthetic test passes no oki1_rom/oki2_rom) - Okim6295DeviceAdapter's own "missing ROM plays silence,
    // never crashes" convention (its header comment) applies through the memory map exactly as directly.
    machine.WriteMemory(0x120000, 0x81);  // bit7 set: first half of a 2-byte phrase-start command
    machine.WriteMemory(0x120000, 0x10);  // second half: voice 1, attenuation 0
    machine.WriteMemory(0x130000, 0x81);
    machine.WriteMemory(0x130000, 0x10);

    // ROM (ignores writes) and RAM (accepts them) - VaporTrailMemoryMap's own established behavior.
    machine.WriteMemory(0x0000, 0xFF);
    Check(machine.ReadMemory(0x0000) == 0xEA, "a write into the ROM window is silently dropped (real hardware ignores it too)");
    machine.WriteMemory(0x1F0000, 0x42);
    Check(machine.ReadMemory(0x1F0000) == 0x42, "RAM (0x1F0000-0x1F1FFF) accepts a write via the memory map and reads it back");

    // Genuinely unmapped physical address - 0x00, not 0xFF (unmapped bus reads are 0x00).
    Check(machine.ReadMemory(0x180000) == 0x00, "a genuinely unmapped physical address reads 0x00, not 0xFF");
}

void VaporTrailMachineRenderIsDeterministicAcrossInstances() {
    auto runSequence = [](VaporTrailMachine& machine) -> std::vector<float> {
        machine.Initialize();
        SendCommandByte(machine, 0x2A);
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

    VaporTrailMachine a(BuildSyntheticSoundCpuRom());
    VaporTrailMachine b(BuildSyntheticSoundCpuRom());
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

void VaporTrailMachineSendCommandGatesSongSwitchWithStop() {
    VaporTrailMachine machine(BuildSyntheticSoundCpuRom());
    machine.Initialize();
    Check(!machine.SongActiveForTest(), "no song active right after boot");

    SendCommandByte(machine, 0x91);
    Check(machine.SongActiveForTest(), "a non-STOP command marks a song active");

    // Re-entrancy safety: calling SendCommand() again while a song is already active must not
    // crash/hang, and must still end with the LATEST command latched (the internal STOP-then-command gate
    // is transparent to the caller).
    SendCommandByte(machine, 0x92);
    Check(machine.SongActiveForTest(), "song stays active after switching to a different non-STOP command");
    Check(machine.ReadMemory(0x140000) == 0x92, "the latch ends up holding the LATEST command, not the internal STOP");

    SendCommandByte(machine, 0x00);
    Check(!machine.SongActiveForTest(), "sending the driver's own STOP (0x00) clears the active-song state");
}

void VaporTrailMachineSoundDeviceSyncTracksCumulativeCyclesAcrossBusAccesses() {
    // Regression guard: SyncSoundDevices() must not add block_base_cycles_ to a mark that bus-access callers
    // have ALREADY made cumulative; otherwise, once a few blocks have elapsed, every chip-register access
    // pushes the YM2151/YM2203
    // "cycles issued" counters to ~2x the true value and the chips stop advancing. A NOP-only ROM
    // never touches a chip window, so it cannot see this; this synthetic driver writes to the YM2151 address
    // port in a tight loop (one bus-access-triggered sync per iteration), then checks that, after every
    // block, the cycles issued to the chips equal the cumulative H6280 cycles executed converted to each
    // chip's own clock - neither ahead of it (double-count) nor behind it (stall).
    std::vector<std::uint8_t> rom = BuildSyntheticSoundCpuRom();
    rom[0x1FFE] = 0x00;  // reset vector -> 0xE000 (physical 0x0000 through the reset-time MPR7=0 mapping)
    rom[0x1FFF] = 0xE0;
    const std::uint8_t program[] = {
        0xA9, 0x88,        // LDA #$88          (bank 0x88 -> physical 0x110000, the YM2151 window)
        0x53, 0x02,        // TAM #$02          (MPR1 = A, so virtual 0x2000-0x3FFF is that bank)
        0x8D, 0x00, 0x20,  // loop: STA $2000   (writes the YM2151 address port - one device bus access)
        0x80, 0xFB,        // BRA loop
    };
    for (std::size_t i = 0; i < sizeof(program); ++i) rom[i] = program[i];

    VaporTrailMachine machine(rom);
    machine.Initialize();

    auto expectedIssued = [](std::uint64_t cumulativeCycles, std::uint64_t chipClockHz) {
        return static_cast<double>(cumulativeCycles) * static_cast<double>(chipClockHz) /
               static_cast<double>(VaporTrailMachine::kH6280ClockRate.hz);
    };

    LogicalChannelSink sink;
    bool ym2151InStep = true;
    bool ym2203InStep = true;
    bool advancedPastWarmup = false;
    const std::uint64_t cyclesAfterInit = machine.CumulativeCyclesExecutedForTest();
    for (int block = 0; block < 3000; ++block) {
        machine.Render(EmuDuration{256}, &sink);
        const std::uint64_t cumulative = machine.CumulativeCyclesExecutedForTest();
        const double want2151 = expectedIssued(cumulative, VaporTrailMachine::kYm2151ClockRate.hz);
        const double want2203 = expectedIssued(cumulative, VaporTrailMachine::kYm2203ClockRate.hz);
        const double got2151 = static_cast<double>(machine.Ym2151CyclesIssuedForTest());
        const double got2203 = static_cast<double>(machine.Ym2203CyclesIssuedForTest());
        if (got2151 < want2151 - 2.0 || got2151 > want2151 + 2.0) ym2151InStep = false;
        if (got2203 < want2203 - 2.0 || got2203 > want2203 + 2.0) ym2203InStep = false;
    }
    advancedPastWarmup = machine.CumulativeCyclesExecutedForTest() > cyclesAfterInit + 1'000'000;
    Check(advancedPastWarmup, "sync regression: the synthetic driver run advanced the H6280 well past the boot warm-up");
    Check(ym2151InStep,
          "sync regression: YM2151 cycles issued equal cumulative H6280 cycles converted to the YM2151 clock after "
          "every block, even though the driver hits the chip window constantly (no 2x double-count, no stall)");
    Check(ym2203InStep, "sync regression: YM2203 cycles issued track the cumulative H6280 cycles the same way");
}
