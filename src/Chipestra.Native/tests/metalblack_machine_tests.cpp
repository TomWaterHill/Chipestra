// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Metal Black native tests. All ROM bytes below are hand-written
// synthetic programs (no real Metal Black ROM data).
//
// The command-injection ROM below is copied byte-for-byte from
// NinjaWarriorsMachineCommandInjectionDeliversViaTc0140sytToAudiocpuNmi's
// own BuildAudiocpuCommandInjectionRom() (ninja_warriors_machine_tests.cpp)
// - it exercises the same TC0140SYT slave protocol at the same
// addresses (0xE200/0xE201) and RAM window (0xC000), because Metal Black's
// sound_map is identical to Ninja Warriors' and both are played through
// the same NinjaWarriorsMachine class.

#include "metalblack_machine_tests.h"

#include "default_machine_catalog.h"
#include "devices/taito/ninjaw/ninja_warriors_machine.h"
#include "machine_registry.h"
#include "test_support.h"
#include "chipestra_abi.h"

#include <cstdint>
#include <vector>

namespace {

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

void MetalBlackMachineConstructsAndRegistersFourteenChannels() {
    NinjaWarriorsMachine machine({}, {}, {});
    machine.Initialize();

    const std::vector<LogicalChannel>& channels = machine.LogicalChannels();
    Check(channels.size() == 14, "channels: Metal Black (via NinjaWarriorsMachine) exposes 14 LogicalChannels (4 "
                                  "FM + 3 SSG + 6 ADPCM-A + 1 ADPCM-B)");
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

void MetalBlackMachineCommandInjectionDeliversViaTc0140sytToAudiocpuNmi() {
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
          "RAM (same addresses as Ninja Warriors - identical sound_map)");
    Check(snap.nmiServiceCount >= 1, "command injection: audiocpu actually serviced an NMI");
    Check(snap.pc == 0x000C, "command injection: RETN resumed the self-loop after the handler returned");
}

void MetalBlackMachineRegisteredInDefaultCatalog() {
    const MachineDefinition* def = DefaultMachineRegistry().Find(MachineId("TAITO_F2SYSTEM"));
    Check(def != nullptr, "catalog: TAITO_F2SYSTEM is registered in the default MachineRegistry");
    if (def != nullptr) {
        Check(def->Category == MachineCategory::Arcade, "catalog: TAITO_F2SYSTEM Category == Arcade");
        Check(def->Manufacturer == "Taito", "catalog: TAITO_F2SYSTEM Manufacturer == Taito");
        Check(!def->Variants.empty(), "catalog: TAITO_F2SYSTEM has at least one Variant");
    }
}
