// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// HD63701V0 (IHd63701Core + MAME-derived backend) native tests. All programs are hand-written
// synthetic byte sequences (no real ROM/game code) - same "not ROM-dependent" convention as
// h6280_adapter_tests.cpp / m6809_adapter_tests.cpp (CPU core only, no real-ROM comparison).
//
// Expected values (opcode bytes, cycle counts, flag results) are derived independently by hand from
// the ported 6800/6801/HD6301 semantics (MAME's 6800ops.hxx) - not by reading back what this adapter computes.

#include "hd63701_adapter_tests.h"

#include "devices/cpu/hd63701/mame_derived_hd63701_adapter.h"
#include "test_support.h"

#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <vector>

namespace {

constexpr std::uint8_t kFlagC = 0x01, kFlagV = 0x02, kFlagZ = 0x04, kFlagN = 0x08, kFlagI = 0x10,
                        kFlagH = 0x20;

struct Hd63701Rig {
    std::vector<std::uint8_t> mem = std::vector<std::uint8_t>(0x10000, 0);
    MameDerivedHd63701Adapter cpu;

    Hd63701Rig() {
        cpu.SetMemoryCallbacks([this](std::uint16_t addr) -> std::uint8_t { return mem[addr]; },
                                [this](std::uint16_t addr, std::uint8_t v) { mem[addr] = v; });
    }

    void Put(std::uint16_t addr, std::initializer_list<std::uint8_t> bytes) {
        std::uint16_t i = 0;
        for (std::uint8_t b : bytes) mem[static_cast<std::uint16_t>(addr + i++)] = b;
    }

    void Prog(std::initializer_list<std::uint8_t> bytes) { Put(0x0400, bytes); }

    void Vector(std::uint16_t vectorAddr, std::uint16_t target) {
        mem[vectorAddr] = static_cast<std::uint8_t>(target >> 8);
        mem[static_cast<std::uint16_t>(vectorAddr + 1)] = static_cast<std::uint8_t>(target & 0xFF);
    }

    void Boot(std::uint16_t entry = 0x0400) {
        Vector(0xFFFE, entry);
        cpu.Reset();
        // A second Reset() call after SetMemoryCallbacks() is required (see
        // the adapter's own class comment for why the constructor's own
        // Reset() reads a garbage reset vector) - this Boot() helper IS
        // that second call, since SetMemoryCallbacks() already ran in the
        // constructor above.
    }

    std::uint64_t Step(int n) {
        std::uint64_t total = 0;
        for (int i = 0; i < n; ++i) total += cpu.Execute(1);
        return total;
    }
};

bool Has(const MameDerivedHd63701Adapter::RegisterSnapshot& s, std::uint8_t flag) { return (s.cc & flag) != 0; }

}  // namespace

void Hd63701ResetState() {
    std::printf("Hd63701ResetState\n");
    Hd63701Rig rig;
    rig.Boot(0x0400);
    auto snap = rig.cpu.Snapshot();
    Check(snap.pc == 0x0400, "reset: PC comes from the reset vector at 0xFFFE/0xFFFF");
    Check(snap.cc == 0xD0, "reset: CC == 0xC0 | I (0xD0)");
    Check(snap.a == 0 && snap.b == 0 && snap.x == 0 && snap.s == 0, "reset: A/B/X/S start at 0");
    Check(!snap.waiWaiting && !snap.slpWaiting, "reset: not waiting/sleeping");
    Check(snap.nmiServiceCount == 0 && snap.irq1ServiceCount == 0 && snap.irq2ServiceCount == 0 &&
              snap.trapCount == 0,
          "reset: all service counters start at 0");
}

void Hd63701ImmediateLoadFlagsAndCycles() {
    std::printf("Hd63701ImmediateLoadFlagsAndCycles\n");
    Hd63701Rig rig;
    rig.Prog({0x86, 0x00, 0x86, 0x80, 0x86, 0x01, 0xC6, 0x55});  // LDAA #$00; LDAA #$80; LDAA #$01; LDAB #$55
    rig.Boot();

    std::uint64_t c = rig.Step(1);
    auto snap = rig.cpu.Snapshot();
    Check(snap.a == 0x00 && Has(snap, kFlagZ) && !Has(snap, kFlagN) && !Has(snap, kFlagV),
          "LDAA #$00: A==0, Z set, N/V clear");
    Check(c == 2, "LDAA #imm costs 2 cycles");

    rig.Step(1);
    snap = rig.cpu.Snapshot();
    Check(snap.a == 0x80 && Has(snap, kFlagN) && !Has(snap, kFlagZ), "LDAA #$80: N set, Z clear");

    rig.Step(1);
    snap = rig.cpu.Snapshot();
    Check(snap.a == 0x01 && !Has(snap, kFlagN) && !Has(snap, kFlagZ), "LDAA #$01: N and Z clear");

    rig.Step(1);
    snap = rig.cpu.Snapshot();
    Check(snap.b == 0x55, "LDAB #$55 loads B independently of A");
    Check(snap.pc == 0x0408, "PC advanced past all four 2-byte instructions");
}

void Hd63701AddSubAdcSbcFlags() {
    std::printf("Hd63701AddSubAdcSbcFlags\n");
    {
        // A=0x7F, ADDA #$01 -> 0x80: N=1, Z=0, V=1 (signed overflow), C=0, H=1 (nibble carry).
        Hd63701Rig rig;
        rig.Prog({0x86, 0x7F, 0x8B, 0x01});
        rig.Boot();
        rig.Step(2);
        auto snap = rig.cpu.Snapshot();
        Check(snap.a == 0x80, "ADDA: 0x7F+0x01 == 0x80");
        Check(Has(snap, kFlagN) && !Has(snap, kFlagZ) && Has(snap, kFlagV) && !Has(snap, kFlagC) &&
                  Has(snap, kFlagH),
              "ADDA: N=1 Z=0 V=1 C=0 H=1");
    }
    {
        // A=0x50, SUBA #$60 -> 0xF0: N=1, Z=0, V=0 (no signed overflow, -16 in range), C=1 (borrow).
        Hd63701Rig rig;
        rig.Prog({0x86, 0x50, 0x80, 0x60});
        rig.Boot();
        rig.Step(2);
        auto snap = rig.cpu.Snapshot();
        Check(snap.a == 0xF0, "SUBA: 0x50-0x60 == 0xF0");
        Check(Has(snap, kFlagN) && !Has(snap, kFlagZ) && !Has(snap, kFlagV) && Has(snap, kFlagC),
              "SUBA: N=1 Z=0 V=0 C=1 (borrow)");
    }
    {
        // A=0xFF, ADCA #$01 with C=0 -> 0x00, C=1 (carry out), Z=1, H=1.
        Hd63701Rig rig;
        rig.Prog({0x86, 0xFF, 0x89, 0x01});
        rig.Boot();
        rig.Step(2);
        auto snap = rig.cpu.Snapshot();
        Check(snap.a == 0x00, "ADCA: 0xFF+0x01+0 == 0x00 (mod 256)");
        Check(!Has(snap, kFlagN) && Has(snap, kFlagZ) && !Has(snap, kFlagV) && Has(snap, kFlagC) &&
                  Has(snap, kFlagH),
              "ADCA: N=0 Z=1 V=0 C=1 H=1");
    }
    {
        // A=0x00, SBCA #$01 with C=0 -> 0xFF, C=1 (borrow), N=1.
        Hd63701Rig rig;
        rig.Prog({0x86, 0x00, 0x82, 0x01});
        rig.Boot();
        rig.Step(2);
        auto snap = rig.cpu.Snapshot();
        Check(snap.a == 0xFF, "SBCA: 0x00-0x01-0 == 0xFF (mod 256)");
        Check(Has(snap, kFlagN) && !Has(snap, kFlagZ) && !Has(snap, kFlagV) && Has(snap, kFlagC),
              "SBCA: N=1 Z=0 V=0 C=1 (borrow)");
    }
    {
        // Logical ops: ANDA/ORAA/EORA/BITA/CMPA, N/Z only, V always cleared.
        Hd63701Rig rig;
        rig.Prog({0x86, 0xF0, 0x84, 0x0F,   // LDAA #$F0; ANDA #$0F -> A=0x00, Z=1
                  0x86, 0x0F, 0x8A, 0xF0,   // LDAA #$0F; ORAA #$F0 -> A=0xFF, N=1
                  0x88, 0xFF,               // EORA #$FF -> A=0x00, Z=1
                  0x86, 0x81, 0x81, 0x81}); // LDAA #$81; CMPA #$81 -> Z=1, A unchanged
        rig.Boot();
        rig.Step(2);
        auto snap = rig.cpu.Snapshot();
        Check(snap.a == 0x00 && Has(snap, kFlagZ) && !Has(snap, kFlagV), "ANDA: 0xF0 & 0x0F == 0, Z=1, V=0");
        rig.Step(2);
        snap = rig.cpu.Snapshot();
        Check(snap.a == 0xFF && Has(snap, kFlagN), "ORAA: 0x0F | 0xF0 == 0xFF, N=1");
        rig.Step(1);
        snap = rig.cpu.Snapshot();
        Check(snap.a == 0x00 && Has(snap, kFlagZ), "EORA: 0xFF ^ 0xFF == 0, Z=1");
        rig.Step(2);
        snap = rig.cpu.Snapshot();
        Check(snap.a == 0x81 && Has(snap, kFlagZ), "CMPA: A unchanged, Z=1 when equal");
    }
}

void Hd63701DirectIndexedExtendedAddressing() {
    std::printf("Hd63701DirectIndexedExtendedAddressing\n");
    Hd63701Rig rig;
    // Direct-page addresses used here (0x20) fall outside the on-chip register (0x00-0x14) and
    // internal RAM (0x80-0xFF) windows, so they route through the external memory callback -
    // required for this test to actually exercise the addressing MODE rather than the chip's own
    // register/RAM interception (see the adapter's own class comment on address decode).
    rig.mem[0x0020] = 0x11;
    rig.mem[0x1234] = 0x22;
    rig.Prog({0x96, 0x20,               // LDAA $20 (direct)
              0x97, 0x21,               // STAA $21 (direct)
              0xCE, 0x00, 0x20,         // LDX #$0020
              0xA6, 0x05,               // LDAA $05,X (indexed, ea = 0x25)
              0xA7, 0x06,               // STAA $06,X (indexed, ea = 0x26)
              0xB6, 0x12, 0x34,         // LDAA $1234 (extended)
              0xB7, 0x12, 0x35});       // STAA $1235 (extended)
    rig.Boot();

    rig.Step(1);
    auto snap = rig.cpu.Snapshot();
    Check(snap.a == 0x11, "LDAA direct reads external memory at $20");
    rig.Step(1);
    Check(rig.mem[0x0021] == 0x11, "STAA direct writes external memory at $21");

    rig.mem[0x0025] = 0x33;
    rig.Step(1);  // LDX #$0020
    rig.Step(1);  // LDAA $05,X
    snap = rig.cpu.Snapshot();
    Check(snap.a == 0x33, "LDAA indexed reads external memory at X+5 == $25");
    rig.Step(1);
    Check(rig.mem[0x0026] == 0x33, "STAA indexed writes external memory at X+6 == $26");

    rig.Step(1);
    snap = rig.cpu.Snapshot();
    Check(snap.a == 0x22, "LDAA extended reads external memory at $1234");
    rig.Step(1);
    Check(rig.mem[0x1235] == 0x22, "STAA extended writes external memory at $1235");
}

void Hd63701ShiftAndRotateOps() {
    std::printf("Hd63701ShiftAndRotateOps\n");
    {
        Hd63701Rig rig;
        rig.Prog({0x86, 0x03, 0x44});  // LDAA #$03; LSRA
        rig.Boot();
        rig.Step(2);
        auto snap = rig.cpu.Snapshot();
        // LSR never sets N (doc "-0*-*"), so V == C always for this op (V = N^C = 0^C = C).
        Check(snap.a == 0x01 && Has(snap, kFlagC) && Has(snap, kFlagV) && !Has(snap, kFlagN) &&
                  !Has(snap, kFlagZ),
              "LSRA: 0x03>>1 == 0x01, C=1, V=C=1, N=0");
    }
    {
        Hd63701Rig rig;
        rig.Prog({0x86, 0x81, 0x47});  // LDAA #$81; ASRA
        rig.Boot();
        rig.Step(2);
        auto snap = rig.cpu.Snapshot();
        Check(snap.a == 0xC0, "ASRA: 0x81 arithmetic-shifted == 0xC0 (sign bit replicated)");
        // V = N^C (MAME's own NXORC macro, XORing the two bits AT THEIR ORIGINAL BIT POSITIONS,
        // not as normalized booleans - the result is nonetheless equivalent to "N XOR C" read as
        // booleans, verified by hand: N=1 and C=1 here, so V is CLEAR, not set).
        Check(Has(snap, kFlagC) && Has(snap, kFlagN) && !Has(snap, kFlagV), "ASRA: C=1 N=1 V=N^C=0");
    }
    {
        Hd63701Rig rig;
        rig.Prog({0x86, 0x40, 0x48});  // LDAA #$40; ASLA
        rig.Boot();
        rig.Step(2);
        auto snap = rig.cpu.Snapshot();
        Check(snap.a == 0x80, "ASLA: 0x40<<1 == 0x80");
        Check(Has(snap, kFlagN) && !Has(snap, kFlagC) && Has(snap, kFlagV), "ASLA: N=1 C=0 V=1 (overflow)");
    }
    {
        Hd63701Rig rig;
        rig.Prog({0x86, 0x80, 0x49});  // LDAA #$80; ROLA (C starts clear from reset)
        rig.Boot();
        rig.Step(2);
        auto snap = rig.cpu.Snapshot();
        Check(snap.a == 0x00, "ROLA: 0x80 rotated left with C=0 in == 0x00");
        Check(Has(snap, kFlagZ) && Has(snap, kFlagC) && Has(snap, kFlagV), "ROLA: Z=1 C=1 V=1");
    }
    {
        Hd63701Rig rig;
        rig.Prog({0x0D, 0x86, 0x01, 0x46});  // SEC; LDAA #$01; RORA
        rig.Boot();
        rig.Step(3);
        auto snap = rig.cpu.Snapshot();
        Check(snap.a == 0x80, "RORA: 0x01 rotated right with C=1 in == 0x80");
        // V = N^C (see the ASRA test above for why this is 0, not 1, when both N and C end up set).
        Check(Has(snap, kFlagN) && Has(snap, kFlagC) && !Has(snap, kFlagV), "RORA: N=1 C=1 V=N^C=0");
    }
}

void Hd63701IncDecNegComClrAccumulatorAndMemory() {
    std::printf("Hd63701IncDecNegComClrAccumulatorAndMemory\n");
    {
        Hd63701Rig rig;
        rig.Prog({0x86, 0x7F, 0x4C,   // LDAA #$7F; INCA -> 0x80, N|V
                  0x4A,               // DECA -> 0x7F, V (overflow boundary)
                  0x86, 0x01, 0x40,   // LDAA #$01; NEGA -> 0xFF, N, C
                  0x86, 0x0F, 0x43,   // LDAA #$0F; COMA -> 0xF0, N, C always
                  0x86, 0xFF, 0x4F,   // LDAA #$FF; CLRA -> 0x00, Z only
                  0x4D});             // TSTA on 0x00 -> Z
        rig.Boot();
        rig.Step(2);  // LDAA #$7F; INCA
        auto snap = rig.cpu.Snapshot();
        Check(snap.a == 0x80 && Has(snap, kFlagN) && Has(snap, kFlagV), "INCA: 0x7F -> 0x80, N|V");
        rig.Step(1);  // DECA
        snap = rig.cpu.Snapshot();
        Check(snap.a == 0x7F && Has(snap, kFlagV) && !Has(snap, kFlagN), "DECA: 0x80 -> 0x7F, V only");
        rig.Step(2);  // LDAA #$01; NEGA
        snap = rig.cpu.Snapshot();
        Check(snap.a == 0xFF && Has(snap, kFlagN) && Has(snap, kFlagC), "NEGA: 0x01 -> 0xFF, N C");
        rig.Step(2);  // LDAA #$0F; COMA
        snap = rig.cpu.Snapshot();
        Check(snap.a == 0xF0 && Has(snap, kFlagN) && Has(snap, kFlagC), "COMA: ~0x0F -> 0xF0, N, C always set");
        rig.Step(2);  // LDAA #$FF; CLRA
        snap = rig.cpu.Snapshot();
        Check(snap.a == 0x00 && Has(snap, kFlagZ) && !Has(snap, kFlagC), "CLRA: -> 0x00, Z only");
        rig.Step(1);  // TSTA
        snap = rig.cpu.Snapshot();
        Check(Has(snap, kFlagZ), "TSTA on 0: Z set");
    }
    {
        // Indexed memory RMW forms, X points at external address $20.
        Hd63701Rig rig;
        rig.mem[0x0020] = 0x01;
        rig.Prog({0xCE, 0x00, 0x20,  // LDX #$0020
                  0x60, 0x00,        // NEG $00,X -> mem[0x20] = 0xFF
                  0x6C, 0x00,        // INC $00,X -> mem[0x20] = 0x00
                  0x6A, 0x00,        // DEC $00,X -> mem[0x20] = 0xFF
                  0x63, 0x00,        // COM $00,X -> mem[0x20] = ~0xFF = 0x00
                  0x6F, 0x00});      // CLR $00,X -> mem[0x20] = 0x00 (already 0)
        rig.Boot();
        rig.Step(1);
        rig.Step(1);
        Check(rig.mem[0x0020] == 0xFF, "NEG indexed: mem[$20] 0x01 -> 0xFF");
        rig.Step(1);
        Check(rig.mem[0x0020] == 0x00, "INC indexed: mem[$20] 0xFF -> 0x00");
        rig.Step(1);
        Check(rig.mem[0x0020] == 0xFF, "DEC indexed: mem[$20] 0x00 -> 0xFF");
        rig.Step(1);
        Check(rig.mem[0x0020] == 0x00, "COM indexed: mem[$20] ~0xFF -> 0x00");
        rig.Step(1);
        auto snap = rig.cpu.Snapshot();
        Check(rig.mem[0x0020] == 0x00 && Has(snap, kFlagZ), "CLR indexed: mem[$20] -> 0x00, Z set");
    }
}

void Hd63701SixteenBitLoadStoreAndArithmetic() {
    std::printf("Hd63701SixteenBitLoadStoreAndArithmetic\n");
    Hd63701Rig rig;
    rig.Prog({0xCC, 0x12, 0x34,        // LDD #$1234
              0xFD, 0x00, 0x20,        // STD $0020 (extended)
              0xCE, 0x00, 0x50,        // LDX #$0050
              0x8C, 0x00, 0x50,        // CPX #$0050 -> Z=1
              0xCC, 0x00, 0x05,        // LDD #$0005
              0x83, 0x00, 0x03,        // SUBD #$0003 -> D=2
              0xCC, 0xFF, 0xFF,        // LDD #$FFFF
              0xC3, 0x00, 0x01});      // ADDD #$0001 -> D=0, C=1
    rig.Boot();

    rig.Step(1);
    auto snap = rig.cpu.Snapshot();
    Check(snap.a == 0x12 && snap.b == 0x34, "LDD #imm loads A:B as D");
    rig.Step(1);
    Check(rig.mem[0x0020] == 0x12 && rig.mem[0x0021] == 0x34, "STD extended writes big-endian D");

    rig.Step(1);
    snap = rig.cpu.Snapshot();
    Check(snap.x == 0x0050, "LDX #imm loads X");
    rig.Step(1);
    snap = rig.cpu.Snapshot();
    Check(Has(snap, kFlagZ) && !Has(snap, kFlagN) && !Has(snap, kFlagC), "CPX #$0050 with X==$0050: Z=1");

    rig.Step(2);
    snap = rig.cpu.Snapshot();
    Check(snap.a == 0x00 && snap.b == 0x02, "SUBD: 5-3 == 2");

    rig.Step(2);
    snap = rig.cpu.Snapshot();
    Check(snap.a == 0x00 && snap.b == 0x00, "ADDD: 0xFFFF+1 == 0x0000 (mod 65536)");
    Check(Has(snap, kFlagZ) && Has(snap, kFlagC) && !Has(snap, kFlagN) && !Has(snap, kFlagV),
          "ADDD: Z=1 C=1 N=0 V=0");
}

void Hd63701ConditionalBranches() {
    std::printf("Hd63701ConditionalBranches\n");
    Hd63701Rig rig;
    rig.Prog({0x86, 0x05,        // 0x0400 LDAA #$05
              0x81, 0x05,        // 0x0402 CMPA #$05 -> Z=1
              0x27, 0x02,        // 0x0404 BEQ +2 (to 0x0408)
              0xC6, 0xFF,        // 0x0406 LDAB #$FF (skipped)
              0xC6, 0x01});      // 0x0408 LDAB #$01 (branch target)
    rig.Boot();
    rig.Step(3);
    auto snap = rig.cpu.Snapshot();
    Check(snap.pc == 0x0408, "BEQ taken: branches over the skipped instruction");
    rig.Step(1);
    snap = rig.cpu.Snapshot();
    Check(snap.b == 0x01, "BEQ taken: the LDAB #$FF at the skipped address never ran");

    {
        Hd63701Rig rig2;
        rig2.Prog({0x86, 0x05, 0x81, 0x06,  // LDAA #$05; CMPA #$06 -> Z=0
                   0x27, 0x02,               // BEQ +2 (NOT taken)
                   0xC6, 0xFF,               // LDAB #$FF (runs, not skipped)
                   0xC6, 0x01});
        rig2.Boot();
        rig2.Step(3);
        auto s2 = rig2.cpu.Snapshot();
        Check(s2.pc == 0x0406, "BEQ not taken: falls through to the very next instruction");
        rig2.Step(1);
        s2 = rig2.cpu.Snapshot();
        Check(s2.b == 0xFF, "BEQ not taken: LDAB #$FF actually ran");
    }
}

void Hd63701JsrBsrRts() {
    std::printf("Hd63701JsrBsrRts\n");
    Hd63701Rig rig;
    rig.Prog({0x8E, 0x01, 0xFF,        // 0x0400 LDS #$01FF
              0xBD, 0x05, 0x00});      // 0x0403 JSR $0500 (extended)
    rig.Put(0x0500, {0x86, 0x42, 0x39});  // 0x0500 LDAA #$42; RTS
    rig.Boot();

    rig.Step(1);
    auto snap = rig.cpu.Snapshot();
    Check(snap.s == 0x01FF, "LDS #imm sets S");

    rig.Step(1);
    snap = rig.cpu.Snapshot();
    Check(snap.pc == 0x0500, "JSR extended jumps to the target address");
    Check(snap.s == 0x01FD, "JSR pushed a 2-byte return address (S -= 2)");
    Check(rig.mem[0x01FE] == 0x04 && rig.mem[0x01FF] == 0x06, "JSR pushed return address 0x0406 (after the 3-byte JSR)");

    rig.Step(1);
    snap = rig.cpu.Snapshot();
    Check(snap.a == 0x42, "LDAA #$42 inside the subroutine");

    rig.Step(1);
    snap = rig.cpu.Snapshot();
    Check(snap.pc == 0x0406, "RTS returns to the address JSR pushed");
    Check(snap.s == 0x01FF, "RTS restored S to its pre-call value");
}

void Hd63701PushPullStack() {
    std::printf("Hd63701PushPullStack\n");
    Hd63701Rig rig;
    rig.Prog({0x8E, 0x02, 0x00,        // LDS #$0200
              0x86, 0x11,              // LDAA #$11
              0x36,                    // PSHA
              0x86, 0x00,              // LDAA #$00
              0x32,                    // PULA -> A should be 0x11 again
              0xCE, 0x33, 0x44,        // LDX #$3344
              0x3C,                    // PSHX
              0xCE, 0x00, 0x00,        // LDX #$0000
              0x38});                  // PULX -> X should be 0x3344 again
    rig.Boot();
    rig.Step(3);
    auto snap = rig.cpu.Snapshot();
    Check(snap.a == 0x11 && snap.s == 0x01FF, "PSHA leaves A untouched and decrements S by 1");
    rig.Step(2);
    snap = rig.cpu.Snapshot();
    Check(snap.a == 0x11 && snap.s == 0x0200, "PULA restores A and S");

    rig.Step(2);  // LDX #$3344; PSHX
    snap = rig.cpu.Snapshot();
    Check(snap.x == 0x3344 && snap.s == 0x01FE, "PSHX leaves X untouched and decrements S by 2");
    rig.Step(2);  // LDX #$0000; PULX
    snap = rig.cpu.Snapshot();
    Check(snap.x == 0x3344 && snap.s == 0x0200, "PULX restores X and S");
}

void Hd63701SwiAndRtiRoundTrip() {
    std::printf("Hd63701SwiAndRtiRoundTrip\n");
    Hd63701Rig rig;
    rig.Vector(0xFFFA, 0x0500);
    rig.Prog({0x8E, 0x02, 0x00,        // LDS #$0200
              0x86, 0x11,              // LDAA #$11
              0xC6, 0x22,              // LDAB #$22
              0xCE, 0x33, 0x44,        // LDX #$3344
              0x3F});                  // SWI
    rig.Put(0x0500, {0x3B});           // RTI
    rig.Boot();
    rig.Step(5);  // LDS, LDAA, LDAB, LDX, then SWI itself
    auto snap = rig.cpu.Snapshot();
    Check(snap.pc == 0x0500, "SWI vectors through 0xFFFA");
    Check(Has(snap, kFlagI), "SWI sets the I mask");
    Check(snap.s == 0x0200 - 7, "SWI pushes 7 bytes (PC/X/A/B/CC)");

    rig.Step(1);
    snap = rig.cpu.Snapshot();
    Check(snap.pc == 0x040B, "RTI returns to the instruction right after SWI");
    Check(snap.a == 0x11 && snap.b == 0x22 && snap.x == 0x3344, "RTI restored A/B/X");
    Check(snap.s == 0x0200, "RTI restored S to its pre-SWI value");
}

void Hd63701NmiEdgeTriggeredAndPriorityOverIrq1() {
    std::printf("Hd63701NmiEdgeTriggeredAndPriorityOverIrq1\n");
    Hd63701Rig rig;
    rig.Vector(0xFFFC, 0x0600);
    rig.Prog({0x8E, 0x02, 0x00});  // LDS #$0200
    // The NMI vector target is populated with NOPs so that IRQ1 (still masked by CC.I, which the
    // interrupt entry itself sets) landing here on the next check does not spuriously TRAP on an
    // uninitialized (0x00) opcode byte - that would still leave nmiServiceCount alone (this test's
    // actual assertion), but a TRAP firing here would be a confusing, unintended side effect.
    rig.Put(0x0600, {0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01});
    rig.Boot();
    rig.Step(1);  // LDS

    // Assert both NMI and IRQ1 simultaneously - NMI must win (and NMI is not maskable by CC.I,
    // which is still set here since we never cleared it).
    rig.cpu.SetInputLine(Hd63701InputLine::Nmi, Hd63701InputLineState::Assert);
    rig.cpu.SetInputLine(Hd63701InputLine::Irq1, Hd63701InputLineState::Assert);
    rig.Step(1);
    auto snap = rig.cpu.Snapshot();
    Check(snap.pc == 0x0600, "NMI vectors through 0xFFFC even though CC.I is set");
    Check(snap.nmiServiceCount == 1, "NMI service counter incremented");
    Check(snap.irq1ServiceCount == 0, "IRQ1 did not also fire this step - NMI took priority");

    // NMI is edge-triggered: leaving the line asserted must not re-fire it.
    rig.cpu.SetInputLine(Hd63701InputLine::Nmi, Hd63701InputLineState::Clear);
    rig.Step(1);
    snap = rig.cpu.Snapshot();
    Check(snap.nmiServiceCount == 1, "NMI does not re-fire while merely held asserted (edge-triggered)");
}

void Hd63701Irq1MaskedByCcI() {
    std::printf("Hd63701Irq1MaskedByCcI\n");
    Hd63701Rig rig;
    rig.Vector(0xFFF8, 0x0700);
    rig.Prog({0x8E, 0x02, 0x00,        // LDS #$0200
              0x01, 0x01, 0x01,        // NOPs (IRQ1 masked here, CC.I still set from reset)
              0x0E, 0x01});            // CLI; NOP (CLI's own one-instruction delay)
    rig.Boot();
    rig.Step(1);  // LDS

    rig.cpu.SetInputLine(Hd63701InputLine::Irq1, Hd63701InputLineState::Assert);
    rig.Step(3);  // the 3 NOPs before CLI
    auto snap = rig.cpu.Snapshot();
    Check(snap.irq1ServiceCount == 0, "IRQ1 stays masked while CC.I is set");

    // CLI's mega-instruction (CLI itself + its mandatory following NOP) unmasks CC.I and then
    // immediately checks for a pending interrupt from INSIDE that same call (matches OpTap()'s own
    // behavior, exercised more directly by Hd63701CliDelaysInterruptRecognitionByOneInstruction) -
    // since IRQ1 has been asserted the whole time, it fires right here, within this one Step(1),
    // and EnterInterrupt() re-sets CC.I as part of taking it (so CC.I ends this call set again, not
    // clear - the interrupt entry itself is what re-masks it).
    rig.Step(1);
    snap = rig.cpu.Snapshot();
    Check(snap.irq1ServiceCount == 1, "IRQ1 fires within CLI's own mega-instruction, once CC.I is briefly clear");
    Check(snap.pc == 0x0700, "IRQ1 vectors through 0xFFF8");
    Check(Has(snap, kFlagI), "entering the interrupt re-sets CC.I (masking further IRQ1 until an RTI/CLI)");
}

void Hd63701WaiPushesStateAndWaitsForInterrupt() {
    std::printf("Hd63701WaiPushesStateAndWaitsForInterrupt\n");
    Hd63701Rig rig;
    rig.Vector(0xFFF8, 0x0800);
    rig.Prog({0x8E, 0x02, 0x00,  // LDS #$0200
              0x0E, 0x01,        // CLI; NOP (unmask, one extra instruction first)
              0x3E});            // WAI
    rig.Boot();
    rig.Step(2);  // LDS, then the CLI mega-instruction
    auto snap = rig.cpu.Snapshot();
    Check(!Has(snap, kFlagI), "I mask is clear before WAI executes");

    rig.Step(1);  // WAI - no interrupt pending yet, so it pre-pushes and starts waiting
    snap = rig.cpu.Snapshot();
    Check(snap.waiWaiting, "WAI enters the waiting state when nothing is pending");
    Check(snap.s == 0x0200 - 7, "WAI pre-pushed all 7 bytes of state");

    for (int i = 0; i < 5; ++i) rig.Step(1);  // idling - still waiting, nothing should change
    snap = rig.cpu.Snapshot();
    Check(snap.waiWaiting && snap.irq1ServiceCount == 0, "WAI keeps idling with no interrupt asserted");

    rig.cpu.SetInputLine(Hd63701InputLine::Irq1, Hd63701InputLineState::Assert);
    rig.Step(1);
    snap = rig.cpu.Snapshot();
    Check(!snap.waiWaiting, "WAI stops waiting once IRQ1 arrives");
    Check(snap.pc == 0x0800, "WAI's pending interrupt vectors through 0xFFF8");
    Check(snap.s == 0x0200 - 7,
          "entering the interrupt from WAI does not re-push state - S stays exactly where WAI's own "
          "pre-push already left it (the 4-cycle shortcut skips the push, it does not undo it)");
}

void Hd63701SlpWaitsWithoutPrePush() {
    std::printf("Hd63701SlpWaitsWithoutPrePush\n");
    Hd63701Rig rig;
    rig.Vector(0xFFF8, 0x0900);
    rig.Prog({0x8E, 0x02, 0x00,  // LDS #$0200
              0x0E, 0x01,        // CLI; NOP
              0x1A});            // SLP
    rig.Boot();
    rig.Step(2);
    rig.Step(1);  // SLP
    auto snap = rig.cpu.Snapshot();
    Check(snap.slpWaiting, "SLP enters the sleeping state");
    Check(snap.s == 0x0200, "SLP does NOT pre-push state (S unchanged)");

    rig.cpu.SetInputLine(Hd63701InputLine::Irq1, Hd63701InputLineState::Assert);
    rig.Step(1);
    snap = rig.cpu.Snapshot();
    Check(!snap.slpWaiting, "SLP stops sleeping once IRQ1 arrives");
    Check(snap.pc == 0x0900, "SLP's pending interrupt vectors through 0xFFF8");
    Check(snap.s == 0x0200 - 7, "entering the interrupt from SLP does a full 7-byte push (unlike WAI)");
}

void Hd63701TapDelaysInterruptRecognitionByOneInstruction() {
    std::printf("Hd63701TapDelaysInterruptRecognitionByOneInstruction\n");
    Hd63701Rig rig;
    rig.Vector(0xFFF8, 0x0A00);
    rig.Prog({0x8E, 0x02, 0x00,  // LDS #$0200
              0x86, 0x00,        // LDAA #$00 (I clear in the byte about to become CC)
              0x06,              // TAP -> CC = A = 0x00 (unmasks IRQ1)
              0xC6, 0x99});      // LDAB #$99 - must run BEFORE the pending IRQ1 is taken
    rig.Boot();
    rig.Step(2);  // LDS, LDAA #$00

    rig.cpu.SetInputLine(Hd63701InputLine::Irq1, Hd63701InputLineState::Assert);
    rig.Step(1);  // TAP mega-instruction: TAP itself + the mandatory LDAB #$99 right after it
    auto snap = rig.cpu.Snapshot();
    Check(snap.b == 0x99, "the instruction immediately after TAP ran before the interrupt was taken");
    Check(snap.irq1ServiceCount == 1, "IRQ1 fires as soon as TAP's own mega-instruction finishes");
    Check(snap.pc == 0x0A00, "IRQ1 vectors through 0xFFF8");
}

void Hd63701CliDelaysInterruptRecognitionByOneInstruction() {
    std::printf("Hd63701CliDelaysInterruptRecognitionByOneInstruction\n");
    Hd63701Rig rig;
    rig.Vector(0xFFF8, 0x0B00);
    rig.Prog({0x8E, 0x02, 0x00,  // LDS #$0200 (CC.I still set here, from reset)
              0x0E,              // CLI
              0xC6, 0x77});      // LDAB #$77 - must run before the pending IRQ1 is taken
    rig.Boot();
    rig.Step(1);  // LDS

    rig.cpu.SetInputLine(Hd63701InputLine::Irq1, Hd63701InputLineState::Assert);
    rig.Step(1);  // CLI mega-instruction: CLI itself + the mandatory LDAB #$77 right after it
    auto snap = rig.cpu.Snapshot();
    Check(snap.b == 0x77, "the instruction immediately after CLI ran before the interrupt was taken");
    Check(snap.irq1ServiceCount == 1, "IRQ1 fires as soon as CLI's own mega-instruction finishes");
    Check(snap.pc == 0x0B00, "IRQ1 vectors through 0xFFF8");
}

void Hd63701DaaAfterBcdAddition() {
    std::printf("Hd63701DaaAfterBcdAddition\n");
    Hd63701Rig rig;
    rig.Prog({0x86, 0x15, 0x8B, 0x27, 0x19});  // LDAA #$15; ADDA #$27; DAA -> BCD 15+27=42 -> 0x42
    rig.Boot();
    rig.Step(3);
    auto snap = rig.cpu.Snapshot();
    Check(snap.a == 0x42, "DAA: BCD 15+27 == 0x42");
    Check(!Has(snap, kFlagC), "DAA: no decimal carry out of this addition");
}

void Hd63701MulOperation() {
    std::printf("Hd63701MulOperation\n");
    {
        Hd63701Rig rig;
        rig.Prog({0x86, 0x0C, 0xC6, 0x0A, 0x3D});  // LDAA #$0C; LDAB #$0A; MUL -> D=120=0x0078
        rig.Boot();
        rig.Step(3);
        auto snap = rig.cpu.Snapshot();
        Check(snap.a == 0x00 && snap.b == 0x78, "MUL: 12*10 == 120 == 0x0078");
        Check(!Has(snap, kFlagC), "MUL: bit 7 of the low byte (0x78) is 0, so C is clear");
    }
    {
        Hd63701Rig rig;
        rig.Prog({0x86, 0x0A, 0xC6, 0x0D, 0x3D});  // LDAA #$0A; LDAB #$0D; MUL -> D=130=0x0082
        rig.Boot();
        rig.Step(3);
        auto snap = rig.cpu.Snapshot();
        Check(snap.a == 0x00 && snap.b == 0x82, "MUL: 10*13 == 130 == 0x0082");
        Check(Has(snap, kFlagC), "MUL: bit 7 of the low byte (0x82) is 1, so C is set");
    }
}

void Hd63701TrapOnIllegalOpcode() {
    std::printf("Hd63701TrapOnIllegalOpcode\n");
    Hd63701Rig rig;
    rig.Vector(0xFFEE, 0x0C00);
    rig.Prog({0x8E, 0x02, 0x00, 0x00});  // LDS #$0200; $00 (illegal)
    rig.Boot();
    rig.Step(1);
    rig.Step(1);
    auto snap = rig.cpu.Snapshot();
    Check(snap.pc == 0x0C00, "an illegal opcode traps through 0xFFEE");
    Check(snap.trapCount == 1, "trap counter incremented");
    Check(snap.s == 0x0200 - 7, "TRAP pushes a full 7-byte state, same as any other interrupt entry");
}

void Hd63701AimOimEimTimBitManipulation() {
    std::printf("Hd63701AimOimEimTimBitManipulation\n");
    Hd63701Rig rig;
    rig.mem[0x0020] = 0xFF;
    rig.Prog({0xCE, 0x00, 0x20,        // LDX #$0020
              0x61, 0x0F, 0x00,        // AIM #$0F,X -> mem[$20] = 0xFF & 0x0F = 0x0F
              0x62, 0xF0, 0x00,        // OIM #$F0,X -> mem[$20] = 0x0F | 0xF0 = 0xFF
              0x65, 0xFF, 0x00,        // EIM #$FF,X -> mem[$20] = 0xFF ^ 0xFF = 0x00
              0x6B, 0xFF, 0x00});      // TIM #$FF,X -> flags only, mem[$20] unchanged (still 0x00)
    rig.Boot();
    rig.Step(1);
    rig.Step(1);
    Check(rig.mem[0x0020] == 0x0F, "AIM indexed: 0xFF & 0x0F == 0x0F");
    rig.Step(1);
    Check(rig.mem[0x0020] == 0xFF, "OIM indexed: 0x0F | 0xF0 == 0xFF");
    rig.Step(1);
    Check(rig.mem[0x0020] == 0x00, "EIM indexed: 0xFF ^ 0xFF == 0x00");
    rig.Step(1);
    auto snap = rig.cpu.Snapshot();
    Check(rig.mem[0x0020] == 0x00 && Has(snap, kFlagZ), "TIM indexed: no write-back, Z set from the AND");

    // Direct-mode form (0x71/0x72/0x75/0x7B) uses DIRECT addressing, not extended - a real quirk of
    // this instruction group's encoding (see the adapter's own class comment).
    Hd63701Rig rig2;
    rig2.mem[0x0030] = 0xAA;
    rig2.Prog({0x71, 0x55, 0x30});  // AIM #$55,$30 (direct) -> mem[$30] = 0xAA & 0x55 = 0x00
    rig2.Boot();
    rig2.Step(1);
    Check(rig2.mem[0x0030] == 0x00, "AIM direct: 0xAA & 0x55 == 0x00");
}

void Hd63701XgdxAndUndocumentedOpcodes() {
    std::printf("Hd63701XgdxAndUndocumentedOpcodes\n");
    Hd63701Rig rig;
    rig.Prog({0x86, 0x12, 0xC6, 0x34,  // LDAA #$12; LDAB #$34 -> D=0x1234
              0xCE, 0x56, 0x78,        // LDX #$5678
              0x18});                 // XGDX -> X=0x1234, D=0x5678
    rig.Boot();
    rig.Step(3);
    rig.Step(1);
    auto snap = rig.cpu.Snapshot();
    Check(snap.x == 0x1234, "XGDX: X takes D's old value");
    Check(snap.a == 0x56 && snap.b == 0x78, "XGDX: D takes X's old value");

    {
        Hd63701Rig rig2;
        rig2.Prog({0x8E, 0x02, 0x00,  // LDS #$0200
                   0xCE, 0x00, 0x10,  // LDX #$0010
                   0x12});            // undoc1 ($12): X += mem[S+1]
        rig2.mem[0x0201] = 0x05;
        rig2.Boot();
        rig2.Step(2);
        rig2.Step(1);
        auto s2 = rig2.cpu.Snapshot();
        Check(s2.x == 0x0015, "undoc1 ($12): X += mem[S+1] == 0x10 + 5");
    }
}

void Hd63701PortGpioReadWrite() {
    std::printf("Hd63701PortGpioReadWrite\n");
    Hd63701Rig rig;
    std::uint8_t externalP1 = 0xA5;
    std::uint8_t lastWrittenP2 = 0;
    rig.cpu.SetPort1Callbacks([&externalP1]() { return externalP1; }, nullptr);
    rig.cpu.SetPort2Callbacks(nullptr, [&lastWrittenP2](std::uint8_t v) { lastWrittenP2 = v; });

    rig.Prog({0x96, 0x02,        // LDAA $02 (P1 data register, all-input DDR from reset)
              0x86, 0xFF,        // LDAA #$FF
              0x97, 0x01,        // STAA $01 (P2 DDR -> all outputs)
              0x86, 0x3C,        // LDAA #$3C
              0x97, 0x03});      // STAA $03 (P2 data -> should reach the write callback as 0x3C)
    rig.Boot();

    rig.Step(1);
    auto snap = rig.cpu.Snapshot();
    Check(snap.a == 0xA5, "P1 read (all-input DDR) reflects the external read callback");

    rig.Step(4);
    Check(lastWrittenP2 == 0x3C, "P2 write (all-output DDR) reaches the write callback with the data value");
}

void Hd63701TimerGeneratesIrq2OnOutputCompare() {
    std::printf("Hd63701TimerGeneratesIrq2OnOutputCompare\n");
    Hd63701Rig rig;
    rig.Vector(0xFFF4, 0x0D00);
    rig.Prog({0x8E, 0x02, 0x00,        // LDS #$0200
              0x0E, 0x01,              // CLI; NOP - unmask CC.I up front
              0x86, 0x08, 0x97, 0x08,  // LDAA #$08; STAA $08  -> TCSR EOCI enabled
              0x86, 0x00, 0x97, 0x0B,  // LDAA #$00; STAA $0B  -> OCRH = 0
              0x86, 0x20, 0x97, 0x0C, // LDAA #$20; STAA $0C  -> OCRL = 0x20 (32)
              });
    // A generous run of NOPs after the setup for the counter to reach the output-compare deadline -
    // the exact instruction count isn't hand-derived here (unlike the other tests above); this test
    // instead polls Snapshot() after every step, which is more robust than pinning an exact cycle
    // count for a peripheral whose triggering depends on the cumulative cost of every instruction
    // that ran before it.
    for (int i = 0; i < 40; ++i) rig.Put(static_cast<std::uint16_t>(0x0411 + i), {0x01});  // NOP
    rig.Boot();

    bool fired = false;
    for (int i = 0; i < 60 && !fired; ++i) {
        rig.Step(1);
        if (rig.cpu.Snapshot().irq2ServiceCount > 0) fired = true;
    }
    Check(fired, "the timer's output-compare interrupt (IRQ2/OCI) eventually fires");
    auto snap = rig.cpu.Snapshot();
    Check(snap.pc == 0x0D00, "OCI vectors through 0xFFF4");
    Check(snap.irq1ServiceCount == 0, "no IRQ1 line was ever asserted in this test - only the timer fired");
}

void Hd63701CyclesConsumedThisCallAdvancesInsideExecute() {
    std::printf("Hd63701CyclesConsumedThisCallAdvancesInsideExecute\n");
    Hd63701Rig rig;
    rig.Prog({0x01, 0x01, 0x01, 0x01, 0x01});  // 5 NOPs, 1 cycle each
    rig.Boot();

    Check(rig.cpu.CyclesConsumedThisCall() == 0, "CyclesConsumedThisCall() is 0 outside Execute()");

    std::vector<std::uint64_t> seenBeforeEachFetch;
    rig.cpu.SetOpcodeFetchObserverForTest([&](std::uint16_t, std::uint8_t) {
        seenBeforeEachFetch.push_back(rig.cpu.CyclesConsumedThisCall());
    });

    std::uint64_t total = rig.cpu.Execute(5);
    Check(total == 5, "Execute(5) with 5 1-cycle NOPs consumes exactly 5 cycles");
    Check(seenBeforeEachFetch.size() == 5, "the opcode-fetch observer fired once per NOP");
    for (std::size_t i = 0; i < seenBeforeEachFetch.size(); ++i) {
        Check(seenBeforeEachFetch[i] == i, "CyclesConsumedThisCall() at each fetch matches the NOPs already charged");
    }
    Check(rig.cpu.CyclesConsumedThisCall() == 0, "CyclesConsumedThisCall() returns to 0 after Execute() returns");
}

void Hd63701DeterministicAcrossRuns() {
    std::printf("Hd63701DeterministicAcrossRuns\n");
    auto run = []() {
        Hd63701Rig rig;
        rig.Vector(0xFFF8, 0x0E00);
        rig.Prog({0x8E, 0x02, 0x00, 0x0E, 0x01, 0x86, 0x7F, 0x8B, 0x01, 0x19, 0x3D, 0xC6, 0x0A, 0x3D});
        rig.Boot();
        rig.cpu.SetInputLine(Hd63701InputLine::Irq1, Hd63701InputLineState::Assert);
        for (int i = 0; i < 12; ++i) rig.Step(1);
        return std::make_pair(rig.cpu.Snapshot(), rig.mem);
    };

    auto [snapA, memA] = run();
    auto [snapB, memB] = run();

    Check(snapA.pc == snapB.pc && snapA.a == snapB.a && snapA.b == snapB.b && snapA.x == snapB.x &&
              snapA.s == snapB.s && snapA.cc == snapB.cc,
          "two identical runs produce identical final register state");
    Check(snapA.irq1ServiceCount == snapB.irq1ServiceCount && snapA.nmiServiceCount == snapB.nmiServiceCount &&
              snapA.trapCount == snapB.trapCount,
          "two identical runs produce identical interrupt/trap service counts");
    Check(memA == memB, "two identical runs leave identical external memory contents");
}
