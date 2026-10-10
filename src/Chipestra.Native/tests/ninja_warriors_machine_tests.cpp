// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// NinjaWarriorsMachine native tests. All ROM bytes below are
// hand-written synthetic programs (no real Ninja Warriors ROM data).

#include "ninja_warriors_machine_tests.h"

#include "default_machine_catalog.h"
#include "devices/taito/ninjaw/ninja_warriors_machine.h"
#include "machine_registry.h"
#include "test_support.h"
#include "chipestra_abi.h"

#include <cstdint>
#include <vector>

namespace {

// audiocpu boot code for the command-injection test: enables the TC0140SYT
// slave's own NMI (mode 6, matching DariusCommandInterfaceSlaveNmiGated
// ByEnableFlag's own SlavePortW(0x06)/SlaveCommW(0x00) sequence - the
// protocol is identical), then
// spins waiting for the NMI. The NMI handler at the real Z80's fixed 0x0066
// vector reads both nibbles (SlaveCommR() x2 via 0xE201, after selecting
// mode 0 via 0xE200), reconstructs the original byte, and stores it to RAM
// (0xC000 - within NinjaWarriorsSoundMemoryMap's own 0xC000-0xDFFF window,
// unlike Darius's 0x8000) so the test can observe it.
//
// Nibble order: mode-0 read is
// the LOW nibble, mode-1 read is the HIGH nibble (confirmed
// against a real-MAME trace of the natural 0xEF handshake).
std::vector<std::uint8_t> BuildAudiocpuCommandInjectionRom() {
    std::vector<std::uint8_t> rom(0x100, 0x00);
    std::size_t i = 0;
    rom[i++] = 0x31;  // LD SP,DF00h (within the real 0xC000-0xDFFF RAM window)
    rom[i++] = 0x00;
    rom[i++] = 0xDF;
    rom[i++] = 0x3E;  // LD A,0x06 (TC0140SYT mode 6 = slave NMI enable)
    rom[i++] = 0x06;
    rom[i++] = 0x32;  // LD (0xE200),A - slave_port_w(6)
    rom[i++] = 0x00;
    rom[i++] = 0xE2;
    rom[i++] = 0xAF;  // XOR A
    rom[i++] = 0x32;  // LD (0xE201),A - slave_comm_w(0), the actual enable trigger
    rom[i++] = 0x01;
    rom[i++] = 0xE2;
    rom[i++] = 0xC3;  // JP 0x000C (self-loop)
    rom[i++] = 0x0C;
    rom[i++] = 0x00;

    // NMI vector, fixed at 0x0066 on real Z80 hardware.
    std::size_t nmi = 0x0066;
    rom[nmi++] = 0x3E;  // LD A,0x00
    rom[nmi++] = 0x00;
    rom[nmi++] = 0x32;  // LD (0xE200),A - slave_port_w(0), select mode 0
    rom[nmi++] = 0x00;
    rom[nmi++] = 0xE2;
    rom[nmi++] = 0x3A;  // LD A,(0xE201) - SlaveCommR(), LOW nibble (mode 0)
    rom[nmi++] = 0x01;
    rom[nmi++] = 0xE2;
    rom[nmi++] = 0x4F;  // LD C,A
    rom[nmi++] = 0x3A;  // LD A,(0xE201) - SlaveCommR(), HIGH nibble (mode 1)
    rom[nmi++] = 0x01;
    rom[nmi++] = 0xE2;
    rom[nmi++] = 0x47;  // LD B,A
    rom[nmi++] = 0x78;  // LD A,B (the HIGH nibble)
    rom[nmi++] = 0x87;  // ADD A,A  (x4: high nibble -> high 4 bits of A)
    rom[nmi++] = 0x87;
    rom[nmi++] = 0x87;
    rom[nmi++] = 0x87;
    rom[nmi++] = 0xB1;  // OR C - combine with the low nibble
    rom[nmi++] = 0x32;  // LD (0xC000),A - store the reconstructed byte to RAM
    rom[nmi++] = 0x00;
    rom[nmi++] = 0xC0;
    rom[nmi++] = 0xED;  // RETN
    rom[nmi++] = 0x45;

    return rom;
}

}  // namespace

void NinjaWarriorsMachineConstructsAndRegistersFourteenChannels() {
    NinjaWarriorsMachine machine({}, {}, {});
    machine.Initialize();

    const std::vector<LogicalChannel>& channels = machine.LogicalChannels();
    Check(channels.size() == 14, "channels: NinjaWarriorsMachine exposes 14 LogicalChannels (4 FM + 3 SSG + 6 "
                                  "ADPCM-A + 1 ADPCM-B)");
    if (channels.size() == 14) {
        Check(channels[0].DeviceName == "YM2610" && channels[0].ChannelName == "FM 1", "channels: id 0 is FM 1");
        Check(channels[3].ChannelName == "FM 4", "channels: id 3 is FM 4");
        Check(channels[4].ChannelName == "SSG 1", "channels: id 4 is SSG 1");
        Check(channels[6].ChannelName == "SSG 3", "channels: id 6 is SSG 3");
        Check(channels[7].ChannelName == "ADPCM-A 1", "channels: id 7 is ADPCM-A 1");
        Check(channels[12].ChannelName == "ADPCM-A 6", "channels: id 12 is ADPCM-A 6");
        Check(channels[13].ChannelName == "ADPCM-B", "channels: id 13 is ADPCM-B");
    }
}

void NinjaWarriorsMachineFmAdpcmChannelsStayCenteredUnlessOverridden() {
    // A real-ROM write-tap on
    // pancontrol_w (0xE400-0xE403) showed the sound Z80 always writes the
    // same value to all 4 physical-output gains at once - the FM/ADPCM-A/
    // ADPCM-B section is genuinely mono on real hardware, regardless of the
    // YM2610 chip's own internal per-channel L/R output-enable bits, so reading
    // those bits directly would fabricate stereo separation the real board never produces.
    NinjaWarriorsMachine machine({}, {}, {});
    machine.Initialize();
    machine.Render(EmuDuration{256}, nullptr);

    std::vector<LogicalChannel>& channels = machine.LogicalChannels();
    bool allCentered = true;
    for (const LogicalChannel& channel : channels) {
        if (channel.Pan != 0.0f) {
            allCentered = false;
            break;
        }
    }
    Check(allCentered, "mono board: every channel (FM/SSG/ADPCM-A/ADPCM-B) stays centered by default");

    // PanOverridden must still be honored even though
    // nothing auto-syncs Pan away from center on this board.
    channels[0].Pan = 0.5f;
    channels[0].PanOverridden = true;
    machine.Render(EmuDuration{256}, nullptr);
    Check(channels[0].Pan == 0.5f, "override: a UI-set Pan is not reset to 0 by the next Render() block");
}

void NinjaWarriorsMachineCommandInjectionDeliversViaTc0140sytToAudiocpuNmi() {
    NinjaWarriorsMachine machine(BuildAudiocpuCommandInjectionRom(), {}, {});
    machine.Initialize();

    MusicCommand command;
    command.Kind = MusicCommandKind::Raw;
    command.Value = 0xA5;
    machine.SendCommand(command);

    machine.Render(EmuDuration{256}, nullptr);

    auto snap = machine.AudioCpuSnapshot();
    Check(machine.ReadAudioCpuMemory(0xC000) == 0xA5,
          "command injection: audiocpu's NMI handler reconstructed the 2-nibble TC0140SYT message and stored it to "
          "RAM");
    Check(snap.nmiServiceCount >= 1, "command injection: audiocpu actually serviced an NMI");
    Check(snap.pc == 0x000C, "command injection: RETN resumed the self-loop after the handler returned");
}

void NinjaWarriorsMachineRepeatedCommandsReAssertNmi() {
    NinjaWarriorsMachine machine(BuildAudiocpuCommandInjectionRom(), {}, {});
    machine.Initialize();

    MusicCommand first;
    first.Kind = MusicCommandKind::Raw;
    first.Value = 0x11;
    machine.SendCommand(first);
    machine.Render(EmuDuration{256}, nullptr);
    Check(machine.ReadAudioCpuMemory(0xC000) == 0x11, "re-assert: first command's byte observed");

    // NinjaWarriorsCommandInterface's NmiHandler only fires on an asserted/
    // deasserted transition (edge-based, same reasoning as
    // DariusCommandInterface) - a second
    // SendCommand must produce a genuine new edge.
    MusicCommand second;
    second.Kind = MusicCommandKind::Raw;
    second.Value = 0x99;
    machine.SendCommand(second);
    machine.Render(EmuDuration{256}, nullptr);
    Check(machine.ReadAudioCpuMemory(0xC000) == 0x99,
          "re-assert: second command's byte observed after the first was serviced");
    Check(machine.AudioCpuSnapshot().nmiServiceCount >= 2, "re-assert: both commands caused NMI services");
}

void NinjaWarriorsMachineRegisteredInDefaultCatalog() {
    const MachineDefinition* def = DefaultMachineRegistry().Find(MachineId("TAITO_NINJAWARRIORS"));
    Check(def != nullptr, "catalog: TAITO_NINJAWARRIORS is registered in the default MachineRegistry");
    if (def != nullptr) {
        Check(def->Category == MachineCategory::Arcade, "catalog: TAITO_NINJAWARRIORS Category == Arcade");
        Check(def->Manufacturer == "Taito", "catalog: TAITO_NINJAWARRIORS Manufacturer == Taito");
        Check(!def->Variants.empty(), "catalog: TAITO_NINJAWARRIORS has at least one Variant");
    }
}
