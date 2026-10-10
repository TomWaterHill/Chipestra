// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// GradiusMachine native tests. All ROM bytes below are hand-written
// synthetic programs (no real Gradius ROM data).
//
// Synthetic ROM layout shared by all tests here:
//   0x0000  LD SP,7F00h     ; 31 00 7F (10 cycles)
//   0x0003  IM 1            ; ED 56    ( 8 cycles)
//   0x0005  EI              ; FB       ( 4 cycles)
//   0x0006  LD A,CDh        ; 3E CD    ( 7 cycles)
//   0x0008  LD (4010h),A    ; 32 10 40 (13 cycles) - proves boot ran (0xCD,
//                                                     not RAM's zero default)
//   0x000B  HALT            ; 76       ( 4 cycles) - waits for the IRQ pulse
//   0x000C  JR 0x000C       ; 18 FE    (12 cycles) - landing spot after the
//                                                     IRQ handler RETIs
//   0x0038  IM 1's fixed vector - the command-latch IRQ handler:
//     PUSH AF        ; F5
//     LD A,(E001h)   ; 3A 01 E0 - read the soundlatch (0xE001)
//     LD (4001h),A   ; 32 01 40 - store it where the test can observe it
//     POP AF         ; F1
//     EI             ; FB
//     RETI           ; ED 4D
//   0x0066  NMI's fixed vector - GradiusMachine also drives a periodic
//     VBLANK NMI, independent of the IRQ path above;
//     kBootWarmupFrames spans more than one vblank period so at least one
//     NMI fires during every Initialize()/Reset() warm-up. A real driver
//     always has *some* NMI handler; this synthetic ROM needs one too or
//     the NMI would otherwise vector into zeroed ROM (NOPs, then falling
//     through into the IM1 handler's own bytes) instead of returning
//     cleanly, corrupting these tests' HALT-loop timing assumptions:
//     RETN           ; ED 45 - restores IFF1 from IFF2 and returns; this
//                              ROM never explicitly touches IFF2, so it is
//                              whatever CheckPendingInterrupts() left it as
//                              (false, matching IFF1) - fine, since nothing
//                              here relies on IFF1 being enabled afterward.
//
// This proves reset (execution starts at 0x0000), the ROM/shared-RAM
// read+write wiring, IM1 IRQ vector dispatch, and the memory-mapped
// soundlatch wiring all in one small, easily checked program.

#include "gradius_machine_tests.h"

#include "default_machine_catalog.h"
#include "devices/konami/gradius/gradius_machine.h"
#include "machine_registry.h"
#include "test_support.h"
#include "chipestra_abi.h"

#include <cstdint>
#include <vector>

namespace {

std::vector<std::uint8_t> BuildSyntheticRom() {
    std::vector<std::uint8_t> rom(0x2000, 0x00);
    std::size_t i = 0;
    rom[i++] = 0x31;  // LD SP,7F00h
    rom[i++] = 0x00;
    rom[i++] = 0x7F;
    rom[i++] = 0xED;  // IM 1
    rom[i++] = 0x56;
    rom[i++] = 0xFB;  // EI
    rom[i++] = 0x3E;  // LD A,CDh
    rom[i++] = 0xCD;
    rom[i++] = 0x32;  // LD (4010h),A
    rom[i++] = 0x10;
    rom[i++] = 0x40;
    rom[i++] = 0x76;  // HALT
    rom[i++] = 0x18;  // JR $ (-> 0x000C)
    rom[i++] = 0xFE;

    i = 0x0038;
    rom[i++] = 0xF5;  // PUSH AF
    rom[i++] = 0x3A;  // LD A,(E001h)
    rom[i++] = 0x01;
    rom[i++] = 0xE0;
    rom[i++] = 0x32;  // LD (4001h),A
    rom[i++] = 0x01;
    rom[i++] = 0x40;
    rom[i++] = 0xF1;  // POP AF
    rom[i++] = 0xFB;  // EI
    rom[i++] = 0xED;  // RETI
    rom[i++] = 0x4D;

    i = 0x0066;
    rom[i++] = 0xED;  // RETN
    rom[i++] = 0x45;

    return rom;
}

}  // namespace

void GradiusMachineBootSequenceReadsAndWrites() {
    // Second constructor arg (K005289 waveform PROM) left empty -
    // these tests only exercise Z80 boot/IRQ, not K005289 itself
    // (k005289_adapter_tests.cpp covers that adapter directly).
    GradiusMachine machine(BuildSyntheticRom(), {});
    machine.Initialize();

    machine.Render(EmuDuration{60}, nullptr);

    auto snap = machine.Z80Snapshot();
    Check(snap.pc == 0x000C, "boot: PC landed right after HALT (reset vector executed in order)");
    // Not asserting snap.halted here: kBootWarmupFrames (Initialize()'s own
    // warm-up) spans more than one VBLANK NMI period, and this ROM's
    // NMI handler (RETN, see this file's own header comment) resumes
    // execution at the same PC=0x000C "spin at $" loop the IRQ handler
    // lands at - HALT itself is not re-entered (matches real Z80: HALT's
    // resume address is the *following* instruction, not the HALT opcode
    // again), so by the time Initialize() returns the CPU has deterministically
    // already been woken once. irqServiceCount (the command-latch path,
    // independent of NMI) is the meaningful "no command serviced yet" check.
    Check(snap.irqServiceCount == 0, "boot: no command has been serviced yet (IRQ path untouched)");
    Check(machine.ReadMemory(0x4010) == 0xCD, "boot: LD (4010h),A wrote through to shared RAM correctly");
}

void GradiusMachineCommandInjectionTriggersIrq() {
    GradiusMachine machine(BuildSyntheticRom(), {});
    machine.Initialize();
    machine.Render(EmuDuration{60}, nullptr);

    MusicCommand command;
    command.Kind = MusicCommandKind::Raw;
    command.Value = 0x77;
    machine.SendCommand(command);

    // IRQ ack(13) + PUSH AF(11) + LD A,(nn)(13) + LD (nn),A(13) + POP AF(10)
    // + EI(4) + RETI(14) = 78, plus spin budget.
    machine.Render(EmuDuration{110}, nullptr);

    auto snap = machine.Z80Snapshot();
    Check(machine.ReadMemory(0x4001) == 0x77, "command injection: IRQ handler read the memory-mapped soundlatch (0xE001)");
    Check(!snap.halted, "command injection: RETI resumed normal execution, no longer halted");
    Check(snap.pc == 0x000C, "command injection: execution resumed at the spin loop after the handler returned");
}

void GradiusMachineRepeatedCommandsReAssertIrq() {
    GradiusMachine machine(BuildSyntheticRom(), {});
    machine.Initialize();
    machine.Render(EmuDuration{60}, nullptr);

    MusicCommand first;
    first.Kind = MusicCommandKind::Raw;
    first.Value = 0x11;
    machine.SendCommand(first);
    machine.Render(EmuDuration{110}, nullptr);
    Check(machine.ReadMemory(0x4001) == 0x11, "re-assert: first command's byte observed");

    // GradiusCommandInterface::ClearIrqAfterBlock() deasserts IRQ
    // unconditionally at the end of every block (this class's own header
    // comment) - a second SendCommand must produce a genuine Clear->Assert
    // edge, not be swallowed by a line that was left stuck high.
    MusicCommand second;
    second.Kind = MusicCommandKind::Raw;
    second.Value = 0x99;
    machine.SendCommand(second);
    machine.Render(EmuDuration{110}, nullptr);
    Check(machine.ReadMemory(0x4001) == 0x99, "re-assert: second command's byte observed after the first was serviced");
}

namespace {

// A minimal 68000 program, hand-assembled the same way
// m68000_adapter_tests.cpp derives its own opcodes - no real ROM data.
// `words` are placed starting at `codeAddress`; the reset vector (SSP from
// address 0, PC from address 4) is set separately by each test below.
std::vector<std::uint8_t> BuildMainCpuRom(std::uint32_t ssp, std::uint32_t codeAddress,
                                           std::initializer_list<std::uint16_t> words) {
    std::vector<std::uint8_t> rom(0x10000, 0x00);
    auto setLong = [&](std::uint32_t addr, std::uint32_t value) {
        rom[addr] = static_cast<std::uint8_t>(value >> 24);
        rom[addr + 1] = static_cast<std::uint8_t>(value >> 16);
        rom[addr + 2] = static_cast<std::uint8_t>(value >> 8);
        rom[addr + 3] = static_cast<std::uint8_t>(value);
    };
    setLong(0, ssp);
    setLong(4, codeAddress);
    std::uint32_t addr = codeAddress;
    for (std::uint16_t w : words) {
        rom[addr] = static_cast<std::uint8_t>(w >> 8);
        rom[addr + 1] = static_cast<std::uint8_t>(w & 0xFF);
        addr += 2;
    }
    return rom;
}

}  // namespace

void GradiusMachineMainCpuSharedRamBridgeReachesSoundZ80() {
    // Sound Z80: LD SP,7F00h ; EI ; JR $ - just enough to let RunBootWarmup()
    // (the adaptive warm-up) see IFF1 become set quickly, without
    // needing a real boot handshake for this bridge-only test.
    std::vector<std::uint8_t> z80Rom(0x2000, 0x00);
    std::size_t zi = 0;
    z80Rom[zi++] = 0x31;
    z80Rom[zi++] = 0x00;
    z80Rom[zi++] = 0x7F;
    z80Rom[zi++] = 0xFB;  // EI
    z80Rom[zi++] = 0x18;  // JR $
    z80Rom[zi++] = 0xFE;

    // Main CPU: MOVE.B #$42,($020001).L (writes the ODD byte of the
    // shared-RAM bridge's word 0:
    // this is Z80 shared-RAM offset 0, i.e. Z80 address 0x4000) ; BRA $
    std::vector<std::uint8_t> mainRom =
        BuildMainCpuRom(0x00001000, 0x00000400, {0x13FC, 0x0042, 0x0002, 0x0001, 0x60FE});

    GradiusMachine machine(z80Rom, {}, mainRom, {});
    machine.Initialize();

    Check(machine.ReadMemory(0x4000) == 0x42,
          "main CPU shared-RAM bridge: 68000 write to the odd byte reaches the sound Z80's shared RAM at 0x4000");
}

void GradiusMachineMainCpuSharedRamBridgeDropsEvenByteWrite() {
    std::vector<std::uint8_t> z80Rom(0x2000, 0x00);
    std::size_t zi = 0;
    z80Rom[zi++] = 0x31;
    z80Rom[zi++] = 0x00;
    z80Rom[zi++] = 0x7F;
    z80Rom[zi++] = 0xFB;  // EI
    z80Rom[zi++] = 0x18;  // JR $
    z80Rom[zi++] = 0xFE;

    // MOVE.B #$99,($020000).L - the EVEN byte of word 0 (only
    // ACCESSING_BITS_0_7, the odd byte, reaches
    // real hardware's shared RAM - this write must be silently dropped).
    std::vector<std::uint8_t> mainRom =
        BuildMainCpuRom(0x00001000, 0x00000400, {0x13FC, 0x0099, 0x0002, 0x0000, 0x60FE});

    GradiusMachine machine(z80Rom, {}, mainRom, {});
    machine.Initialize();

    Check(machine.ReadMemory(0x4000) == 0x00,
          "main CPU shared-RAM bridge: a write to the even byte of a word is dropped, matching real hardware's "
          "ACCESSING_BITS_0_7 gating");
}

void GradiusMachineMainCpuSoundlatchAndOutlatchReachZ80Irq() {
    // Main CPU: MOVE.B #$77,($05C001).L (soundlatch write, no IRQ side
    // effect by itself) ; MOVE.B #$01,($05E004).L (outlatch bit 2 =
    // sound_irq_w, pulses the IRQ using whatever the soundlatch write just
    // latched) ; BRA $.
    std::vector<std::uint8_t> mainRom = BuildMainCpuRom(
        0x00001000, 0x00000400,
        {0x13FC, 0x0077, 0x0005, 0xC001, 0x13FC, 0x0001, 0x0005, 0xE004, 0x60FE});

    // Reuses this file's own BuildSyntheticRom() (the same IM1 handler that
    // GradiusMachineCommandInjectionTriggersIrq already verifies against
    // Chipestra's own SendCommand() path) to prove the main CPU's real
    // soundlatch/outlatch path reaches the identical IRQ handler.
    GradiusMachine machine(BuildSyntheticRom(), {}, mainRom, {});
    machine.Initialize();

    // The main CPU's outlatch write happens during Initialize()'s own
    // adaptive warm-up (which stops as soon as the sound Z80's IFF1 is set,
    // shortly after boot) - GradiusCommandInterface::Tick() only picks up
    // the resulting pending IRQ at the *top* of the next Render() call
    // (one-block latency, this file's own header comment on Render()'s
    // ordering), so an explicit Render() call is needed here, same IRQ
    // service budget as GradiusMachineCommandInjectionTriggersIrq.
    machine.Render(EmuDuration{110}, nullptr);

    auto snap = machine.Z80Snapshot();
    Check(machine.ReadMemory(0x4001) == 0x77,
          "main CPU soundlatch/outlatch: the sound Z80's IRQ handler read the soundlatch byte the main CPU wrote");
    Check(!snap.halted, "main CPU soundlatch/outlatch: RETI resumed normal execution, no longer halted");
}

void GradiusMachineRegisteredInDefaultCatalog() {
    const MachineDefinition* def = DefaultMachineRegistry().Find(MachineId("KONAMI_GRADIUS_BUBBLESYSTEM"));
    Check(def != nullptr, "catalog: KONAMI_GRADIUS_BUBBLESYSTEM is registered in the default MachineRegistry");
    if (def != nullptr) {
        Check(def->Category == MachineCategory::Arcade, "catalog: KONAMI_GRADIUS_BUBBLESYSTEM Category == Arcade");
        Check(def->Manufacturer == "Konami", "catalog: KONAMI_GRADIUS_BUBBLESYSTEM Manufacturer == Konami");
        Check(!def->Variants.empty(), "catalog: KONAMI_GRADIUS_BUBBLESYSTEM has at least one Variant");
    }
}
