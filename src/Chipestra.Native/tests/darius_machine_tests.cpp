// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// DariusMachine native tests. All ROM bytes below are hand-written synthetic
// programs (no real Darius ROM data).

#include "darius_machine_tests.h"

#include "default_machine_catalog.h"
#include "devices/taito/darius/darius_machine.h"
#include "machine_registry.h"
#include "test_support.h"
#include "chipestra_abi.h"

#include <cstdint>
#include <vector>

namespace {

// audiocpu boot code shared by the two-Z80-coordination test: writes 0x77
// to the adpcm_command_w latch (0xD400) then spins - proves audiocpu's own
// memory-mapped write reaches the shared m_adpcm_command_ latch.
std::vector<std::uint8_t> BuildAudiocpuAdpcmLatchWriterRom() {
    std::vector<std::uint8_t> rom(0x100, 0x00);
    std::size_t i = 0;
    rom[i++] = 0x3E;  // LD A,0x77
    rom[i++] = 0x77;
    rom[i++] = 0x32;  // LD (0xD400),A
    rom[i++] = 0x00;
    rom[i++] = 0xD4;
    rom[i++] = 0xC3;  // JP 0x0005 (self-loop)
    rom[i++] = 0x05;
    rom[i++] = 0x00;
    return rom;
}

// adpcm Z80 busy-poll code: reads I/O port 0x00 (adpcm_command_r) until it
// sees 0x77, then jumps to a fixed "success trap" (self-loop at 0x0020) -
// proves the adpcm Z80's own I/O read reaches that SAME shared latch,
// completing the cross-CPU coordination.
std::vector<std::uint8_t> BuildAdpcmLatchWaiterRom() {
    std::vector<std::uint8_t> rom(0x30, 0x00);
    std::size_t i = 0;
    rom[i++] = 0xDB;  // IN A,(0x00)
    rom[i++] = 0x00;
    rom[i++] = 0xFE;  // CP 0x77
    rom[i++] = 0x77;
    rom[i++] = 0x20;  // JR NZ,-6 (back to offset 0)
    rom[i++] = 0xFA;
    rom[i++] = 0xC3;  // JP 0x0020
    rom[i++] = 0x20;
    rom[i++] = 0x00;
    rom[0x20] = 0xC3;  // JP 0x0020 (self-loop, the success trap)
    rom[0x21] = 0x20;
    rom[0x22] = 0x00;
    return rom;
}

// audiocpu boot code for the command-injection test: enables the PC060HA
// slave's own NMI (mode 6, matching DariusCommandInterfaceSlaveNmiGated
// ByEnableFlag's own SlavePortW(0x06)/SlaveCommW(0x00) sequence -
// darius_command_interface_tests.cpp), then spins waiting for the NMI. The
// NMI handler at the real Z80's fixed 0x0066 vector reads both nibbles
// (SlaveCommR() x2 via 0xB001, after selecting mode 0 via 0xB000),
// reconstructs the original byte, and stores it to RAM (0x8000) so the test
// can observe it.
//
// Nibble order: mode-0 read is the LOW nibble, mode-1 read is the HIGH nibble
// (confirmed against a real-MAME trace of Darius's NMI handler).
std::vector<std::uint8_t> BuildAudiocpuCommandInjectionRom() {
    std::vector<std::uint8_t> rom(0x100, 0x00);
    std::size_t i = 0;
    rom[i++] = 0x31;  // LD SP,7F00h
    rom[i++] = 0x00;
    rom[i++] = 0x7F;
    rom[i++] = 0x3E;  // LD A,0x06 (PC060HA mode 6 = slave NMI enable)
    rom[i++] = 0x06;
    rom[i++] = 0x32;  // LD (0xB000),A - slave_port_w(6)
    rom[i++] = 0x00;
    rom[i++] = 0xB0;
    rom[i++] = 0xAF;  // XOR A
    rom[i++] = 0x32;  // LD (0xB001),A - slave_comm_w(0), the actual enable trigger
    rom[i++] = 0x01;
    rom[i++] = 0xB0;
    rom[i++] = 0xC3;  // JP 0x000C (self-loop)
    rom[i++] = 0x0C;
    rom[i++] = 0x00;

    // NMI vector, fixed at 0x0066 on real Z80 hardware.
    std::size_t nmi = 0x0066;
    rom[nmi++] = 0x3E;  // LD A,0x00
    rom[nmi++] = 0x00;
    rom[nmi++] = 0x32;  // LD (0xB000),A - slave_port_w(0), select mode 0
    rom[nmi++] = 0x00;
    rom[nmi++] = 0xB0;
    rom[nmi++] = 0x3A;  // LD A,(0xB001) - SlaveCommR(), LOW nibble (mode 0)
    rom[nmi++] = 0x01;
    rom[nmi++] = 0xB0;
    rom[nmi++] = 0x4F;  // LD C,A
    rom[nmi++] = 0x3A;  // LD A,(0xB001) - SlaveCommR(), HIGH nibble (mode 1)
    rom[nmi++] = 0x01;
    rom[nmi++] = 0xB0;
    rom[nmi++] = 0x47;  // LD B,A
    rom[nmi++] = 0x78;  // LD A,B (the HIGH nibble)
    rom[nmi++] = 0x87;  // ADD A,A  (x4: high nibble -> high 4 bits of A)
    rom[nmi++] = 0x87;
    rom[nmi++] = 0x87;
    rom[nmi++] = 0x87;
    rom[nmi++] = 0xB1;  // OR C - combine with the low nibble
    rom[nmi++] = 0x32;  // LD (0x8000),A - store the reconstructed byte to RAM
    rom[nmi++] = 0x00;
    rom[nmi++] = 0x80;
    rom[nmi++] = 0xED;  // RETN
    rom[nmi++] = 0x45;

    return rom;
}

}  // namespace

void DariusMachineConstructsAndRegistersThirteenChannels() {
    DariusMachine machine({}, {});
    machine.Initialize();

    const std::vector<LogicalChannel>& channels = machine.LogicalChannels();
    Check(channels.size() == 13, "channels: DariusMachine exposes 13 LogicalChannels (6+6 YM2203, 1 MSM5205)");
    if (channels.size() == 13) {
        Check(channels[0].DeviceName == "YM2203" && channels[0].ChannelName == "YM1 FM1", "channels: id 0 is YM1 FM1");
        Check(channels[3].ChannelName == "YM1 PSG1", "channels: id 3 is YM1 PSG1");
        Check(channels[6].ChannelName == "YM2 FM1", "channels: id 6 is YM2 FM1");
        Check(channels[9].ChannelName == "YM2 PSG1", "channels: id 9 is YM2 PSG1");
        Check(channels[12].DeviceName == "MSM5205" && channels[12].ChannelName == "ADPCM",
              "channels: id 12 is MSM5205 ADPCM");
    }
}

void DariusMachineTwoZ80sCoordinateViaSharedAdpcmLatch() {
    DariusMachine machine(BuildAudiocpuAdpcmLatchWriterRom(), BuildAdpcmLatchWaiterRom());
    machine.Initialize();

    auto snap = machine.AdpcmZ80Snapshot();
    Check(snap.pc == 0x0020,
          "2xZ80 coordination: adpcm Z80's busy-poll observed audiocpu's memory-mapped write (0xD400) via its own "
          "I/O port 0x00 read and reached the success trap");
}

void DariusMachineCommandInjectionDeliversViaPc060haToAudiocpuNmi() {
    DariusMachine machine(BuildAudiocpuCommandInjectionRom(), {});
    machine.Initialize();

    MusicCommand command;
    command.Kind = MusicCommandKind::Raw;
    command.Value = 0xA5;
    machine.SendCommand(command);

    machine.Render(EmuDuration{256}, nullptr);

    auto snap = machine.AudioCpuSnapshot();
    Check(machine.ReadAudioCpuMemory(0x8000) == 0xA5,
          "command injection: audiocpu's NMI handler reconstructed the 2-nibble PC060HA message and stored it to RAM");
    Check(snap.nmiServiceCount >= 1, "command injection: audiocpu actually serviced an NMI");
    Check(snap.pc == 0x000C, "command injection: RETN resumed the self-loop after the handler returned");
}

void DariusMachineRepeatedCommandsReAssertNmi() {
    DariusMachine machine(BuildAudiocpuCommandInjectionRom(), {});
    machine.Initialize();

    MusicCommand first;
    first.Kind = MusicCommandKind::Raw;
    first.Value = 0x11;
    machine.SendCommand(first);
    machine.Render(EmuDuration{256}, nullptr);
    Check(machine.ReadAudioCpuMemory(0x8000) == 0x11, "re-assert: first command's byte observed");

    // DariusCommandInterface's NmiHandler only fires on an asserted/deasserted transition
    // (edge-based), so a second SendCommand must produce a genuine new edge,
    // not be swallowed because the line was already asserted or the first
    // command's NMI hadn't been fully serviced.
    MusicCommand second;
    second.Kind = MusicCommandKind::Raw;
    second.Value = 0x99;
    machine.SendCommand(second);
    machine.Render(EmuDuration{256}, nullptr);
    Check(machine.ReadAudioCpuMemory(0x8000) == 0x99, "re-assert: second command's byte observed after the first was serviced");
    Check(machine.AudioCpuSnapshot().nmiServiceCount >= 2, "re-assert: both commands caused NMI services");
}

// Song switching (DariusMachine::SendCommand()): when a song is already running, the driver's own STOP (0x00)
// is delivered first (and a silent gap rendered) before the requested command.
void DariusMachineFirstCommandAfterBootSendsNoStop() {
    DariusMachine machine(BuildAudiocpuCommandInjectionRom(), {});
    machine.Initialize();
    Check(!machine.SongActiveForTest(), "first command: no song is active right after Initialize()");

    const std::uint64_t before = machine.AudioCpuSnapshot().nmiServiceCount;
    MusicCommand command;
    command.Kind = MusicCommandKind::Raw;
    command.Value = 0x11;
    machine.SendCommand(command);
    machine.Render(EmuDuration{256}, nullptr);

    Check(machine.AudioCpuSnapshot().nmiServiceCount - before == 1,
          "first command: exactly one byte (the command itself) reached audiocpu - no STOP before the first command");
    Check(machine.ReadAudioCpuMemory(0x8000) == 0x11, "first command: the delivered byte is the requested command");
    Check(machine.SongActiveForTest(), "first command: a non-STOP command marks a song active");
}

void DariusMachineSwitchSendsStopThenTheCommand() {
    DariusMachine machine(BuildAudiocpuCommandInjectionRom(), {});
    machine.Initialize();

    MusicCommand first;
    first.Kind = MusicCommandKind::Raw;
    first.Value = 0x28;
    machine.SendCommand(first);
    machine.Render(EmuDuration{256}, nullptr);

    const std::uint64_t before = machine.AudioCpuSnapshot().nmiServiceCount;
    MusicCommand second;
    second.Kind = MusicCommandKind::Raw;
    second.Value = 0x07;
    machine.SendCommand(second);
    machine.Render(EmuDuration{256}, nullptr);

    Check(machine.AudioCpuSnapshot().nmiServiceCount - before == 2,
          "switch: STOP (0x00) and then the requested command - two bytes - reached audiocpu");
    Check(machine.ReadAudioCpuMemory(0x8000) == 0x07, "switch: the requested command is the last byte delivered");
    Check(machine.SongActiveForTest(), "switch: the new song is active");

    // A STOP itself is delivered as-is (no second STOP in front of it) and ends the song.
    const std::uint64_t beforeStop = machine.AudioCpuSnapshot().nmiServiceCount;
    MusicCommand stop;
    stop.Kind = MusicCommandKind::Raw;
    stop.Value = 0x00;
    machine.SendCommand(stop);
    machine.Render(EmuDuration{256}, nullptr);
    Check(machine.AudioCpuSnapshot().nmiServiceCount - beforeStop == 1, "switch: a STOP command is delivered once, with no extra STOP before it");
    // A STOP still counts as "something was sent": Player does not render after a type="control" track, so the
    // 0x00 may still be unprocessed when the next command arrives - that command must bring its own STOP + gap.
    Check(machine.SongActiveForTest(), "switch: after a STOP the machine still counts as active (only Reset() clears it)");

    const std::uint64_t beforeAfterStop = machine.AudioCpuSnapshot().nmiServiceCount;
    MusicCommand third;
    third.Kind = MusicCommandKind::Raw;
    third.Value = 0x21;
    machine.SendCommand(third);
    machine.Render(EmuDuration{256}, nullptr);
    Check(machine.AudioCpuSnapshot().nmiServiceCount - beforeAfterStop == 2,
          "switch: a command sent after a STOP is again preceded by its own STOP (two bytes delivered)");
}

void DariusMachineResetForgetsTheActiveSong() {
    DariusMachine machine(BuildAudiocpuCommandInjectionRom(), {});
    machine.Initialize();
    MusicCommand command;
    command.Kind = MusicCommandKind::Raw;
    command.Value = 0x28;
    machine.SendCommand(command);
    Check(machine.SongActiveForTest(), "reset: a song is active before Reset()");

    machine.Reset();
    Check(!machine.SongActiveForTest(), "reset: Reset() clears the active-song flag (the next command needs no STOP)");
}

void DariusMachineRegisteredInDefaultCatalog() {
    const MachineDefinition* def = DefaultMachineRegistry().Find(MachineId("TAITO_DARIUS"));
    Check(def != nullptr, "catalog: TAITO_DARIUS is registered in the default MachineRegistry");
    if (def != nullptr) {
        Check(def->Category == MachineCategory::Arcade, "catalog: TAITO_DARIUS Category == Arcade");
        Check(def->Manufacturer == "Taito", "catalog: TAITO_DARIUS Manufacturer == Taito");
        Check(!def->Variants.empty(), "catalog: TAITO_DARIUS has at least one Variant");
    }
}

// OnPanOverrideCleared() must re-derive Pan: ApplyFm0/ApplyFm1/ApplyPsg0/ApplyPsg1/ApplyDa
// only run from their own register-write handlers, never every Render() block,
// so clearing PanOverridden alone would leave a channel's Pan stuck at the
// last UI override until the driver happened to touch that register again.
void DariusMachineOnPanOverrideClearedResyncsPan() {
    DariusMachine machine({}, {});
    machine.Initialize();

    std::vector<LogicalChannel>& channels = machine.LogicalChannels();
    Check(channels.size() > 0, "OnPanOverrideCleared: channels exist");
    if (channels.empty()) return;

    // fm0_pan_'s power-on default is 0x00, and DariusPanFromByte(0x00) is
    // +1.0 (real hardware's own "data 0x00:right" convention) - Initialize()
    // already ran ApplyFm0() once, so this is what channel 0 (YM1 FM1)
    // should already read.
    const float autoSyncedPan = channels[0].Pan;
    Check(autoSyncedPan == 1.0f, "OnPanOverrideCleared: FM0's auto-synced Pan starts at +1.0 (power-on default)");

    // Simulate a UI override (chipestra_set_logical_channel_pan's own effect).
    channels[0].Pan = -0.5f;
    channels[0].PanOverridden = true;
    Check(channels[0].Pan == -0.5f, "OnPanOverrideCleared: the override itself took effect");

    // Simulate chipestra_clear_logical_channel_pan_override's own sequence: clear
    // the flag, then call OnPanOverrideCleared.
    channels[0].PanOverridden = false;
    machine.OnPanOverrideCleared(0);

    Check(channels[0].Pan == autoSyncedPan,
          "OnPanOverrideCleared: FM0's Pan is immediately re-synced back to the driver's real value, not left "
          "stuck at the old override");
}
