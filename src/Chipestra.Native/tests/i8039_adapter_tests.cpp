// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// I8039Core + MAME-derived i8039 backend native tests. All programs
// below are hand-written synthetic byte sequences (no real ROM/game code) -
// same "not ROM-dependent" convention as z80_adapter_tests.cpp/
// m68000_adapter_tests.cpp.
//
// Expected register/flag/cycle values are derived independently from the
// public MCS-48 instruction semantics (MAME's mcs48.cpp) - opcode bytes are
// looked up directly from that file's s_mcs48_opcodes[256] table, and
// expected results are computed by hand from the documented instruction
// semantics, not by reading back what this adapter itself computes.

#include "i8039_adapter_tests.h"

#include "devices/cpu/i8039/mame_derived_i8039_adapter.h"
#include "test_support.h"

#include <array>
#include <cstdint>
#include <vector>

namespace {

// 4KB synthetic program space + 256-byte synthetic external bus, wired to
// an I8039Core via plain lambdas - the simplest possible harness, matching
// FlatMemoryHarness's own precedent in z80_adapter_tests.cpp.
struct FlatMemoryHarness {
    std::array<std::uint8_t, 0x1000> program{};
    std::array<std::uint8_t, 0x100> bus{};
    std::uint8_t p1 = 0, p2 = 0;
    MameDerivedI8039Adapter cpu;

    FlatMemoryHarness() {
        cpu.SetProgramMemoryCallback([this](std::uint16_t addr) { return program[addr & 0xFFF]; });
        cpu.SetBusCallbacks([this](std::uint8_t addr) { return bus[addr]; },
                              [this](std::uint8_t addr, std::uint8_t value) { bus[addr] = value; });
        cpu.SetPort1Callbacks([this]() { return p1; }, [this](std::uint8_t value) { p1 = value; });
        cpu.SetPort2Callbacks([this]() { return p2; }, [this](std::uint8_t value) { p2 = value; });
    }

    void Load(std::uint16_t address, std::initializer_list<std::uint8_t> bytes) {
        std::uint16_t addr = address;
        for (std::uint8_t b : bytes) {
            program[addr++] = b;
        }
    }
};

}  // namespace

void I8039BasicMovAndCycleCounting() {
    FlatMemoryHarness h;
    // MOV A,#42h (opcode 0x23, 2 cycles) ; MOV R0,A (0xA8, 1 cycle) ;
    // INC R0 (0x18, 1 cycle) = 4 machine cycles = 60 input clocks.
    h.Load(0x000, {0x23, 0x42, 0xA8, 0x18});

    const std::uint64_t consumed = h.cpu.Execute(60);
    const auto snap = h.cpu.Snapshot();

    Check(consumed == 60, "basic mov: Execute() consumed exactly the requested budget (4 machine cycles)");
    Check(snap.a == 0x42, "basic mov: A == 0x42 after MOV A,#42h");
    Check(h.cpu.RamPeekForTest(0) == 0x43, "basic mov: R0 (bank 0, ram[0]) == 0x43 after MOV R0,A; INC R0");
    Check(snap.pc == 4, "basic mov: PC advanced by exactly 4 opcode bytes");
}

void I8039AddCarryAuxCarryFlags() {
    FlatMemoryHarness h;
    // MOV A,#FFh (0x23,0xFF, 2cyc) ; MOV R1,#01h (0xB9,0x01, 2cyc) ;
    // ADD A,R1 (0x69, 1cyc). 0xFF + 0x01 = 0x100 -> A=0x00, Carry set
    // (bit 8 carries out), AuxCarry set (low nibble 0xF+0x1=0x10 carries).
    h.Load(0x000, {0x23, 0xFF, 0xB9, 0x01, 0x69});

    h.cpu.Execute(75);  // 2+2+1 = 5 machine cycles = 75 clocks
    const auto snap = h.cpu.Snapshot();

    Check(snap.a == 0x00, "add carry: 0xFF + 0x01 wraps to 0x00");
    Check((snap.psw & 0x80) != 0, "add carry: Carry flag (0x80) set");
    Check((snap.psw & 0x40) != 0, "add carry: Auxiliary Carry flag (0x40) set");
}

void I8039JumpWithinPage() {
    FlatMemoryHarness h;
    // JMP 010h (opcode 0x04 = jmp_0, page 0, 2 cycles).
    h.Load(0x000, {0x04, 0x10});
    h.Load(0x010, {0x00});  // NOP at the target, so a subsequent fetch is well-defined

    h.cpu.Execute(30);  // 2 machine cycles = 30 clocks
    const auto snap = h.cpu.Snapshot();

    Check(snap.pc == 0x010, "jmp: PC == 0x010 after JMP 010h");
}

void I8039DjnzLoop() {
    FlatMemoryHarness h;
    // MOV R2,#03h (0xBA,0x03, 2cyc) ; DJNZ R2,002h (0xEA,0x02, 2cyc,
    // self-referencing loop - a real MCS-48 idiom). Decrements R2 each
    // pass; loops back to its own address while R2 != 0, falls through
    // once R2 reaches 0. 3 passes total (R2: 3->2->1->0, last pass not
    // taken) = 2 + 3*2 = 8 machine cycles = 120 clocks, ending at PC=4.
    h.Load(0x000, {0xBA, 0x03, 0xEA, 0x02});
    h.Load(0x004, {0x00});  // NOP after the loop, so PC=4's fetch is well-defined

    h.cpu.Execute(120);
    const auto snap = h.cpu.Snapshot();

    Check(h.cpu.RamPeekForTest(2) == 0, "djnz: R2 == 0 after the loop completes");
    Check(snap.pc == 4, "djnz: PC fell through to address 4 once R2 reached 0");
}

void I8039CallAndReturnRoundTrip() {
    FlatMemoryHarness h;
    // CALL 020h (0x14 = call_0, page 0, 2cyc) ; NOP (0x00, 1cyc, the
    // instruction immediately after the call - RET should land here).
    // At 020h: MOV A,#7Fh (0x23,0x7F, 2cyc) ; RET (0x83, 2cyc).
    // Total: CALL(2) + MOV A,#n(2) + RET(2) = 6 machine cycles = 90 clocks.
    h.Load(0x000, {0x14, 0x20, 0x00});
    h.Load(0x020, {0x23, 0x7F, 0x83});

    h.cpu.Execute(90);
    const auto snap = h.cpu.Snapshot();

    Check(snap.a == 0x7F, "call/ret: A == 0x7F (set inside the called routine)");
    // CALL 020h is a 2-byte instruction (opcode + target-low-byte), so the
    // pushed/returned-to address is 2, not 3 (the byte right after CALL's
    // own operand - there is no separate NOP consumed on the way in).
    Check(snap.pc == 2, "call/ret: RET returned to the instruction right after CALL (address 2)");
}

void I8039Port1OutputAndReadback() {
    FlatMemoryHarness h;
    // MOV A,#5Ah (0x23,0x5A, 2cyc) ; OUTL P1,A (0x39, 2cyc) ; CLR A
    // (0x27, 1cyc) ; IN A,P1 (0x09, 2cyc) - reads back Port1Read() & the
    // internal latch, both of which now reflect 0x5A.
    h.Load(0x000, {0x23, 0x5A, 0x39, 0x27, 0x09});

    h.cpu.Execute(105);  // 2+2+1+2 = 7 machine cycles = 105 clocks
    const auto snap = h.cpu.Snapshot();

    Check(h.p1 == 0x5A, "port1: the external Port1Write callback observed 0x5A");
    Check(snap.p1 == 0x5A, "port1: the internal Port1 latch is 0x5A");
    Check(snap.a == 0x5A, "port1: IN A,P1 read back 0x5A (latch AND external read, both 0x5A)");
}

void I8039ExternalBusMovx() {
    FlatMemoryHarness h;
    h.bus[0x10] = 0xC3;
    // MOV R0,#10h (0xB8,0x10, 2cyc) ; MOVX A,@R0 (0x80, 2cyc).
    h.Load(0x000, {0xB8, 0x10, 0x80});

    h.cpu.Execute(60);  // 2+2 = 4 machine cycles = 60 clocks
    const auto snap = h.cpu.Snapshot();

    Check(snap.a == 0xC3, "movx: A read back the external bus byte at R0's address (0x10)");
}

void I8039TimerOverflowServicesInterrupt() {
    FlatMemoryHarness h;
    // MOV A,#FFh (0x23,0xFF,2cyc) ; MOV T,A (0x62,1cyc) ; STRT T
    // (0x55,1cyc, resets the 5-bit prescaler to 0) ; EN TCNTI (0x25,1cyc)
    // ; 32x NOP (0x00, 1cyc each). Each machine cycle bumps the prescaler
    // by 1; the timer itself increments by prescaler>>5 - so after exactly
    // 32 accumulated machine cycles the prescaler wraps once and the timer
    // increments by exactly 1 (0xFF -> 0x00, an overflow). One more
    // instruction (a NOP placed right after the 32) is where CheckIrqs()
    // actually services the now-pending timer interrupt, vectoring to
    // 0x007 (MAME mcs48.cpp's check_irqs()).
    h.Load(0x000, {0x23, 0xFF, 0x62, 0x55, 0x25});
    for (std::uint16_t i = 0; i < 33; ++i) h.program[5 + i] = 0x00;  // 33 NOPs
    h.Load(0x007, {0x00});  // NOP at the timer-IRQ vector itself

    // 2+1+1+1 (setup) + 33 (NOPs) = 38 machine cycles, plus the interrupt
    // entry's own 2-cycle overhead once it fires = 40 machine cycles.
    // Run a bit past that to be certain the vectoring instruction itself
    // (the NOP at 0x007) has also executed.
    h.cpu.Execute(41 * 15);
    const auto snap = h.cpu.Snapshot();

    Check(snap.timerIrqServiceCount >= 1, "timer irq: the timer-overflow interrupt was serviced at least once");
    Check(snap.pc >= 0x007 && snap.pc < 0x020,
          "timer irq: PC landed inside the timer-interrupt vector region (0x007+)");
}

void I8039ExternalIrqServicedAndRetrRestoresPsw() {
    FlatMemoryHarness h;
    // The external IRQ line is level-triggered (real hardware) - a
    // handler that never clears its own trigger would refire immediately
    // after RETR, forever. A real driver avoids this by clearing its own
    // IRQ via a Port 2 write - modeled here the same way: any Port 2
    // write clears the line.
    h.cpu.SetPort2Callbacks([&h]() { return h.p2; }, [&h](std::uint8_t value) {
        h.p2 = value;
        h.cpu.SetInputLine(I8039InputLine::Irq, I8039InputLineState::Clear);
    });

    // EN I (0x05,1cyc, enable external interrupts) - CheckIrqs() (polled
    // at the top of every instruction) then services the already-
    // asserted IRQ line on the very next iteration, vectoring to 0x003
    // (mcs48.cpp's own check_irqs()/execute_call(0x03)). At 0x003:
    // MOV A,#11h (0x23,0x11,2cyc) ; OUTL P2,A (0x39... no: OUTL P2,A is
    // 0x3A, 2cyc, clears the line via the callback above) ; RETR
    // (0x93,2cyc, restores PC+PSW and clears the in-progress flag).
    // Precise cost: EN I(1) + IRQ entry(2) + MOV A,#n(2) + OUTL P2,A(2) +
    // RETR(2) = 9 machine cycles = 135 clocks exactly.
    h.Load(0x000, {0x05, 0x00, 0x00, 0x23, 0x11, 0x3A, 0x93});
    h.Load(0x007, {0x00});  // NOP at the return address

    h.cpu.SetInputLine(I8039InputLine::Irq, I8039InputLineState::Assert);
    h.cpu.Execute(135);
    const auto snap = h.cpu.Snapshot();

    Check(snap.irqServiceCount == 1, "external irq: the external interrupt was serviced exactly once");
    Check(!snap.irqInProgress, "external irq: RETR cleared the in-progress flag");
    Check(snap.a == 0x11, "external irq: the handler's own MOV A,#11h actually ran");
    Check(snap.pc == 1, "external irq: RETR returned to right after EN I (address 1)");
}

void I8039MemoryBankSelectAffectsJump() {
    FlatMemoryHarness h;
    // SEL MB1 (0xF5, 1cyc, sets the a11 bank flip-flop to 0x800) ; JMP
    // 010h (0x04,0x10, page 0 target - but SEL MB1 means the actual
    // destination is 0x810, not 0x010, since JMP/CALL OR in the current
    // bank flip-flop - MAME mcs48.cpp's execute_jmp()).
    h.Load(0x000, {0xF5, 0x04, 0x10});
    h.program[0x810] = 0x00;  // NOP at the true (bank-1) target

    h.cpu.Execute(45);  // 1 + 2 = 3 machine cycles = 45 clocks
    const auto snap = h.cpu.Snapshot();

    Check(snap.pc == 0x810, "sel mb1: JMP landed at 0x810 (bank 1), not 0x010 (bank 0)");
}
