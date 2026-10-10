// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// IM68000Core + MAME-derived M68000 backend native tests. All
// programs below are hand-assembled synthetic 68000 machine code (no real
// ROM/game code), matching z80_adapter_tests.cpp.
//
// Expected register/flag/cycle values are derived independently from
// public, standard 68000 documentation (opcode encodings, exception frame
// layout, condition-code semantics) - not from reading back what the
// adapter itself computes - so these tests actually exercise correctness
// rather than just re-confirming the implementation against itself.

#include "m68000_adapter_tests.h"

#include "devices/cpu/m68000/mame_derived_m68000_adapter.h"
#include "test_support.h"

#include <array>
#include <cstdint>

namespace {

// A flat 64KiB synthetic memory space, wired to an IM68000Core via plain
// lambdas (no AddressSpace involved) - the simplest possible harness for
// exercising instruction semantics in isolation, matching
// z80_adapter_tests.cpp's own FlatMemoryHarness.
struct FlatMemoryHarness {
    std::array<std::uint8_t, 0x10000> memory{};
    MameDerivedM68000Adapter cpu;

    FlatMemoryHarness() {
        cpu.SetMemoryCallbacks([this](std::uint32_t addr) { return memory[addr & 0xFFFF]; },
                                [this](std::uint32_t addr, std::uint8_t value) { memory[addr & 0xFFFF] = value; });
    }

    void SetLong(std::uint32_t address, std::uint32_t value) {
        memory[address & 0xFFFF] = static_cast<std::uint8_t>(value >> 24);
        memory[(address + 1) & 0xFFFF] = static_cast<std::uint8_t>(value >> 16);
        memory[(address + 2) & 0xFFFF] = static_cast<std::uint8_t>(value >> 8);
        memory[(address + 3) & 0xFFFF] = static_cast<std::uint8_t>(value);
    }

    void SetWord(std::uint32_t address, std::uint16_t value) {
        memory[address & 0xFFFF] = static_cast<std::uint8_t>(value >> 8);
        memory[(address + 1) & 0xFFFF] = static_cast<std::uint8_t>(value & 0xFF);
    }

    std::uint32_t GetLong(std::uint32_t address) const {
        return (static_cast<std::uint32_t>(memory[address & 0xFFFF]) << 24) |
               (static_cast<std::uint32_t>(memory[(address + 1) & 0xFFFF]) << 16) |
               (static_cast<std::uint32_t>(memory[(address + 2) & 0xFFFF]) << 8) |
               static_cast<std::uint32_t>(memory[(address + 3) & 0xFFFF]);
    }

    std::uint16_t GetWord(std::uint32_t address) const {
        return static_cast<std::uint16_t>((static_cast<std::uint16_t>(memory[address & 0xFFFF]) << 8) |
                                           memory[(address + 1) & 0xFFFF]);
    }

    // Loads the reset vector (SSP/PC), a code sequence at `codeAddress`, and
    // performs Reset() - the standard setup every test below shares.
    void SetupReset(std::uint32_t ssp, std::uint32_t codeAddress, std::initializer_list<std::uint16_t> words) {
        SetLong(0, ssp);
        SetLong(4, codeAddress);
        std::uint32_t addr = codeAddress;
        for (std::uint16_t w : words) {
            SetWord(addr, w);
            addr += 2;
        }
        cpu.Reset();
    }
};

// CCR bit masks (public 68000 documentation).
constexpr std::uint16_t kCcrX = 0x10;
constexpr std::uint16_t kCcrN = 0x08;
constexpr std::uint16_t kCcrZ = 0x04;
constexpr std::uint16_t kCcrV = 0x02;
constexpr std::uint16_t kCcrC = 0x01;

}  // namespace

void M68000ResetVectorLoadsInitialSpAndPc() {
    std::printf("M68000ResetVectorLoadsInitialSpAndPc\n");
    FlatMemoryHarness h;
    h.SetLong(0, 0x00001000);
    h.SetLong(4, 0x00000400);
    h.cpu.Reset();
    auto snap = h.cpu.Snapshot();

    Check(snap.ssp == 0x00001000, "reset: SSP loaded from the long at address 0");
    Check(snap.pc == 0x00000400, "reset: PC loaded from the long at address 4");
    Check((snap.sr & 0x2000) != 0, "reset: S (supervisor) bit set");
    Check(((snap.sr >> 8) & 7) == 7, "reset: interrupt mask == 7 (all maskable interrupts disabled)");
    Check((snap.sr & 0x8000) == 0, "reset: T (trace) bit clear");
}

void M68000MoveqAndAddSetsFlagsCorrectly() {
    std::printf("M68000MoveqAndAddSetsFlagsCorrectly\n");
    FlatMemoryHarness h;
    // MOVEQ #$7F,D0 (0x707F) ; MOVE.W #$1234,D1 (0x323C,0x1234) ; ADD.W D0,D1 (0xD240)
    h.SetupReset(0x1000, 0x400, {0x707F, 0x323C, 0x1234, 0xD240});

    const std::uint64_t consumed = h.cpu.Execute(4 + 8 + 4);  // MOVEQ(4) + MOVE.W#imm(4+4) + ADD.W(4)
    auto snap = h.cpu.Snapshot();

    Check(consumed == 16, "moveq/add: Execute() consumed exactly the requested budget (3 whole instructions)");
    Check(snap.d[0] == 0x0000007F, "moveq/add: D0 == 0x7F (MOVEQ sign-extends but 0x7F is already positive)");
    Check((snap.d[1] & 0xFFFF) == 0x12B3, "moveq/add: D1 low word == 0x1234 + 0x7F == 0x12B3");
    Check((snap.sr & kCcrN) == 0, "moveq/add: N clear (0x12B3 is positive as a 16-bit value)");
    Check((snap.sr & kCcrZ) == 0, "moveq/add: Z clear (nonzero result)");
    Check((snap.sr & kCcrV) == 0, "moveq/add: V clear (no signed overflow)");
    Check((snap.sr & kCcrC) == 0, "moveq/add: C clear (no unsigned carry out of bit 15)");
    Check(snap.pc == 0x400 + 8, "moveq/add: PC advanced past all three instructions (2+4+2 bytes)");
}

void M68000SubtractionSetsCarryAndOverflow() {
    std::printf("M68000SubtractionSetsCarryAndOverflow\n");
    FlatMemoryHarness h;
    // MOVEQ #1,D0 (0x7001) ; MOVEQ #2,D1 (0x7202) ; SUB.W D1,D0 (0x9041)
    h.SetupReset(0x1000, 0x400, {0x7001, 0x7202, 0x9041});
    h.cpu.Execute(4 + 4 + 4);
    auto snap = h.cpu.Snapshot();

    Check((snap.d[0] & 0xFFFF) == 0xFFFF, "subtract: D0 low word == 1 - 2 == -1 == 0xFFFF");
    Check((snap.sr & kCcrN) != 0, "subtract: N set (result is negative as a 16-bit value)");
    Check((snap.sr & kCcrZ) == 0, "subtract: Z clear");
    Check((snap.sr & kCcrC) != 0, "subtract: C set (borrow occurred, 1 < 2)");
    Check((snap.sr & kCcrX) != 0, "subtract: X mirrors C for a plain SUB");
    Check((snap.sr & kCcrV) == 0, "subtract: V clear (both operands positive, no signed overflow)");
}

void M68000BranchAndSubroutineCallRoundTrips() {
    std::printf("M68000BranchAndSubroutineCallRoundTrips\n");
    FlatMemoryHarness h;
    // 0x400: BSR +4 (to 0x406)              0x6104
    // 0x402: MOVEQ #$11,D0 (return lands here) 0x7011
    // 0x404: NOP                             0x4E71
    // 0x406: MOVEQ #$22,D1 (subroutine)      0x7222
    // 0x408: RTS                             0x4E75
    h.SetupReset(0x1000, 0x400, {0x6104, 0x7011, 0x4E71, 0x7222, 0x4E75});

    const std::uint64_t consumed = h.cpu.Execute(18 + 4 + 16 + 4 + 4);  // BSR+MOVEQ(sub)+RTS+MOVEQ(main)+NOP
    auto snap = h.cpu.Snapshot();

    Check(consumed == 46, "bsr/rts: Execute() consumed exactly the requested budget (5 whole instructions)");
    Check(snap.d[0] == 0x11, "bsr/rts: D0 == 0x11 (set after RTS returned to 0x402)");
    Check(snap.d[1] == 0x22, "bsr/rts: D1 == 0x22 (set inside the subroutine at 0x406)");
    Check(snap.pc == 0x400 + 6, "bsr/rts: PC ends at 0x406 (right after the NOP at 0x404)");
    Check(snap.ssp == 0x1000, "bsr/rts: SSP back to its original value (BSR's push and RTS's pop net to zero)");
}

void M68000DbccLoopCountsDownCorrectly() {
    std::printf("M68000DbccLoopCountsDownCorrectly\n");
    FlatMemoryHarness h;
    // 0x400: MOVEQ #0,D0                      0x7000
    // 0x402: MOVEQ #3,D1                      0x7203
    // 0x404: loop: ADDQ.W #1,D0                0x5240
    // 0x406: DBF D1,loop (disp -4 to 0x404)    0x51C9, 0xFFFC
    // 0x40A: NOP                               0x4E71
    h.SetupReset(0x1000, 0x400, {0x7000, 0x7203, 0x5240, 0x51C9, 0xFFFC, 0x4E71});

    // setup(4+4) + 3 taken passes(ADDQ 4 + DBcc-taken 10)*3 + final pass
    // (ADDQ 4 + DBcc-not-taken 14) + NOP(4)
    const std::uint64_t budget = 4 + 4 + (4 + 10) * 3 + (4 + 14) + 4;
    const std::uint64_t consumed = h.cpu.Execute(budget);
    auto snap = h.cpu.Snapshot();

    Check(consumed == budget, "dbcc: Execute() consumed exactly the requested budget");
    Check(snap.d[0] == 4, "dbcc: D0 == 4 (ADDQ executed once per pass through the loop body, 4 passes total)");
    Check((snap.d[1] & 0xFFFF) == 0xFFFF, "dbcc: D1 low word == 0xFFFF (counter wrapped from 0 to -1, ending the loop)");
    Check(snap.pc == 0x40A + 2, "dbcc: PC ends right after the trailing NOP");
}

void M68000AutovectorLevel7InterruptTakesExpectedFrame() {
    std::printf("M68000AutovectorLevel7InterruptTakesExpectedFrame\n");
    FlatMemoryHarness h;
    // Level 7 autovector = vector 31, address 31*4 = 0x7C.
    h.SetLong(0x7C, 0x00000500);
    // 0x500: MOVE.W #$99,D0 (0x303C,0x0099) ; RTE (0x4E73, not reached this call)
    h.SetupReset(0x1000, 0x400, {0x4E71});  // NOP at the reset PC - never actually runs (interrupt preempts it)
    h.SetWord(0x500, 0x303C);
    h.SetWord(0x502, 0x0099);
    h.SetWord(0x504, 0x4E73);

    h.cpu.SetInterruptLevel(7);
    // 44 (exception overhead) + 8 (MOVE.W #imm,D0: base 4 + immediate EA extra 4) - stop before RTE runs,
    // so this Execute() call can't loop on the still-asserted level-7 line.
    const std::uint64_t consumed = h.cpu.Execute(44 + 8);
    auto snap = h.cpu.Snapshot();

    Check(consumed == 52, "autovector: Execute() consumed exactly the requested budget");
    Check(snap.interruptServiceCount == 1, "autovector: exactly one interrupt was serviced");
    Check(snap.d[0] == 0x99, "autovector: the handler's own MOVE.W #$99,D0 ran");
    Check(snap.pc == 0x504, "autovector: PC is at the handler's RTE (not yet executed)");
    Check(((snap.sr >> 8) & 7) == 7, "autovector: interrupt mask raised to the serviced level (7)");

    // The pushed exception frame: SSP was decremented by 6 (4-byte PC, then
    // 2-byte SR) from its original 0x1000.
    const std::uint32_t frameSp = snap.ssp;
    Check(frameSp == 0x1000 - 6, "autovector: SSP decremented by exactly 6 bytes (one 3-word frame)");
    Check(h.GetWord(frameSp) == 0x2700, "autovector: pushed SR == 0x2700 (supervisor, mask 7, CCR clear - the pre-exception state)");
    Check(h.GetLong(frameSp + 2) == 0x400, "autovector: pushed PC == 0x400 (where execution would have resumed)");
}

void M68000MaskedInterruptIsNotTakenUntilUnmasked() {
    std::printf("M68000MaskedInterruptIsNotTakenUntilUnmasked\n");
    FlatMemoryHarness h;
    // Level 3 autovector = vector 27, address 27*4 = 0x6C.
    h.SetLong(0x6C, 0x00000500);
    // 0x400: MOVE.W #$2000,SR (unmask everything: mask field = 0) - 0x46FC,0x2000 ; NOP - 0x4E71
    h.SetupReset(0x1000, 0x400, {0x46FC, 0x2000, 0x4E71});
    h.SetWord(0x500, 0x4E73);  // handler: just RTE

    h.cpu.SetInterruptLevel(3);
    // Default reset mask is 7, so level 3 stays masked through the first
    // instruction (MOVE #$2000,SR: base 12 + immediate-word EA extra 4 =
    // 16 cycles) - budget it exactly so this call stops right after MOVE
    // and before the interrupt check ahead of the next instruction fetch.
    h.cpu.Execute(16);
    auto snapBeforeUnmask = h.cpu.Snapshot();
    Check(snapBeforeUnmask.interruptServiceCount == 0, "masked interrupt: level 3 stayed masked while SR's mask was 7");
    Check(((snapBeforeUnmask.sr >> 8) & 7) == 0, "masked interrupt: SR mask is now 0 after MOVE #$2000,SR");
    Check(snapBeforeUnmask.pc == 0x404, "masked interrupt: PC is right after the MOVE #$2000,SR instruction");

    // Now that the mask is 0, the still-asserted level-3 line fires on the
    // very next CheckPendingInterrupt() - which, per Execute()'s own loop
    // structure, unconditionally runs one handler instruction (this
    // handler is just RTE) in the same iteration before re-checking the
    // budget, so this call fully round-trips into the handler and back out
    // via RTE (44 exception overhead + 20 for RTE = 64, exceeding the
    // requested 44-cycle budget by one indivisible instruction - the same
    // "runs a little over, never under" tolerance Execute() documents).
    h.cpu.Execute(44);
    auto snapAfterUnmask = h.cpu.Snapshot();
    Check(snapAfterUnmask.interruptServiceCount == 1, "masked interrupt: level 3 was serviced once the mask dropped to 0");
    Check(snapAfterUnmask.pc == 0x404, "masked interrupt: RTE completed within the same call, back at 0x404");
}

void M68000MovemPredecrementAndPostincrementRoundTrip() {
    std::printf("M68000MovemPredecrementAndPostincrementRoundTrip\n");
    FlatMemoryHarness h;
    // MOVEQ #$11,D0 ; MOVEQ #$22,D1 ; MOVE.L #$3000,A0 (setup) ;
    // MOVEM.L D0-D1,-(A0) ; MOVEM.L (A0)+,D2-D3
    //   MOVEM.L D0-D1,-(A0) = 0x48E0 (0100 1 0 001 1 100 000: d=0,s=1,mode=4,reg=A0). The
    //   -(An) form's register-list bits are reversed (bit0=A7..bit7=A0,bit8=D7..bit15=D0,
    //   real 68000 documented quirk) so D0/D1 are bits 15/14 => mask 0xC000.
    //   MOVEM.L (A0)+,D2-D3 = 0x4CD8 (0100 1 1 001 1 011 000: d=1,s=1,mode=3,reg=A0). The
    //   (An)+ form uses the natural order (bit0=D0..bit7=D7,bit8=A0..) so D2/D3 are bits
    //   2/3 => mask 0x000C.
    h.SetupReset(0x1000, 0x400,
                 {0x7011, 0x7222, 0x207C, 0x0000, 0x3000, 0x48E0, 0xC000, 0x4CD8, 0x000C});
    // MOVE.L #$3000,A0 is MOVEA.L #imm,A0: 0010 000 001 111 100 = 0x207C, imm32=0x00003000

    const std::uint64_t consumed =
        h.cpu.Execute(4 + 4 + 12 /*MOVEA.L #imm long-imm extra(8)+base(4)*/ + (8 + 2 * 8) + (8 + 2 * 8));
    (void)consumed;
    auto snap = h.cpu.Snapshot();

    Check(snap.a[0] == 0x3000, "movem: A0 == 0x3000 (predecrement then postincrement moved it back to the start)");
    Check(snap.d[2] == 0x11, "movem: D2 == D0's original value (0x11), round-tripped through memory");
    Check(snap.d[3] == 0x22, "movem: D3 == D1's original value (0x22), round-tripped through memory");
}

void M68000ShiftAndRotateMatchStandardSemantics() {
    std::printf("M68000ShiftAndRotateMatchStandardSemantics\n");
    {
        FlatMemoryHarness h;
        // MOVE.W #$4000,D0 ; ASL.W #1,D0 (shift left by immediate 1: 1110 001 1 01 000 000 = 0xE340)
        h.SetupReset(0x1000, 0x400, {0x303C, 0x4000, 0xE340});
        h.cpu.Execute(4 + 4 + 8);
        auto snap = h.cpu.Snapshot();
        Check((snap.d[0] & 0xFFFF) == 0x8000, "asl: 0x4000 << 1 == 0x8000");
        Check((snap.sr & kCcrV) != 0, "asl: V set (sign bit changed from 0 to 1 during the shift)");
        Check((snap.sr & kCcrC) == 0, "asl: C clear (the bit shifted out of bit 15 was 0)");
        Check((snap.sr & kCcrN) != 0, "asl: N set (0x8000 is negative as a 16-bit value)");
    }
    {
        FlatMemoryHarness h;
        // MOVE.W #$0001,D0 ; ROR.W #1,D0 (rotate right by immediate 1: 1110 001 0 01 011 000 = 0xE258)
        h.SetupReset(0x1000, 0x400, {0x303C, 0x0001, 0xE258});
        h.cpu.Execute(4 + 4 + 8);
        auto snap = h.cpu.Snapshot();
        Check((snap.d[0] & 0xFFFF) == 0x8000, "ror: 0x0001 rotated right by 1 == 0x8000 (the bit wraps around)");
        Check((snap.sr & kCcrC) != 0, "ror: C set (the bit rotated out, and back in, was 1)");
        Check((snap.sr & kCcrX) == 0, "ror: X unaffected by plain ROR (still clear from reset)");
    }
}

void M68000ExecuteIsDeterministic() {
    std::printf("M68000ExecuteIsDeterministic\n");
    FlatMemoryHarness a;
    FlatMemoryHarness b;
    // A short mixed program covering data movement, arithmetic, and a
    // DBcc loop.
    const std::initializer_list<std::uint16_t> program = {
        0x7003,          // MOVEQ #3,D0
        0x7200,          // MOVEQ #0,D1
        0x5241,          // ADDQ.W #1,D1   (loop:)
        0x51C8, 0xFFFC,  // DBF D0,loop
    };
    a.SetupReset(0x1000, 0x400, program);
    b.SetupReset(0x1000, 0x400, program);

    // 2 setup MOVEQs (4+4) + 4 loop passes (ADDQ 4 + DBcc-taken 10)*3 + final (ADDQ 4 + DBcc-not-taken 14)
    const std::uint64_t budget = 4 + 4 + (4 + 10) * 3 + (4 + 14);
    a.cpu.Execute(budget);
    b.cpu.Execute(budget);

    auto snapA = a.cpu.Snapshot();
    auto snapB = b.cpu.Snapshot();
    Check(snapA.d[0] == snapB.d[0] && snapA.d[1] == snapB.d[1] && snapA.pc == snapB.pc && snapA.sr == snapB.sr,
          "determinism: two fresh adapters running the same program reach the same state");
}

void M68000MovepTransfersOddBytesOnly() {
    std::printf("M68000MovepTransfersOddBytesOnly\n");
    FlatMemoryHarness h;
    // MOVE.L #$12345678,D0 (0x203C, 0x1234, 0x5678)
    // MOVEA.L #$5000,A1    (0x227C, 0x0000, 0x5000)
    // MOVEP.L D0,(0,A1)    (0x01C9, 0x0000) - Dy=D0(reg9=0), long reg->mem(opmode=111), Ax=A1(1)
    // MOVEP.L (0,A1),D2    (0x0549, 0x0000) - Dy=D2(reg9=2), long mem->reg(opmode=101), Ax=A1(1)
    // Encodings hand-derived from the official layout (bits11-9=Dy, bits8-6=
    // opmode, bits5-3=001 fixed, bits2-0=Ax); e.g. 0x09CE = MOVEP.L
    // D4,(d16,A6): reg9=4,opmode=111,Ax=6 under the same formula.
    h.SetupReset(0x1000, 0x400,
                 {0x203C, 0x1234, 0x5678, 0x227C, 0x0000, 0x5000, 0x01C9, 0x0000, 0x0549, 0x0000});

    // MOVE.L #imm,D0 (4+8 immediate-long EA) + MOVEA.L #imm,A1 (4+8) +
    // MOVEP.L D0,(0,A1) (24, long) + MOVEP.L (0,A1),D2 (24, long) - exact,
    // not generous: Execute()'s "runs one whole instruction past the
    // budget if there's room" behavior means a loose budget here would
    // decode trailing zero bytes as a real (and flag-clobbering) extra
    // instruction, same pitfall the BCD tests below hit first.
    const std::uint64_t consumed = h.cpu.Execute(12 + 12 + 24 + 24);
    auto snap = h.cpu.Snapshot();
    Check(consumed == 72, "movep: Execute() consumed exactly the requested budget (4 whole instructions)");

    Check(snap.a[1] == 0x5000, "movep: A1 == 0x5000 (MOVEA.L set it, MOVEP never modifies address registers)");
    Check(h.memory[0x5000] == 0x12, "movep: byte 3 (MSB) of D0 landed at EA+0");
    Check(h.memory[0x5002] == 0x34, "movep: byte 2 of D0 landed at EA+2");
    Check(h.memory[0x5004] == 0x56, "movep: byte 1 of D0 landed at EA+4");
    Check(h.memory[0x5006] == 0x78, "movep: byte 0 (LSB) of D0 landed at EA+6");
    Check(h.memory[0x5001] == 0 && h.memory[0x5003] == 0 && h.memory[0x5005] == 0 && h.memory[0x5007] == 0,
          "movep: the odd (in-between) bytes were never touched - only every other byte is part of the transfer");
    Check(snap.d[2] == 0x12345678, "movep: D2 == D0's original value, round-tripped through memory and back");
}

void M68000AddqSubqWordToAddressRegisterUsesFull32Bits() {
    std::printf("M68000AddqSubqWordToAddressRegisterUsesFull32Bits\n");
    // Real 68000 semantics (public documentation, "ADDQ"/"SUBQ" notes):
    // when the destination is an address register, the ENTIRE 32-bit
    // register participates in the add/subtract regardless of the encoded
    // operation size (.W or .L) - unlike every other ADDQ/SUBQ destination,
    // where the size field truncates the operand. This is the exact
    // instruction pattern (ADDQ.W #8,A6) used by a shared-RAM bulk-transfer loop.
    FlatMemoryHarness h;
    // MOVEA.L #$00010000,A6 (0x2C7C,0x0001,0x0000) - deliberately picks a
    // base address with a non-zero upper 16 bits.
    // ADDQ.W #8,A6           (0x504E) - data=8 (ddd=000), size=01(word),
    // mode=001(An direct), reg=110(A6). Cross-checked against the known-good
    // ADDQ.W #1,D1 encoding (0x5241) used elsewhere in this file.
    h.SetupReset(0x1000, 0x400, {0x2C7C, 0x0001, 0x0000, 0x504E});

    // MOVEA.L #imm,An (4 base + 8 immediate-long EA) + ADDQ.W #imm,An
    // (4 base + 0 EA extra, An-direct has no extra EA cycles) - exact budget.
    const std::uint64_t consumed = h.cpu.Execute(12 + 4);
    auto snap = h.cpu.Snapshot();
    Check(consumed == 16, "addq-to-an: Execute() consumed exactly the requested budget (2 whole instructions)");

    Check(snap.a[6] == 0x00010008,
          "addq-to-an: A6 == 0x00010008 (full 32-bit register advanced by 8 - "
          "a buggy word-truncating add would instead leave A6 == 0x00000008, "
          "silently discarding the upper 16 bits and derailing any loop that "
          "uses this addressing pattern to walk a >64KiB destination)");
}

void M68000AbcdSbcdNbcdPerformDecimalCorrection() {
    std::printf("M68000AbcdSbcdNbcdPerformDecimalCorrection\n");
    {
        // ABCD D1,D0: D0=0x09 (dest), D1=0x01 (src), X=0 -> BCD "09"+"01" == "10".
        // Dest=reg9=D0(0), src=reg=D1(1): 1100 000 100 000 001 = 0xC101.
        FlatMemoryHarness h;
        // MOVEQ #9,D0 ; MOVEQ #1,D1 ; ABCD D1,D0
        h.SetupReset(0x1000, 0x400, {0x7009, 0x7201, 0xC101});
        // Exact budget (MOVEQ 4 + MOVEQ 4 + ABCD 6 = 14) - see the MOVEP
        // test's own comment on why a loose budget is unsafe here.
        h.cpu.Execute(14);
        auto snap = h.cpu.Snapshot();
        Check((snap.d[0] & 0xFF) == 0x10, "abcd: BCD 09 + 01 == 10 (low-nibble carry propagated into the high digit)");
        Check((snap.sr & kCcrC) == 0 && (snap.sr & kCcrX) == 0, "abcd: no carry out (result fits in two BCD digits)");
        Check((snap.sr & kCcrN) == 0, "abcd: N clear (0x10's bit 7 is 0)");
    }
    {
        // SBCD D1,D0: D0=0x00 (dest), D1=0x01 (src), X=0 -> BCD "00"-"01" borrows: result 0x99, borrow out.
        // Dest=reg9=D0(0), src=reg=D1(1): 1000 000 100 000 001 = 0x8101.
        FlatMemoryHarness h;
        // MOVEQ #0,D0 ; MOVEQ #1,D1 ; SBCD D1,D0
        h.SetupReset(0x1000, 0x400, {0x7000, 0x7201, 0x8101});
        h.cpu.Execute(14);  // exact: MOVEQ 4 + MOVEQ 4 + SBCD 6
        auto snap = h.cpu.Snapshot();
        Check((snap.d[0] & 0xFF) == 0x99, "sbcd: BCD 00 - 01 borrows through both digits, landing on 99");
        Check((snap.sr & kCcrC) != 0 && (snap.sr & kCcrX) != 0, "sbcd: borrow out set (dest was less than src)");
        Check((snap.sr & kCcrN) != 0, "sbcd: N set (0x99's bit 7 is 1)");
    }
    {
        // NBCD D0: D0=0x01, X=1 (set via MOVE #imm,SR beforehand) ->
        // 0 - 01 - X(1) borrows through both digits, landing on 0x98.
        // Dn-direct EA (mode=0,reg=0): 0100 1000 00 000 000 = 0x4800.
        FlatMemoryHarness h;
        // MOVE.W #$2010,SR (S=1,mask=0,X=1) ; MOVEQ #1,D0 ; NBCD D0
        h.SetupReset(0x1000, 0x400, {0x46FC, 0x2010, 0x7001, 0x4800});
        h.cpu.Execute(16 + 4 + 6);  // exact: MOVE#imm,SR(12+4 immediate-word EA) + MOVEQ 4 + NBCD 6
        auto snap = h.cpu.Snapshot();
        Check(snap.d[0] == 0x00000098, "nbcd: 0 - 01 - X(1) == 98 (byte op - D0's upper 24 bits stay from the MOVEQ)");
        Check((snap.sr & kCcrC) != 0 && (snap.sr & kCcrX) != 0, "nbcd: borrow out set");
        Check((snap.sr & kCcrN) != 0, "nbcd: N set (0x98's bit 7 is 1)");
    }
}
