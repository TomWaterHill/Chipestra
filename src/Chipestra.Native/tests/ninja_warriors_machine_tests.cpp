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

#include <cmath>
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

// audiocpu code that writes the four pan-control ports 0xE400-0xE403 (0xCE, 0x80, 0x00, 0xFF) and then spins.
std::vector<std::uint8_t> BuildAudiocpuPanPortRom() {
    std::vector<std::uint8_t> rom(0x100, 0x00);
    std::size_t i = 0;
    rom[i++] = 0x31; rom[i++] = 0x00; rom[i++] = 0xDF;  // LD SP,DF00h
    const std::uint8_t values[4] = {0xCE, 0x80, 0x00, 0xFF};
    for (int port = 0; port < 4; ++port) {
        rom[i++] = 0x3E; rom[i++] = values[port];                                          // LD A,value
        rom[i++] = 0x32; rom[i++] = static_cast<std::uint8_t>(port); rom[i++] = 0xE4;      // LD (0xE400+port),A
    }
    const std::size_t jp = i;
    rom[i++] = 0xC3; rom[i++] = static_cast<std::uint8_t>(jp); rom[i++] = 0x00;  // JP self
    return rom;
}

// Sets the L/R output bits (bit 7 = left, bit 6 = right) of ADPCM-A channel `ch`.
void SetAdpcmAOutputBits(NinjaWarriorsMachine& machine, std::uint8_t ch, bool left, bool right) {
    machine.Ym2610ForTest().Write(2, static_cast<std::uint8_t>(0x08 + ch));
    machine.Ym2610ForTest().Write(3, static_cast<std::uint8_t>((left ? 0x80 : 0) | (right ? 0x40 : 0) | 0x1F));
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

void NinjaWarriorsMachineRoutingCountsTheOutputBitsOfEachChannel() {
    // With the pan-control gains at their power-on 1.0: a channel that outputs on both the chip's left and right
    // counts twice (each speaker gets both), one that outputs on one side counts once, and one with neither is silent.
    NinjaWarriorsMachine machine({}, {}, {});
    machine.Initialize();
    SetAdpcmAOutputBits(machine, 0, true, true);
    SetAdpcmAOutputBits(machine, 1, true, false);
    SetAdpcmAOutputBits(machine, 2, false, true);
    SetAdpcmAOutputBits(machine, 3, false, false);
    machine.Render(EmuDuration{256}, nullptr);
    const std::vector<LogicalChannel>& channels = machine.LogicalChannels();
    const std::uint32_t a = NinjaWarriorsMachine::kAdpcmABase;
    Check(channels[a + 0].RoutingGain == 4.0f && channels[a + 0].Pan == 0.0f,
          "routing: left+right bits count twice on each speaker (gain 2 x 2, centred)");
    Check(channels[a + 1].RoutingGain == 2.0f && channels[a + 1].Pan == 0.0f,
          "routing: a left-only channel counts once on each speaker (the board sums the chip's two outputs into both)");
    Check(channels[a + 2].RoutingGain == 2.0f && channels[a + 2].Pan == 0.0f,
          "routing: a right-only channel counts once on each speaker");
    Check(channels[a + 3].RoutingGain == 0.0f, "routing: a channel with neither output bit is silent");
    Check(channels[NinjaWarriorsMachine::kSsgBase].RoutingGain == NinjaWarriorsMachine::kSsgRoutingGain,
          "routing: the SSG gain is left as it was");
}

void NinjaWarriorsMachineFollowsThePanControlPorts() {
    // The ports hold int(value * 100 / 255) percent. Port 1 = chip left -> left speaker, port 0 = chip left -> right
    // speaker, port 3 = chip right -> left speaker, port 2 = chip right -> right speaker.
    NinjaWarriorsMachine machine(BuildAudiocpuPanPortRom(), {}, {});
    machine.Initialize();
    Check(machine.PanGainForTest(0) == 0.80f, "ports: 0xCE -> 80 percent");
    Check(machine.PanGainForTest(1) == 0.50f, "ports: 0x80 -> 50 percent");
    Check(machine.PanGainForTest(2) == 0.00f, "ports: 0x00 -> 0 percent");
    Check(machine.PanGainForTest(3) == 1.00f, "ports: 0xFF -> 100 percent");

    SetAdpcmAOutputBits(machine, 0, true, false);
    SetAdpcmAOutputBits(machine, 1, false, true);
    SetAdpcmAOutputBits(machine, 2, true, true);
    machine.Render(EmuDuration{256}, nullptr);
    const std::vector<LogicalChannel>& channels = machine.LogicalChannels();
    const std::uint32_t a = NinjaWarriorsMachine::kAdpcmABase;
    // left-only: left speaker 0.5, right speaker 0.8
    Check(std::fabs(channels[a + 0].RoutingGain - 1.3f) < 1e-6f && std::fabs(channels[a + 0].Pan - (0.3f / 1.3f)) < 1e-6f,
          "ports: a left-only channel reaches the left speaker through port 1 and the right speaker through port 0");
    // right-only: left speaker 1.0 (port 3), right speaker 0.0 (port 2)
    Check(std::fabs(channels[a + 1].RoutingGain - 1.0f) < 1e-6f && channels[a + 1].Pan == -1.0f,
          "ports: a right-only channel reaches the left speaker through port 3 and the right speaker through port 2");
    // both: sum of the two
    Check(std::fabs(channels[a + 2].RoutingGain - 2.3f) < 1e-6f, "ports: a left+right channel adds the two paths");

    // A UI-set pan is kept, while the gain still follows the ports.
    machine.LogicalChannels()[a + 0].Pan = 0.25f;
    machine.LogicalChannels()[a + 0].PanOverridden = true;
    machine.Render(EmuDuration{256}, nullptr);
    Check(machine.LogicalChannels()[a + 0].Pan == 0.25f, "ports: a pan set from the UI is kept");
}

void NinjaWarriorsMachineTaitoF2MixSendsEachChipOutputToItsOwnSpeaker() {
    // The F2 System board (Metal Black): chip left -> left speaker, chip right -> right speaker, gain 1.0 each; the
    // SSG goes to both speakers with 0.75 each; the Z80's writes to 0xE400-0xE403 change nothing.
    NinjaWarriorsMachine machine(BuildAudiocpuPanPortRom(), {}, {}, NinjaWarriorsMachine::Mix::TaitoF2);
    machine.Initialize();
    SetAdpcmAOutputBits(machine, 0, true, false);
    SetAdpcmAOutputBits(machine, 1, false, true);
    SetAdpcmAOutputBits(machine, 2, true, true);
    SetAdpcmAOutputBits(machine, 3, false, false);
    machine.Render(EmuDuration{256}, nullptr);
    const std::vector<LogicalChannel>& channels = machine.LogicalChannels();
    const std::uint32_t a = NinjaWarriorsMachine::kAdpcmABase;
    Check(channels[a + 0].RoutingGain == 1.0f && channels[a + 0].Pan == -1.0f,
          "f2: a left-output channel is on the left speaker only, gain 1");
    Check(channels[a + 1].RoutingGain == 1.0f && channels[a + 1].Pan == 1.0f,
          "f2: a right-output channel is on the right speaker only, gain 1");
    Check(channels[a + 2].RoutingGain == 2.0f && channels[a + 2].Pan == 0.0f,
          "f2: a channel with both output bits is on both speakers, gain 1 each");
    Check(channels[a + 3].RoutingGain == 0.0f, "f2: a channel with neither output bit is silent");
    Check(channels[NinjaWarriorsMachine::kSsgBase].RoutingGain == 1.5f && channels[NinjaWarriorsMachine::kSsgBase].Pan == 0.0f,
          "f2: the SSG is on both speakers, 0.75 each");
}

void NinjaWarriorsMachineTaitoF2MixRemovesTheSsgRestingDc() {
    // At rest the chip's SSG outputs a constant level. The F2 mix removes it (the Ninja Warriors mix does not touch the
    // SSG, which it silences by its gain).
    NinjaWarriorsMachine f2({}, {}, {}, NinjaWarriorsMachine::Mix::TaitoF2);
    f2.Initialize();
    NinjaWarriorsMachine nj({}, {}, {});
    nj.Initialize();
    auto restingLevel = [](NinjaWarriorsMachine& machine) {
        // Tone and noise off, channel A at full volume: the chip's output stays at a constant level.
        machine.Ym2610ForTest().Write(0, 0x07);
        machine.Ym2610ForTest().Write(1, 0x3F);
        machine.Ym2610ForTest().Write(0, 0x08);
        machine.Ym2610ForTest().Write(1, 0x0F);
        LogicalChannelSink sink;
        float worst = 0.0f;
        for (int block = 0; block < 48; ++block) {  // the first 40 blocks let the blocker settle on the step
            machine.Render(EmuDuration{256}, &sink);
            const LogicalChannelBuffer& buffer = sink.ChannelBuffer(NinjaWarriorsMachine::kSsgBase);
            for (std::size_t n = 0; block >= 40 && n < buffer.Size(); ++n) worst = std::max(worst, std::fabs(buffer[n]));
            sink.ClearAll();
        }
        return worst;
    };
    const float f2Level = restingLevel(f2);
    const float njLevel = restingLevel(nj);
    Check(njLevel > 0.1f, "f2: (reference) the chip's SSG rests at a constant level");
    Check(f2Level < 1e-3f, "f2: the SSG's resting DC is removed");
}
