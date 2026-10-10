// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// IZ80Core + MAME-derived Z80 backend native tests. All programs
// below are hand-written synthetic byte sequences (no real ROM/game code).
//
// Expected register/flag/cycle values are derived independently from
// public, standard Z80 documentation (not from reading back what the
// adapter itself computes), so these tests actually exercise correctness
// rather than just re-confirming the implementation against itself.

#include "z80_adapter_tests.h"

#include "address_space.h"
#include "devices/cpu/z80/mame_derived_z80_adapter.h"
#include "memory_region.h"
#include "test_support.h"

#include <array>
#include <cstdint>
#include <vector>

namespace {

// Flag bit masks (public Z80 documentation - same values as
// devices/cpu/z80/z80_registers.h's kZ80Flag* constants, duplicated here
// so these tests state their expectations independently rather than by
// referencing the adapter's own constants).
constexpr std::uint8_t kFlagC = 0x01;
constexpr std::uint8_t kFlagN = 0x02;
constexpr std::uint8_t kFlagPV = 0x04;
constexpr std::uint8_t kFlagH = 0x10;
constexpr std::uint8_t kFlagZ = 0x40;
constexpr std::uint8_t kFlagS = 0x80;

// A flat 64KiB synthetic memory + 256-entry synthetic I/O space, wired to
// an IZ80Core via plain lambdas (no AddressSpace involved) - the simplest
// possible harness for exercising instruction semantics in isolation.
// Z80AddressSpaceIntegration separately proves the same adapter
// works when wired through a real AddressSpace/MemoryRegion instead.
struct FlatMemoryHarness {
    std::array<std::uint8_t, 0x10000> memory{};
    std::array<std::uint8_t, 0x100> ports{};
    MameDerivedZ80Adapter cpu;

    FlatMemoryHarness() {
        cpu.SetMemoryCallbacks(
            [this](std::uint16_t addr) { return memory[addr]; },
            [this](std::uint16_t addr, std::uint8_t value) { memory[addr] = value; });
        cpu.SetIoCallbacks([this](std::uint8_t port) { return ports[port]; },
                            [this](std::uint8_t port, std::uint8_t value) { ports[port] = value; });
    }

    void Load(std::uint16_t address, std::initializer_list<std::uint8_t> bytes) {
        std::uint16_t addr = address;
        for (std::uint8_t b : bytes) {
            memory[addr++] = b;
        }
    }
};

}  // namespace

void Z80BasicLoadAndCycleCounting() {
    FlatMemoryHarness h;
    // LD A,42h(7) ; LD B,10h(7) ; LD HL,1234h(10) ; NOP(4) = 28 cycles total
    h.Load(0x0000, {0x3E, 0x42, 0x06, 0x10, 0x21, 0x34, 0x12, 0x00});

    std::uint64_t consumed = h.cpu.Execute(28);
    auto snap = h.cpu.Snapshot();

    Check(consumed == 28, "basic load: Execute() consumed exactly the requested budget (4 whole instructions)");
    Check((snap.af >> 8) == 0x42, "basic load: A == 0x42");
    Check(((snap.bc >> 8) & 0xFF) == 0x10, "basic load: B == 0x10");
    Check(snap.hl == 0x1234, "basic load: HL == 0x1234");
    Check(snap.pc == 8, "basic load: PC advanced past all four instructions");
}

void Z80IncDecBoundaryFlags() {
    {
        FlatMemoryHarness h;
        // LD A,7Fh(7) ; INC A(4) -> A=0x80: S set, Z clear, H set, PV set (signed overflow), N clear
        h.Load(0x0000, {0x3E, 0x7F, 0x3C});
        h.cpu.Execute(11);
        auto snap = h.cpu.Snapshot();
        std::uint8_t a = snap.af >> 8;
        std::uint8_t f = snap.af & 0xFF;
        Check(a == 0x80, "inc boundary: 0x7F + 1 == 0x80");
        Check((f & kFlagS) != 0, "inc boundary: S set for 0x80 result");
        Check((f & kFlagZ) == 0, "inc boundary: Z clear (result nonzero)");
        Check((f & kFlagH) != 0, "inc boundary: H set (low nibble 0xF wrapped)");
        Check((f & kFlagPV) != 0, "inc boundary: PV set (0x7F -> 0x80 is signed overflow)");
        Check((f & kFlagN) == 0, "inc boundary: N clear after INC");
    }
    {
        FlatMemoryHarness h;
        // LD A,80h(7) ; DEC A(4) -> A=0x7F: PV set (signed overflow the other direction), N set
        h.Load(0x0000, {0x3E, 0x80, 0x3D});
        h.cpu.Execute(11);
        auto snap = h.cpu.Snapshot();
        std::uint8_t a = snap.af >> 8;
        std::uint8_t f = snap.af & 0xFF;
        Check(a == 0x7F, "dec boundary: 0x80 - 1 == 0x7F");
        Check((f & kFlagPV) != 0, "dec boundary: PV set (0x80 -> 0x7F is signed overflow)");
        Check((f & kFlagN) != 0, "dec boundary: N set after DEC");
    }
}

void Z80DjnzDoesNotAffectFlags() {
    FlatMemoryHarness h;
    // SCF(4, sets C) ; DJNZ +0(13, B: 1->0, branch not taken since B==0 after
    // dec... use B=2 so it loops once) - verify C flag survives untouched.
    // LD B,02h(7) ; SCF(4) ; DJNZ $+0(13, taken: B 2->1) ; DJNZ $+0(8, not
    // taken: B 1->0)
    h.Load(0x0000, {0x06, 0x02, 0x37, 0x10, 0xFE, 0x10, 0xFE});
    h.cpu.Execute(7 + 4 + 13 + 8);
    auto snap = h.cpu.Snapshot();
    std::uint8_t f = snap.af & 0xFF;
    std::uint8_t b = (snap.bc >> 8) & 0xFF;
    Check(b == 0, "djnz: B reached 0 after two DJNZ iterations");
    Check((f & kFlagC) != 0, "djnz: SCF's carry flag survives DJNZ untouched (DJNZ sets no flags)");
}

void Z80AddCarryOverflowFlags() {
    FlatMemoryHarness h;
    // LD A,80h(7) ; ADD A,A(4) -> 0x00, C set, Z set, PV set (signed overflow), H clear
    h.Load(0x0000, {0x3E, 0x80, 0x87});
    h.cpu.Execute(11);
    auto snap = h.cpu.Snapshot();
    std::uint8_t a = snap.af >> 8;
    std::uint8_t f = snap.af & 0xFF;
    Check(a == 0x00, "add overflow: 0x80 + 0x80 truncates to 0x00");
    Check((f & kFlagC) != 0, "add overflow: C set (carry out of bit 7)");
    Check((f & kFlagZ) != 0, "add overflow: Z set (result is zero)");
    Check((f & kFlagPV) != 0, "add overflow: PV set (-128 + -128 signed-overflows)");
    Check((f & kFlagH) == 0, "add overflow: H clear (no half-carry: 0+0)");
}

void Z80AddHl16BitFlags() {
    FlatMemoryHarness h;
    // LD HL,FFFFh(10) ; LD BC,0001h(10) ; ADD HL,BC(11) -> HL=0x0000, C set, H set
    h.Load(0x0000, {0x21, 0xFF, 0xFF, 0x01, 0x01, 0x00, 0x09});
    h.cpu.Execute(10 + 10 + 11);
    auto snap = h.cpu.Snapshot();
    std::uint8_t f = snap.af & 0xFF;
    Check(snap.hl == 0x0000, "add hl: 0xFFFF + 1 wraps to 0x0000");
    Check((f & kFlagC) != 0, "add hl: C set (carry out of bit 15)");
    Check((f & kFlagH) != 0, "add hl: H set (carry out of bit 11)");
}

void Z80SbcAdcHl16BitZeroFlag() {
    // SBC HL,dd / ADC HL,dd are 16-bit ops - Z must reflect the whole 16-bit
    // result, not just its low byte (SBC HL,DE of 0x0120 - 0x0020 = 0x0100
    // must leave Z clear).
    {
        FlatMemoryHarness h;
        // LD HL,0120h ; LD DE,0020h ; OR A ; SBC HL,DE -> 0x0100, Z MUST be clear
        h.Load(0x0000, {0x21, 0x20, 0x01, 0x11, 0x20, 0x00, 0xB7, 0xED, 0x52});
        h.cpu.Execute(10 + 10 + 4 + 15);
        auto snap = h.cpu.Snapshot();
        Check(snap.hl == 0x0100, "sbc hl: 0x0120 - 0x0020 = 0x0100");
        Check((snap.af & kFlagZ) == 0, "sbc hl: Z clear - result 0x0100 is not zero (high byte counts)");
    }
    {
        FlatMemoryHarness h;
        // LD HL,0100h ; LD DE,0100h ; OR A ; SBC HL,DE -> 0x0000, Z set
        h.Load(0x0000, {0x21, 0x00, 0x01, 0x11, 0x00, 0x01, 0xB7, 0xED, 0x52});
        h.cpu.Execute(10 + 10 + 4 + 15);
        auto snap = h.cpu.Snapshot();
        Check(snap.hl == 0x0000, "sbc hl: 0x0100 - 0x0100 = 0x0000");
        Check((snap.af & kFlagZ) != 0, "sbc hl: Z set - the full 16-bit result is zero");
    }
    {
        FlatMemoryHarness h;
        // LD HL,00FFh ; LD DE,0001h ; OR A ; ADC HL,DE (ED 5A) -> 0x0100, Z clear
        h.Load(0x0000, {0x21, 0xFF, 0x00, 0x11, 0x01, 0x00, 0xB7, 0xED, 0x5A});
        h.cpu.Execute(10 + 10 + 4 + 15);
        auto snap = h.cpu.Snapshot();
        Check(snap.hl == 0x0100, "adc hl: 0x00FF + 0x0001 = 0x0100");
        Check((snap.af & kFlagZ) == 0, "adc hl: Z clear - result 0x0100 is not zero");
    }
}

void Z80JumpSkipsInterveningCode() {
    FlatMemoryHarness h;
    // JP 0005h(10) ; [0003] LD A,FFh (never executed) ; [0005] LD A,01h(7)
    h.Load(0x0000, {0xC3, 0x05, 0x00, 0x3E, 0xFF, 0x3E, 0x01});
    h.cpu.Execute(17);
    auto snap = h.cpu.Snapshot();
    Check((snap.af >> 8) == 0x01, "jump: the skipped LD A,0xFF never executed");
    Check(snap.pc == 7, "jump: PC lands after the instruction at the jump target");
}

void Z80CallAndReturnRoundTrip() {
    // Default SP after Reset() is 0, which would corrupt the program on
    // PUSH - seed a real stack first.
    FlatMemoryHarness fresh;
    fresh.Load(0x0000, {0x31, 0x00, 0x80,               // LD SP,8000h (10)
                         0xCD, 0x08, 0x00,               // CALL 0008h (17)
                         0x76,                            // HALT (not reached directly)
                         0x00,                            // padding
                         0x3E, 0x2A,                       // [0008] LD A,2Ah (7)
                         0xC9});                           // [000A] RET (10)
    std::uint64_t consumed = fresh.cpu.Execute(10 + 17 + 7 + 10);
    auto snap = fresh.cpu.Snapshot();
    Check(consumed == 10 + 17 + 7 + 10, "call/ret: exact expected cycle total consumed");
    Check((snap.af >> 8) == 0x2A, "call/ret: subroutine's LD A,0x2A executed");
    Check(snap.pc == 0x0006, "call/ret: RET returned to the instruction right after CALL");
    Check(snap.sp == 0x8000, "call/ret: SP restored to its pre-CALL value");
}

void Z80PushPopAfRoundTripsFlags() {
    FlatMemoryHarness h;
    // LD SP,8000h(10) ; LD A,00h(7) ; INC A(4, A=1, S=0 Z=0 H=0 PV=0 N=0
    // C=0(from reset)) ; PUSH AF(11) ; LD A,00h(7, clobber) ; POP AF(10)
    h.Load(0x0000, {0x31, 0x00, 0x80, 0x3E, 0x00, 0x3C, 0xF5, 0x3E, 0x00, 0xF1});
    h.cpu.Execute(10 + 7 + 4 + 11 + 7 + 10);
    auto snap = h.cpu.Snapshot();
    Check((snap.af >> 8) == 0x01, "push/pop af: A restored to 1 after POP");
    Check((snap.af & 0xFF) == 0x00, "push/pop af: F byte round-trips exactly (S=Z=H=PV=N=C=0 after INC 0->1)");
    Check(snap.sp == 0x8000, "push/pop af: SP restored after matching PUSH/POP");
}

// The standard Z80 ED-prefixed opcode table (public documentation) has
// 0xED 0x57 = LD A,I and 0xED 0x5F = LD A,R. Drivers that use I as
// general-purpose scratch storage depend on LD A,I reading I (not R, which
// increments on every opcode fetch).
void Z80LdAiAndLdArReadTheCorrectRegister() {
    FlatMemoryHarness h;
    // LD A,99h(7) ; LD I,A(9, I=99h) ; LD A,11h(7) ; LD R,A(9, R low7=11h) ;
    // LD A,00h(7, clobber+R++) ; LD A,I(9, A should become I=99h, not R)
    h.Load(0x0000, {0x3E, 0x99, 0xED, 0x47, 0x3E, 0x11, 0xED, 0x4F, 0x3E, 0x00, 0xED, 0x57});
    h.cpu.Execute(7 + 9 + 7 + 9 + 7 + 9);
    auto snap1 = h.cpu.Snapshot();
    Check((snap1.af >> 8) == 0x99, "ld a,i: A reads the I register (0x99), not R");
    Check((snap1.af & kFlagH) == 0 && (snap1.af & kFlagN) == 0,
          "ld a,i: H and N are cleared (public Z80 documentation's own flag table)");

    // LD A,00h(7, clobber) ; LD A,R(9, A should become R's current value,
    // which has been incrementing on every opcode fetch since it was set to
    // 0x11h - definitely not I's 0x99h).
    h.Load(0x000C, {0x3E, 0x00, 0xED, 0x5F});
    h.cpu.Execute(7 + 9);
    auto snap2 = h.cpu.Snapshot();
    Check((snap2.af >> 8) != 0x99, "ld a,r: A reads the R register, not I (would be 0x99 if still swapped)");
}

void Z80CbBitSetResOnMemory() {
    FlatMemoryHarness h;
    // LD HL,8000h(10) ; LD (HL),00h(10) ; BIT 0,(HL)(12, Z set: bit clear) ;
    // SET 0,(HL)(15) ; BIT 0,(HL)(12, Z clear: bit now set) ; RES 0,(HL)(15)
    h.Load(0x0000, {0x21, 0x00, 0x80, 0x36, 0x00, 0xCB, 0x46, 0xCB, 0xC6, 0xCB, 0x46, 0xCB, 0x86});

    std::uint64_t c1 = h.cpu.Execute(10 + 10 + 12);
    Check((h.cpu.Snapshot().af & kFlagZ) != 0, "cb bit: BIT 0,(HL) sets Z when the bit is 0");
    Check(c1 == 32, "cb bit: cycle total through the first BIT test");

    h.cpu.Execute(15);
    Check(h.memory[0x8000] == 0x01, "cb bit: SET 0,(HL) sets bit 0 in memory");

    h.cpu.Execute(12);
    Check((h.cpu.Snapshot().af & kFlagZ) == 0, "cb bit: BIT 0,(HL) clears Z once the bit is set");

    h.cpu.Execute(15);
    Check(h.memory[0x8000] == 0x00, "cb bit: RES 0,(HL) clears bit 0 in memory");
}

void Z80IndexedIxLoadAndStore() {
    FlatMemoryHarness h;
    // LD IX,8000h(DD 21, 14) ; LD (IX+2),5Ah(DD 36 02 5A, 19) ;
    // LD A,(IX+2)(DD 7E 02, 19)
    h.Load(0x0000, {0xDD, 0x21, 0x00, 0x80, 0xDD, 0x36, 0x02, 0x5A, 0xDD, 0x7E, 0x02});
    h.cpu.Execute(14 + 19 + 19);
    auto snap = h.cpu.Snapshot();
    Check(snap.ix == 0x8000, "indexed ix: IX loaded correctly");
    Check(h.memory[0x8002] == 0x5A, "indexed ix: LD (IX+2),n wrote to the displaced address");
    Check((snap.af >> 8) == 0x5A, "indexed ix: LD A,(IX+2) read back the same displaced address");
}

void Z80IndexedLoadStoreKeepsRealHlOperand() {
    // Under a DD/FD prefix only the (HL) slot becomes (IX+d) - the *register*
    // operand of LD r,(IX+d) / LD (IX+d),r stays the true B/C/D/E/H/L/A.
    // LD L,(IX+d) must write the real L (not IXL) and leave IX untouched;
    // likewise LD H,(IX+d), and the store forms.
    FlatMemoryHarness h;
    h.Load(0x0000, {
        0xDD, 0x21, 0x00, 0x90,  // LD IX,9000h        (14)
        0xDD, 0x36, 0x05, 0xAA,  // LD (IX+5),AAh      (19)
        0xDD, 0x36, 0x06, 0xBB,  // LD (IX+6),BBh      (19)
        0x21, 0x34, 0x12,        // LD HL,1234h        (10)
        0xDD, 0x6E, 0x05,        // LD L,(IX+5)        (19)  -> L=AA, IX stays 9000h
        0xDD, 0x66, 0x06,        // LD H,(IX+6)        (19)  -> H=BB, IX stays 9000h
        0xDD, 0x75, 0x09,        // LD (IX+9),L        (19)  -> mem[9009]=AA (real L)
        0xDD, 0x74, 0x0A,        // LD (IX+0Ah),H      (19)  -> mem[900A]=BB (real H)
    });
    h.cpu.Execute(14 + 19 + 19 + 10 + 19 + 19 + 19 + 19);
    auto snap = h.cpu.Snapshot();
    Check(snap.ix == 0x9000, "indexed hl-operand: IX is not clobbered by LD L,(IX+d) / LD H,(IX+d)");
    Check(snap.hl == 0xBBAA, "indexed hl-operand: the real H/L receive the (IX+d) bytes");
    Check(h.memory[0x9009] == 0xAA, "indexed hl-operand: LD (IX+d),L stores the real L");
    Check(h.memory[0x900A] == 0xBB, "indexed hl-operand: LD (IX+d),H stores the real H");
}

void Z80NmiServicedCorrectly() {
    FlatMemoryHarness h;
    h.Load(0x0000, {0x31, 0x00, 0x80});  // LD SP,8000h (10)
    h.cpu.Execute(10);
    Check(h.cpu.Snapshot().pc == 3, "nmi: setup instruction executed before asserting NMI");

    h.cpu.SetInputLine(Z80InputLine::Nmi, Z80InputLineState::Assert);
    std::uint64_t consumed = h.cpu.Execute(11);
    auto snap = h.cpu.Snapshot();

    Check(consumed == 11, "nmi: acknowledge costs exactly 11 T-states");
    Check(snap.pc == 0x0066, "nmi: PC jumped to the fixed NMI vector 0x0066");
    Check(snap.sp == 0x7FFE, "nmi: return address pushed (SP -= 2)");
    Check(h.memory[0x7FFE] == 0x03 && h.memory[0x7FFF] == 0x00,
          "nmi: pushed return address is the pre-NMI PC (0x0003)");
    Check(!snap.iff1, "nmi: IFF1 cleared on acknowledge");
}

void Z80MaskableInterruptImOneWithEiDelay() {
    FlatMemoryHarness h;
    // LD SP,8000h(10) ; IM 1(ED 56, 8) ; EI(4) ; NOP(4, protected by EI's
    // one-instruction delay even though IRQ is already asserted) ;
    // [would-be second NOP at 0007, hijacked by the IRQ ack instead]
    h.Load(0x0000, {0x31, 0x00, 0x80, 0xED, 0x56, 0xFB, 0x00, 0x00});
    h.cpu.SetInputLine(Z80InputLine::Irq, Z80InputLineState::Assert);

    std::uint64_t consumed = h.cpu.Execute(10 + 8 + 4 + 4 + 13);
    auto snap = h.cpu.Snapshot();

    Check(consumed == 10 + 8 + 4 + 4 + 13, "im1+ei delay: exact expected cycle total");
    Check(snap.pc == 0x0038, "im1+ei delay: PC jumped to the fixed IM 1 vector 0x0038");
    Check(!snap.iff1, "im1+ei delay: IFF1 cleared on acknowledge");
    Check(h.memory[0x7FFE] == 0x07 && h.memory[0x7FFF] == 0x00,
          "im1+ei delay: pushed return address is right after the NOP that DID execute "
          "(the EI-protected one), not before it");
}

void Z80MaskableInterruptImTwoVectorTable() {
    FlatMemoryHarness h;
    // LD SP,8000h(10) ; LD A,90h(7) ; LD I,A(ED 47, 9) ; IM 2(ED 5E, 8) ;
    // EI(4) ; NOP(4, EI-protected) ; [next opcode slot hijacked by IRQ ack]
    h.Load(0x0000, {0x31, 0x00, 0x80, 0x3E, 0x90, 0xED, 0x47, 0xED, 0x5E, 0xFB, 0x00});
    // Vector table entry at (I<<8)|ackByte = 0x9000|0x10 = 0x9010, pointing
    // at a target address (0x7000) that is never itself executed - the
    // test only checks the CPU actually jumped there.
    h.memory[0x9010] = 0x00;
    h.memory[0x9011] = 0x70;
    h.cpu.SetInterruptAcknowledgeCallback([]() -> std::uint32_t { return 0x10; });
    h.cpu.SetInputLine(Z80InputLine::Irq, Z80InputLineState::Assert);

    std::uint64_t consumed = h.cpu.Execute(10 + 7 + 9 + 8 + 4 + 4 + 19);
    auto snap = h.cpu.Snapshot();

    Check(consumed == 10 + 7 + 9 + 8 + 4 + 4 + 19, "im2: exact expected cycle total (ack costs 19)");
    Check(snap.i == 0x90, "im2: I register loaded correctly");
    Check(snap.im == 2, "im2: interrupt mode set to 2");
    Check(snap.pc == 0x7000, "im2: PC jumped to the address read from the vector table at (I<<8)|ackByte");
}

// ---------------------------------------------------------------------
// IM 0, matching MAME's take_interrupt macro exactly (z80.lst) - only CALL nnnn/JP nnnn/RST xx/EI are
// special-cased, everything else is a no-op. Every cycle-budget/expected-state number below was derived by hand
// by walking z80.lst's macro (its "+N" literals plus the implicit 6
// T-states each @wm16_sp/@rm16 sub-macro costs for a push/vector-table
// read).
//
// All five share the same setup program: LD SP,8000h(10) ; IM 0(ED 46, 8) ;
// EI(4) ; NOP(4, EI-protected) ; [next opcode slot hijacked by the IRQ ack
// instead of a normal fetch] - same shape as the IM1/IM2 tests above, so
// the pre-ack return address pushed for CALL/RST is always 0x0007 (right
// after the EI-protected NOP).

void Z80MaskableInterruptImZeroCall() {
    FlatMemoryHarness h;
    h.Load(0x0000, {0x31, 0x00, 0x80, 0xED, 0x46, 0xFB, 0x00});
    // Packed vector: opcode 0xCD (CALL) in bits 16-23, address 0x1234 in
    // bits 0-15 - the two address bytes never touch program memory/PC.
    h.cpu.SetInterruptAcknowledgeCallback([]() -> std::uint32_t { return 0xCD1234; });
    h.cpu.SetInputLine(Z80InputLine::Irq, Z80InputLineState::Assert);

    std::uint64_t consumed = h.cpu.Execute(10 + 8 + 4 + 4 + 19);
    auto snap = h.cpu.Snapshot();

    Check(consumed == 10 + 8 + 4 + 4 + 19, "im0 CALL: exact expected cycle total (ack costs 19)");
    Check(snap.pc == 0x1234, "im0 CALL: PC jumped to the address supplied by the ack callback");
    Check(snap.sp == 0x7FFE, "im0 CALL: return address pushed (SP -= 2)");
    Check(h.memory[0x7FFE] == 0x07 && h.memory[0x7FFF] == 0x00,
          "im0 CALL: pushed return address is the pre-ack PC (0x0007)");
    Check(snap.im0ServiceCount == 1, "im0 CALL: counted as one IM0 service");
    Check(snap.im0UnhandledServiceCount == 0, "im0 CALL: not counted as unhandled");
}

void Z80MaskableInterruptImZeroJp() {
    FlatMemoryHarness h;
    h.Load(0x0000, {0x31, 0x00, 0x80, 0xED, 0x46, 0xFB, 0x00});
    // Packed vector: opcode 0xC3 (JP) in bits 16-23, address 0x5678 in bits 0-15.
    h.cpu.SetInterruptAcknowledgeCallback([]() -> std::uint32_t { return 0xC35678; });
    h.cpu.SetInputLine(Z80InputLine::Irq, Z80InputLineState::Assert);

    std::uint64_t consumed = h.cpu.Execute(10 + 8 + 4 + 4 + 12);
    auto snap = h.cpu.Snapshot();

    Check(consumed == 10 + 8 + 4 + 4 + 12, "im0 JP: exact expected cycle total (ack costs 12)");
    Check(snap.pc == 0x5678, "im0 JP: PC jumped to the address supplied by the ack callback");
    Check(snap.sp == 0x8000, "im0 JP: SP unchanged (JP does not push a return address)");
    Check(snap.im0ServiceCount == 1, "im0 JP: counted as one IM0 service");
    Check(snap.im0UnhandledServiceCount == 0, "im0 JP: not counted as unhandled");
}

void Z80MaskableInterruptImZeroRst() {
    FlatMemoryHarness h;
    h.Load(0x0000, {0x31, 0x00, 0x80, 0xED, 0x46, 0xFB, 0x00});
    // 0xD7 = RST 10h (opcode pattern 11xxx111, xx=010 -> target 0x0010).
    h.cpu.SetInterruptAcknowledgeCallback([]() -> std::uint32_t { return 0xD7; });
    h.cpu.SetInputLine(Z80InputLine::Irq, Z80InputLineState::Assert);

    std::uint64_t consumed = h.cpu.Execute(10 + 8 + 4 + 4 + 13);
    auto snap = h.cpu.Snapshot();

    Check(consumed == 10 + 8 + 4 + 4 + 13, "im0 RST: exact expected cycle total (ack costs 13)");
    Check(snap.pc == 0x0010, "im0 RST: PC jumped to the RST 10h target");
    Check(snap.sp == 0x7FFE, "im0 RST: return address pushed (SP -= 2)");
    Check(h.memory[0x7FFE] == 0x07 && h.memory[0x7FFF] == 0x00,
          "im0 RST: pushed return address is the pre-ack PC (0x0007)");
    Check(snap.im0ServiceCount == 1, "im0 RST: counted as one IM0 service");
    Check(snap.im0UnhandledServiceCount == 0, "im0 RST: not counted as unhandled");
}

void Z80MaskableInterruptImZeroEi() {
    FlatMemoryHarness h;
    h.Load(0x0000, {0x31, 0x00, 0x80, 0xED, 0x46, 0xFB, 0x00});
    h.cpu.SetInterruptAcknowledgeCallback([]() -> std::uint32_t { return 0xFB; });
    h.cpu.SetInputLine(Z80InputLine::Irq, Z80InputLineState::Assert);

    std::uint64_t consumed = h.cpu.Execute(10 + 8 + 4 + 4 + 6);
    auto snap = h.cpu.Snapshot();

    Check(consumed == 10 + 8 + 4 + 4 + 6, "im0 EI: exact expected cycle total (ack costs 6)");
    Check(snap.pc == 0x0007, "im0 EI: PC unchanged (EI does not jump)");
    Check(snap.sp == 0x8000, "im0 EI: SP unchanged (EI does not push)");
    Check(snap.iff1 && snap.iff2, "im0 EI: both interrupt flip-flops set, same as the normal 0xFB opcode");
    Check(snap.im0ServiceCount == 1, "im0 EI: counted as one IM0 service");
    Check(snap.im0UnhandledServiceCount == 0, "im0 EI: not counted as unhandled");
}

void Z80MaskableInterruptImZeroUnhandledByteDoesNothing() {
    // A byte matching none of CALL/JP/RST/EI is silently ignored -
    // checked against two representative values: an ordinary opcode byte
    // (0x3E, LD A,n) and 0x00, which MAME's take_interrupt macro happens to
    // early-out on explicitly (functionally identical to falling through
    // the other patterns, but worth covering since MAME's own source
    // treats it as a distinct case).
    for (std::uint32_t vector : {0x3Eu, 0x00u}) {
        FlatMemoryHarness h;
        h.Load(0x0000, {0x31, 0x00, 0x80, 0xED, 0x46, 0xFB, 0x00});
        h.cpu.SetInterruptAcknowledgeCallback([vector]() -> std::uint32_t { return vector; });
        h.cpu.SetInputLine(Z80InputLine::Irq, Z80InputLineState::Assert);

        std::uint64_t consumed = h.cpu.Execute(10 + 8 + 4 + 4 + 2);
        auto snap = h.cpu.Snapshot();

        Check(consumed == 10 + 8 + 4 + 4 + 2,
              "im0 unhandled: exact expected cycle total (base ack latency only, no extra)");
        Check(snap.pc == 0x0007, "im0 unhandled: PC unchanged - no instruction was actually executed");
        Check(snap.sp == 0x8000, "im0 unhandled: SP unchanged - nothing was pushed");
        Check(snap.im0ServiceCount == 1, "im0 unhandled: still counted as one IM0 service attempt");
        Check(snap.im0UnhandledServiceCount == 1, "im0 unhandled: counted as unhandled");
    }
}

void Z80HaltWakesOnInterrupt() {
    FlatMemoryHarness h;
    h.Load(0x0000, {0x76});  // HALT
    h.cpu.Execute(4);
    Check(h.cpu.Snapshot().halted, "halt: CPU enters the halted state");
    Check(h.cpu.Snapshot().pc == 1, "halt: PC already advanced past the HALT opcode itself");

    // Spin a few more cycles while halted: PC must not move further.
    h.cpu.Execute(8);
    Check(h.cpu.Snapshot().pc == 1, "halt: PC stays put while halted (no memory access to advance it)");

    h.cpu.SetInputLine(Z80InputLine::Nmi, Z80InputLineState::Assert);
    std::uint64_t consumed = h.cpu.Execute(11);
    auto snap = h.cpu.Snapshot();
    Check(consumed == 11, "halt: waking via NMI costs the normal 11-cycle NMI acknowledge");
    Check(!snap.halted, "halt: no longer halted after NMI");
    Check(snap.pc == 0x0066, "halt: PC jumped to the NMI vector");
    Check(h.memory[snap.sp] == 0x01, "halt: the pushed return address is right after HALT (resumes past it)");
}

void Z80AddressSpaceIntegration() {
    // Wires IZ80Core's memory callbacks through an actual
    // AddressSpace/MemoryRegion, not a bare test array, proving the
    // adapter is usable the way a real Machine will use it.
    MemoryRegion ram(0x10000);
    AddressSpace space(
        [](std::uint64_t) -> std::uint8_t { return 0xFF; },
        [](std::uint64_t, std::uint8_t) {});
    space.Map(0x0000, 0x10000, &ram);

    // LD A,99h(7) ; LD (2000h),A(13) ; LD B,(2000h)... not a real opcode -
    // use LD A,n then LD (nn),A then LD HL,2000h; LD C,(HL) instead.
    std::uint8_t* rom = ram.RawData();
    const std::vector<std::uint8_t> program = {0x3E, 0x99, 0x32, 0x00, 0x20, 0x21, 0x00, 0x20, 0x4E};
    for (std::size_t i = 0; i < program.size(); ++i) {
        rom[i] = program[i];
    }

    MameDerivedZ80Adapter cpu;
    cpu.SetMemoryCallbacks([&space](std::uint16_t addr) { return space.Read8(addr); },
                            [&space](std::uint16_t addr, std::uint8_t value) { space.Write8(addr, value); });

    // LD A,99h(7) ; LD (2000h),A(13) ; LD HL,2000h(10) ; LD C,(HL)(7)
    cpu.Execute(7 + 13 + 10 + 7);
    auto snap = cpu.Snapshot();
    Check((snap.bc & 0xFF) == 0x99, "address space: value written via AddressSpace round-trips back through it");
}

void Z80ExecuteIsDeterministic() {
    FlatMemoryHarness a;
    FlatMemoryHarness b;
    const std::initializer_list<std::uint8_t> program = {0x3E, 0x01, 0x06, 0x02, 0x80, 0x21, 0x00, 0x10,
                                                           0x09, 0xCB, 0x00, 0x00};
    a.Load(0x0000, program);
    b.Load(0x0000, program);

    a.cpu.Execute(60);
    b.cpu.Execute(60);

    auto snapA = a.cpu.Snapshot();
    auto snapB = b.cpu.Snapshot();
    Check(snapA.af == snapB.af && snapA.bc == snapB.bc && snapA.hl == snapB.hl && snapA.pc == snapB.pc,
          "determinism: two fresh adapters running the same program reach the same state");
}
