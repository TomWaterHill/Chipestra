// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// OutRunMachine native tests. All ROM bytes below are hand-written
// synthetic programs (no real Out Run ROM data).
//
// Synthetic ROM layout shared by all tests here (identical to XBoardMachine's
// own synthetic tests - the memory map, I/O port decode, and NMI vector
// semantics are numerically the same on both boards):
//   0x0000  LD HL,F800h      ; 21 00 F8 (10 cycles)
//   0x0003  LD (HL),ABh      ; 36 AB    (10 cycles)
//   0x0005  HALT             ; 76       ( 4 cycles) - waits for the first NMI
//   0x0006  JR 0x0006        ; 18 FE    (12 cycles) - after the NMI handler
//                                                     returns here, spin
//                                                     forever (not halted) so
//                                                     the CPU has a defined
//                                                     landing spot no matter
//                                                     how many cycles a
//                                                     Render() runs
//   0x0066  IN A,(40h)       ; DB 40    (11 cycles) - NMI vector: read the
//                                                      command latch (this
//                                                      also clears NMI)
//   0x0068  LD (F801h),A     ; 32 01 F8 (13 cycles) - store it where the
//                                                      test can observe it
//   0x006B  RETN             ; ED 45    (14 cycles)
//
// This proves reset (execution starts at 0x0000), the ROM/RAM read+write
// wiring, the NMI vector dispatch, and the command-latch I/O port wiring
// all in one small, easily checked program.

#include "outrun_machine_tests.h"

#include "default_machine_catalog.h"
#include "devices/sega/outrun/outrun_machine.h"
#include "machine_registry.h"
#include "test_support.h"
#include "chipestra_abi.h"

#include <cstdint>
#include <vector>

namespace {

std::vector<std::uint8_t> BuildSyntheticRom() {
    std::vector<std::uint8_t> rom(0xF000, 0x00);
    // [0x0000] LD HL,F800h ; LD (HL),ABh ; HALT ; JR $ (self-loop landing spot)
    std::size_t i = 0;
    rom[i++] = 0x21;
    rom[i++] = 0x00;
    rom[i++] = 0xF8;
    rom[i++] = 0x36;
    rom[i++] = 0xAB;
    rom[i++] = 0x76;
    rom[i++] = 0x18;  // JR -2 -> 0x0006
    rom[i++] = 0xFE;

    // [0x0066] IN A,(40h) ; LD (F801h),A ; RETN
    i = 0x0066;
    rom[i++] = 0xDB;
    rom[i++] = 0x40;
    rom[i++] = 0x32;
    rom[i++] = 0x01;
    rom[i++] = 0xF8;
    rom[i++] = 0xED;
    rom[i++] = 0x45;

    return rom;
}

}  // namespace

void OutRunMachineBootSequenceReadsAndWrites() {
    OutRunMachine machine(BuildSyntheticRom());
    machine.Initialize();

    // LD HL,F800h(10) + LD (HL),ABh(10) + HALT(4) = 24, plus some spin
    // budget so it settles into the halted state.
    machine.Render(EmuDuration{40}, nullptr);

    auto snap = machine.Z80Snapshot();
    Check(snap.pc == 0x0006, "boot: PC landed right after HALT (reset vector executed in order)");
    Check(snap.hl == 0xF800, "boot: LD HL,F800h read its immediate operand from ROM correctly");
    Check(snap.halted, "boot: CPU is halted, waiting for a command (no NMI asserted yet)");
    Check(machine.ReadMemory(0xF800) == 0xAB, "boot: LD (HL),ABh wrote through to RAM correctly");
}

void OutRunMachineCommandInjectionTriggersNmi() {
    OutRunMachine machine(BuildSyntheticRom());
    machine.Initialize();
    machine.Render(EmuDuration{40}, nullptr);

    MusicCommand command;
    command.Kind = MusicCommandKind::Raw;
    command.Value = 0x77;
    machine.SendCommand(command);

    // NMI ack(11) + IN A,(40h)(11) + LD (F801h),A(13) + RETN(14) = 49,
    // plus spin budget.
    machine.Render(EmuDuration{80}, nullptr);

    auto snap = machine.Z80Snapshot();
    Check(machine.ReadMemory(0xF801) == 0x77, "command injection: NMI handler read the latched command byte");
    Check(!snap.halted, "command injection: RETN resumed normal execution, no longer halted");
}

void OutRunMachineRepeatedCommandsReArmNmi() {
    OutRunMachine machine(BuildSyntheticRom());
    machine.Initialize();
    machine.Render(EmuDuration{40}, nullptr);

    MusicCommand first;
    first.Kind = MusicCommandKind::Raw;
    first.Value = 0x11;
    machine.SendCommand(first);
    machine.Render(EmuDuration{80}, nullptr);
    Check(machine.ReadMemory(0xF801) == 0x11, "re-arm: first command's byte observed");

    // The NMI handler's own IN A,(40h) cleared NMI as a side effect - a second
    // SendCommand must re-assert it from a genuine Clear->Assert edge, not be
    // swallowed.
    MusicCommand second;
    second.Kind = MusicCommandKind::Raw;
    second.Value = 0x99;
    machine.SendCommand(second);
    machine.Render(EmuDuration{80}, nullptr);
    Check(machine.ReadMemory(0xF801) == 0x99, "re-arm: second command's byte observed after the first was serviced");
}

void OutRunMachineRegisteredInDefaultCatalog() {
    const MachineDefinition* def = DefaultMachineRegistry().Find(MachineId("SEGA_OUTRUN"));
    Check(def != nullptr, "catalog: SEGA_OUTRUN is registered in the default MachineRegistry");
    if (def != nullptr) {
        Check(def->Category == MachineCategory::Arcade, "catalog: SEGA_OUTRUN Category == Arcade");
        Check(def->Manufacturer == "SEGA", "catalog: SEGA_OUTRUN Manufacturer == SEGA");
        Check(!def->Variants.empty(), "catalog: SEGA_OUTRUN has at least one Variant");
    }
}
