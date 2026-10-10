// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// XBoardMachine native tests. All ROM bytes below are hand-written
// synthetic programs (no real After Burner II / X-Board ROM data).
//
// Synthetic ROM layout shared by all tests here:
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

#include "xboard_machine_tests.h"

#include "default_machine_catalog.h"
#include "devices/sega/xboard/xboard_machine.h"
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

void XBoardMachineBootSequenceReadsAndWrites() {
    XBoardMachine machine(BuildSyntheticRom());
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

void XBoardMachineCommandInjectionTriggersNmi() {
    XBoardMachine machine(BuildSyntheticRom());
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

void XBoardMachineRepeatedCommandsReArmNmi() {
    XBoardMachine machine(BuildSyntheticRom());
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

void XBoardMachineClockCarryForwardBoundsLongRunDrift() {
    // Numeric proof that the carry-forward (xboard_machine.h's
    // cumulative_ticks_processed_/cumulative_cycles_executed_) keeps the
    // long-run average Z80 rate exact, rather than a constant ~0.00625% slow.
    XBoardMachine machine(BuildSyntheticRom());
    machine.Initialize();

    // Initialize()'s boot warm-up (kBootWarmupFrames) already runs the
    // synthetic ROM's five-instruction boot sequence to completion and
    // settles the CPU into its self-looping HALT. From here, every
    // Execute() iteration burns exactly 4 T-states
    // (MameDerivedZ80Adapter::Execute's `if (halted_)` branch), giving the
    // carry-forward's residual error a known, tiny ceiling to check against.
    constexpr std::uint64_t kBlockTicks = 256;
    constexpr int kBlocks = 50000;
    for (int i = 0; i < kBlocks; ++i) {
        machine.Render(EmuDuration{kBlockTicks}, nullptr);
    }

    const std::uint64_t processed = machine.CumulativeTicksProcessedForTest();
    const std::uint64_t executed = machine.CumulativeCyclesExecutedForTest();

    ClockConverter converter(kNativeRuntimeTickRateHz, XBoardMachine::kZ80ClockRate);
    const std::uint64_t target = converter.TicksToCycles(EmuDuration(processed));
    const std::uint64_t diff = target > executed ? target - executed : executed - target;

    // Halted-loop granularity (4 T-states/iteration) bounds the residual to
    // under one such step, however many blocks are rendered - the opposite
    // of a per-block truncation error, which would grow linearly with block
    // count (checked below).
    Check(diff < 4, "carry-forward: cumulative Z80 cycles track the exact target within one halted-loop step, however many blocks rendered");

    // What a carry-less design (a fresh TicksToCycles() call on each block's
    // fixed duration) would have drifted by over this same run, for
    // comparison - a real, growing error rather than an already-negligible
    // one.
    const std::uint64_t perBlockCyclesNoCarry = converter.TicksToCycles(EmuDuration(kBlockTicks));
    const std::uint64_t noCarryTotal = perBlockCyclesNoCarry * static_cast<std::uint64_t>(kBlocks);
    Check(target > noCarryTotal && (target - noCarryTotal) > 1000,
          "carry-forward: carry-less per-block truncation would have drifted thousands of cycles over this same run");
}

void XBoardMachineChannelPanAutoSyncsUnlessOverridden() {
    // A UI-driven Pan change (chipestra_set_logical_channel_pan) must not be
    // overwritten by the next block's routing update: auto-sync while
    // un-overridden, frozen once overridden, auto-sync resumes once the
    // override is cleared (see LogicalChannel::PanOverridden, chipestra_abi.h).
    XBoardMachine machine(BuildSyntheticRom());
    machine.Initialize();

    Ym2151DeviceAdapter& ym = machine.Ym2151ForTest();
    auto setChannelZeroRouting = [&](std::uint8_t rlBits) {
        ym.WriteAddress(0x20);
        ym.WriteData(rlBits);  // bits 6/7 = L/R; low bits (algorithm/feedback) left at 0
    };

    // Left-only routing -> auto-synced Pan should read full left.
    setChannelZeroRouting(0x40);
    machine.Render(EmuDuration{256}, nullptr);
    std::vector<LogicalChannel>& channels = machine.LogicalChannels();
    Check(channels[0].Pan == -1.0f, "auto-sync: Left-only YM2151 routing sets Pan to full left");
    Check(!channels[0].PanOverridden, "auto-sync: PanOverridden starts false");

    // Simulate the UI overriding Pan (what chipestra_set_logical_channel_pan does).
    channels[0].Pan = 0.5f;
    channels[0].PanOverridden = true;

    // Right-only routing now - an un-overridden channel would flip to +1.0,
    // but this one must stay exactly where the UI left it.
    setChannelZeroRouting(0x80);
    machine.Render(EmuDuration{256}, nullptr);
    Check(channels[0].Pan == 0.5f, "override: Pan stays at the UI-set value despite the driver's new routing");

    // Simulate the UI clearing the override (chipestra_clear_logical_channel_pan_override).
    channels[0].PanOverridden = false;
    machine.Render(EmuDuration{256}, nullptr);
    Check(channels[0].Pan == 1.0f, "clear override: Pan resumes auto-sync, now reflecting the Right-only routing");
}

void XBoardMachineRegisteredInDefaultCatalog() {
    const MachineDefinition* def = DefaultMachineRegistry().Find(MachineId("SEGA_XBOARD"));
    Check(def != nullptr, "catalog: SEGA_XBOARD is registered in the default MachineRegistry");
    if (def != nullptr) {
        Check(def->Category == MachineCategory::Arcade, "catalog: SEGA_XBOARD Category == Arcade");
        Check(def->Manufacturer == "SEGA", "catalog: SEGA_XBOARD Manufacturer == SEGA");
        Check(!def->Variants.empty(), "catalog: SEGA_XBOARD has at least one Variant");
    }
}
