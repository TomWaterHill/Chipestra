// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// MC6809E (IM6809Core + MAME-derived backend) native tests. All programs are hand-written synthetic
// byte sequences (no real ROM/game code) - same "not ROM-dependent" convention as
// h6280_adapter_tests.cpp / z80_adapter_tests.cpp / m68000_adapter_tests.cpp.
//
// Expected values (opcode bytes, cycle counts, flag results) are derived independently by hand from
// standard 6809 instruction semantics and cross-checked against the m6809make.py-generated m6809.hxx
// this adapter was ported from - not by reading back what this adapter computes.

#include "m6809_adapter_tests.h"

#include "devices/cpu/m6809/mame_derived_m6809_adapter.h"
#include "test_support.h"

#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <vector>

namespace {

constexpr std::uint8_t kFlagC = 0x01, kFlagV = 0x02, kFlagZ = 0x04, kFlagN = 0x08, kFlagI = 0x10,
                        kFlagH = 0x20, kFlagF = 0x40, kFlagE = 0x80;

struct M6809Rig {
    std::vector<std::uint8_t> mem = std::vector<std::uint8_t>(0x10000, 0);
    MameDerivedM6809Adapter cpu;

    M6809Rig() {
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
    }

    std::uint64_t Step(int n) {
        std::uint64_t total = 0;
        for (int i = 0; i < n; ++i) total += cpu.Execute(1);
        return total;
    }
};

bool Has(const MameDerivedM6809Adapter::RegisterSnapshot& s, std::uint8_t flag) { return (s.cc & flag) != 0; }

}  // namespace

void M6809ResetState() {
    std::printf("M6809ResetState\n");
    M6809Rig rig;
    rig.Boot(0x0400);
    auto snap = rig.cpu.Snapshot();
    Check(snap.pc == 0x0400, "reset: PC comes from the reset vector at 0xFFFE/0xFFFF");
    Check(snap.cc == (kFlagI | kFlagF), "reset: CC == I|F (0x50), other bits left as they were");
    Check(snap.dp == 0x00, "reset: DP cleared to 0");
    Check(!snap.ldsEncountered, "reset: ldsEncountered starts false");
}

void M6809ImmediateLoadFlagsAndCycles() {
    std::printf("M6809ImmediateLoadFlagsAndCycles\n");
    M6809Rig rig;
    rig.Prog({0x86, 0x00, 0x86, 0x80, 0x86, 0x01, 0xC6, 0x55});  // LDA #$00; LDA #$80; LDA #$01; LDB #$55
    rig.Boot();

    std::uint64_t c = rig.Step(1);
    auto snap = rig.cpu.Snapshot();
    Check(snap.a == 0x00 && Has(snap, kFlagZ) && !Has(snap, kFlagN) && !Has(snap, kFlagV),
          "LDA #$00: A==0, Z set, N/V clear");
    Check(c == 2, "LDA #imm costs 2 cycles");

    rig.Step(1);
    snap = rig.cpu.Snapshot();
    Check(snap.a == 0x80 && Has(snap, kFlagN) && !Has(snap, kFlagZ), "LDA #$80: N set, Z clear");

    rig.Step(1);
    snap = rig.cpu.Snapshot();
    Check(snap.a == 0x01 && !Has(snap, kFlagN) && !Has(snap, kFlagZ), "LDA #$01: N and Z clear");

    rig.Step(1);
    snap = rig.cpu.Snapshot();
    Check(snap.b == 0x55, "LDB #$55 loads B independently of A");
    Check(snap.pc == 0x0408, "PC advanced past all four 2-byte instructions");
}

void M6809AddSubFlagsOverflowAndCarry() {
    std::printf("M6809AddSubFlagsOverflowAndCarry\n");
    {
        // A=0x7F, ADDA #$01 -> A=0x80: signed overflow (127+1), H set (nibble carry), C clear.
        M6809Rig rig;
        rig.Prog({0x86, 0x7F, 0x8B, 0x01});  // LDA #$7F; ADDA #$01
        rig.Boot();
        rig.Step(2);
        auto snap = rig.cpu.Snapshot();
        Check(snap.a == 0x80, "ADDA #$01: 0x7F+0x01 == 0x80");
        Check(Has(snap, kFlagN) && !Has(snap, kFlagZ) && Has(snap, kFlagV) && !Has(snap, kFlagC) &&
                  Has(snap, kFlagH),
              "ADDA #$01: 127+1 overflow -> N=1,Z=0,V=1,C=0,H=1");
    }
    {
        // A=0x00, SUBA #$01 -> A=0xFF: NO signed overflow (0-1=-1, in range), C set (borrow).
        // A naive implementation that truncates the intermediate result to 8 bits BEFORE
        // computing V (rather than keeping it at MAME's own uint32_t width through that step) gets
        // V=1 here, which is wrong.
        M6809Rig rig;
        rig.Prog({0x86, 0x00, 0x80, 0x01});  // LDA #$00; SUBA #$01
        rig.Boot();
        rig.Step(2);
        auto snap = rig.cpu.Snapshot();
        Check(snap.a == 0xFF, "SUBA #$01: 0x00-0x01 == 0xFF");
        Check(Has(snap, kFlagN) && !Has(snap, kFlagZ) && !Has(snap, kFlagV) && Has(snap, kFlagC),
              "SUBA #$01 from 0: N=1,Z=0,V=0 (no signed overflow),C=1 (borrow)");
    }
    {
        // A=0x80 (-128), SUBA #$01 -> A=0x7F: signed overflow (-128-1 out of int8 range).
        M6809Rig rig;
        rig.Prog({0x86, 0x80, 0x80, 0x01});  // LDA #$80; SUBA #$01
        rig.Boot();
        rig.Step(2);
        auto snap = rig.cpu.Snapshot();
        Check(snap.a == 0x7F, "SUBA #$01: 0x80-0x01 == 0x7F");
        Check(!Has(snap, kFlagN) && !Has(snap, kFlagZ) && Has(snap, kFlagV) && !Has(snap, kFlagC),
              "SUBA #$01 from -128: N=0 (result positive),V=1 (overflow),C=0 (no borrow)");
    }
    {
        // A=0xFF, ADDA #$01 -> A=0x00: unsigned carry out, but NO signed overflow (-1+1=0, operands
        // have different signs so addition overflow is impossible).
        M6809Rig rig;
        rig.Prog({0x86, 0xFF, 0x8B, 0x01});  // LDA #$FF; ADDA #$01
        rig.Boot();
        rig.Step(2);
        auto snap = rig.cpu.Snapshot();
        Check(snap.a == 0x00, "ADDA #$01: 0xFF+0x01 wraps to 0x00");
        Check(!Has(snap, kFlagN) && Has(snap, kFlagZ) && !Has(snap, kFlagV) && Has(snap, kFlagC),
              "ADDA #$01 from 0xFF: N=0,Z=1,V=0 (no overflow),C=1 (carry out)");
    }
}

void M6809DirectAndExtendedAddressing() {
    std::printf("M6809DirectAndExtendedAddressing\n");
    M6809Rig rig;
    rig.mem[0x0050] = 0x42;    // direct page (DP==0 after reset) target
    rig.mem[0x1234] = 0x99;    // extended target
    rig.Prog({0x96, 0x50,          // LDA <$50 (direct)
              0xB6, 0x12, 0x34});  // LDA $1234 (extended)
    rig.Boot();

    std::uint64_t c = rig.Step(1);
    auto snap = rig.cpu.Snapshot();
    Check(snap.a == 0x42, "LDA <$50 (direct, DP=0): reads physical 0x0050");
    Check(c == 4, "LDA direct costs 4 cycles");

    c = rig.Step(1);
    snap = rig.cpu.Snapshot();
    Check(snap.a == 0x99, "LDA $1234 (extended): reads 0x1234");
    Check(c == 5, "LDA extended costs 5 cycles");
}

void M6809IndexedAddressingVariants() {
    std::printf("M6809IndexedAddressingVariants\n");
    {
        // ,X - postbyte 0x84 (0x80|0x04, X register, no offset).
        M6809Rig rig;
        rig.mem[0x0060] = 0x77;
        rig.Prog({0x10, 0xCE, 0x00, 0x00,  // LDS #$0000 (harmless; keeps ldsEncountered path exercised
                                            // elsewhere from interfering with this test's own state)
              0x8E, 0x00, 0x60,            // LDX #$0060
              0xA6, 0x84});                // LDA ,X
        rig.Boot();
        rig.Step(3);
        auto snap = rig.cpu.Snapshot();
        Check(snap.a == 0x77, ",X (postbyte 0x84): reads memory[X]");
        Check(snap.x == 0x0060, ",X: X unchanged (no auto-increment)");
    }
    {
        // ,X+ (postincrement by 1) and ,-X (predecrement by 1) - postbytes 0x80 / 0x82.
        M6809Rig rig;
        rig.mem[0x0070] = 0x11;
        rig.Prog({0x8E, 0x00, 0x70,    // LDX #$0070
              0xA6, 0x80,              // LDA ,X+
              0xA6, 0x82});            // LDA ,-X   (X is now 0x0071; predecrement reads memory[0x0070])
        rig.Boot();
        rig.Step(1);
        rig.Step(1);
        auto snap = rig.cpu.Snapshot();
        Check(snap.a == 0x11 && snap.x == 0x0071, ",X+ reads memory[0x0070] then increments X to 0x0071");
        rig.Step(1);
        snap = rig.cpu.Snapshot();
        Check(snap.a == 0x11 && snap.x == 0x0070, ",-X decrements X to 0x0070 first, then reads it");
    }
    {
        // 5-bit offset (X+5) - postbyte 0x05 (reg=X (0x00) | 5-bit value 5, high bit clear).
        M6809Rig rig;
        rig.mem[0x0045] = 0x22;
        rig.Prog({0x8E, 0x00, 0x40,   // LDX #$0040
              0xA6, 0x05});           // LDA 5,X
        rig.Boot();
        rig.Step(2);
        auto snap = rig.cpu.Snapshot();
        Check(snap.a == 0x22, "5,X (5-bit offset): reads memory[X+5] == memory[0x0045]");
    }
    {
        // n8,R - postbyte 0x88 (X, sub-case 0x08) + one extra signed offset byte.
        M6809Rig rig;
        rig.mem[0x0040] = 0x33;
        rig.Prog({0x8E, 0x00, 0x30,        // LDX #$0030
              0xA6, 0x88, 0x10});          // LDA $10,X  (8-bit offset +0x10)
        rig.Boot();
        rig.Step(2);
        auto snap = rig.cpu.Snapshot();
        Check(snap.a == 0x33, "n8,X: reads memory[X+0x10] == memory[0x0040]");
    }
    {
        // n16,R - postbyte 0x89 (X, sub-case 0x09) + two extra offset bytes.
        M6809Rig rig;
        rig.mem[0x0300] = 0x44;
        rig.Prog({0x8E, 0x02, 0x00,              // LDX #$0200
              0xA6, 0x89, 0x01, 0x00});          // LDA $0100,X (16-bit offset)
        rig.Boot();
        rig.Step(2);
        auto snap = rig.cpu.Snapshot();
        Check(snap.a == 0x44, "n16,X: reads memory[X+0x0100] == memory[0x0300]");
    }
    {
        // B,R - postbyte 0x85 (X, sub-case 0x05): B register's signed value as the offset.
        M6809Rig rig;
        rig.mem[0x0058] = 0x55;
        rig.Prog({0xC6, 0x08,           // LDB #$08
              0x8E, 0x00, 0x50,         // LDX #$0050
              0xA6, 0x85});             // LDA B,X
        rig.Boot();
        rig.Step(3);
        auto snap = rig.cpu.Snapshot();
        Check(snap.a == 0x55 && snap.b == 0x08, "B,X: reads memory[X+B] == memory[0x0058], B unaffected");
    }
    {
        // n8,PCR - postbyte 0x8C (sub-case 0x0C): EA = PC-after-reading-the-offset-byte + offset.
        M6809Rig rig;
        rig.mem[0x0408] = 0x77;  // LDA n8,PCR opcode at 0x0400, postbyte 0x401, offset 0x402;
                                  // PC after reading both == 0x0403; 0x0403+5 == 0x0408.
        rig.Prog({0xA6, 0x8C, 0x05});  // LDA 5,PCR
        rig.Boot();
        rig.Step(1);
        auto snap = rig.cpu.Snapshot();
        Check(snap.a == 0x77, "n8,PCR: EA computed from PC AFTER the offset byte, not the opcode's own PC");
    }
    {
        // [,X] indirect - postbyte 0x94 (0x84 | 0x10 indirect bit): EA is read FROM memory[X].
        M6809Rig rig;
        rig.mem[0x0060] = 0x02;
        rig.mem[0x0061] = 0x00;  // pointer stored big-endian at X: points to 0x0200
        rig.mem[0x0200] = 0x88;
        rig.Prog({0x8E, 0x00, 0x60,   // LDX #$0060
              0xA6, 0x94});           // LDA [,X]
        rig.Boot();
        rig.Step(2);
        auto snap = rig.cpu.Snapshot();
        Check(snap.a == 0x88, "[,X] indirect: reads the pointer at memory[X], then dereferences it");
    }
}

void M6809ShiftAndRotateOps() {
    std::printf("M6809ShiftAndRotateOps\n");
    {
        // LSRA: 0x03 (0000 0011) -> 0x01, C set (bit 0 shifted out was 1).
        M6809Rig rig;
        rig.Prog({0x86, 0x03, 0x44});  // LDA #$03; LSRA
        rig.Boot();
        rig.Step(2);
        auto snap = rig.cpu.Snapshot();
        Check(snap.a == 0x01 && Has(snap, kFlagC) && !Has(snap, kFlagN), "LSRA: 0x03 -> 0x01, C=1, N always 0");
    }
    {
        // ASRA: 0x81 (1000 0001, -127) -> arithmetic shift keeps the sign bit: 0xC0, C=1 (bit0 was 1).
        M6809Rig rig;
        rig.Prog({0x86, 0x81, 0x47});  // LDA #$81; ASRA
        rig.Boot();
        rig.Step(2);
        auto snap = rig.cpu.Snapshot();
        Check(snap.a == 0xC0 && Has(snap, kFlagC) && Has(snap, kFlagN), "ASRA: 0x81 -> 0xC0 (sign preserved), C=1");
    }
    {
        // ASLA (== LSLA): 0x81 -> 0x02, C=1 (bit7 shifted out was 1), V=1 (sign changed: N was 1, now 0).
        M6809Rig rig;
        rig.Prog({0x86, 0x81, 0x48});  // LDA #$81; ASLA
        rig.Boot();
        rig.Step(2);
        auto snap = rig.cpu.Snapshot();
        Check(snap.a == 0x02 && Has(snap, kFlagC) && !Has(snap, kFlagN) && Has(snap, kFlagV),
              "ASLA: 0x81 -> 0x02, C=1 (bit7 out), V=1 (bit7 changed under the shift)");
    }
    {
        // ROLA with carry IN clear: 0x81 -> 0x02 (like ASL, carry-in=0 goes into bit0), C_out=1.
        M6809Rig rig;
        rig.Prog({0x1C, 0xFE,  // ANDCC #$FE (clear C)
              0x86, 0x81,      // LDA #$81
              0x49});          // ROLA
        rig.Boot();
        rig.Step(3);
        auto snap = rig.cpu.Snapshot();
        Check(snap.a == 0x02 && Has(snap, kFlagC), "ROLA with C=0 in: 0x81 -> 0x02, C_out=1");
    }
    {
        // RORA with carry IN set: 0x02 -> 0x81 (carry-in becomes the new bit7), C_out=0 (bit0 of 0x02 was 0).
        M6809Rig rig;
        rig.Prog({0x1A, 0x01,  // ORCC #$01 (set C)
              0x86, 0x02,      // LDA #$02
              0x46});          // RORA
        rig.Boot();
        rig.Step(3);
        auto snap = rig.cpu.Snapshot();
        Check(snap.a == 0x81 && !Has(snap, kFlagC), "RORA with C=1 in: 0x02 -> 0x81, C_out=0");
    }
}

void M6809IncDecNegComClr() {
    std::printf("M6809IncDecNegComClr\n");
    {
        M6809Rig rig;
        rig.Prog({0x86, 0x7F, 0x4C});  // LDA #$7F; INCA
        rig.Boot();
        rig.Step(2);
        auto snap = rig.cpu.Snapshot();
        Check(snap.a == 0x80 && Has(snap, kFlagN) && Has(snap, kFlagV), "INCA: 0x7F -> 0x80, N=1,V=1 (overflow)");
    }
    {
        // DEC of 0 does NOT overflow (0-1=-1, in signed-byte range) - same shape as the SUBA #$01
        // from A==0 case above (both compute a=0,b=1,r=-1); V==1 only when decrementing 0x80.
        M6809Rig rig;
        rig.Prog({0x86, 0x00, 0x4A});  // LDA #$00; DECA
        rig.Boot();
        rig.Step(2);
        auto snap = rig.cpu.Snapshot();
        Check(snap.a == 0xFF && Has(snap, kFlagN) && !Has(snap, kFlagV), "DECA: 0x00 -> 0xFF, N=1, V=0 (no overflow)");
    }
    {
        // DEC of 0x80 (-128) DOES overflow (-128-1 is out of int8 range, wraps to 0x7F).
        M6809Rig rig;
        rig.Prog({0x86, 0x80, 0x4A});  // LDA #$80; DECA
        rig.Boot();
        rig.Step(2);
        auto snap = rig.cpu.Snapshot();
        Check(snap.a == 0x7F && !Has(snap, kFlagN) && Has(snap, kFlagV), "DECA: 0x80 -> 0x7F, N=0, V=1 (overflow)");
    }
    {
        // NEGA: two's-complement negate. 0x01 -> 0xFF, C=1 (NEG sets C unless the operand was 0).
        M6809Rig rig;
        rig.Prog({0x86, 0x01, 0x40});  // LDA #$01; NEGA
        rig.Boot();
        rig.Step(2);
        auto snap = rig.cpu.Snapshot();
        Check(snap.a == 0xFF && Has(snap, kFlagN) && Has(snap, kFlagC), "NEGA: -1 == 0xFF, N=1, C=1");
    }
    {
        // NEGA of 0x80 (-128): overflows (can't represent +128 in int8) -> stays 0x80, V=1.
        M6809Rig rig;
        rig.Prog({0x86, 0x80, 0x40});  // LDA #$80; NEGA
        rig.Boot();
        rig.Step(2);
        auto snap = rig.cpu.Snapshot();
        Check(snap.a == 0x80 && Has(snap, kFlagV) && Has(snap, kFlagN), "NEGA of -128: overflow, stays 0x80, V=1");
    }
    {
        M6809Rig rig;
        rig.Prog({0x86, 0x55, 0x43});  // LDA #$55; COMA
        rig.Boot();
        rig.Step(2);
        auto snap = rig.cpu.Snapshot();
        Check(snap.a == 0xAA && Has(snap, kFlagC) && !Has(snap, kFlagV), "COMA: bitwise NOT, C always set, V always clear");
    }
    {
        M6809Rig rig;
        rig.Prog({0x86, 0x55, 0x4F});  // LDA #$55; CLRA
        rig.Boot();
        rig.Step(2);
        auto snap = rig.cpu.Snapshot();
        Check(snap.a == 0x00 && Has(snap, kFlagZ) && !Has(snap, kFlagN) && !Has(snap, kFlagV) && !Has(snap, kFlagC),
              "CLRA: A=0, Z=1, N/V/C all forced clear");
    }
}

void M6809SixteenBitLoadStoreAndArithmetic() {
    std::printf("M6809SixteenBitLoadStoreAndArithmetic\n");
    {
        M6809Rig rig;
        rig.Prog({0xCC, 0x12, 0x34});  // LDD #$1234
        rig.Boot();
        std::uint64_t c = rig.Step(1);
        auto snap = rig.cpu.Snapshot();
        Check(snap.a == 0x12 && snap.b == 0x34, "LDD #$1234 splits into A=0x12,B=0x34 (D==A:B)");
        Check(c == 3, "LDD #imm16 costs 3 cycles");
    }
    {
        M6809Rig rig;
        rig.Prog({0x10, 0xCE, 0x02, 0x00});  // LDS #$0200 (page 0x10 prefix)
        rig.Boot();
        rig.Step(1);
        auto snap = rig.cpu.Snapshot();
        Check(snap.s == 0x0200, "LDS #$0200 (page-0x10 opcode 0xCE)");
        Check(snap.ldsEncountered, "LDS sets ldsEncountered (gates NMI edge detection)");
    }
    {
        // STD extended, then verify the bytes landed in memory big-endian.
        M6809Rig rig;
        rig.Prog({0xCC, 0xAB, 0xCD,          // LDD #$ABCD
              0xFD, 0x05, 0x00});            // STD $0500 (extended)
        rig.Boot();
        rig.Step(2);
        Check(rig.mem[0x0500] == 0xAB && rig.mem[0x0501] == 0xCD, "STD $0500 writes A then B (big-endian)");
    }
    {
        // ADDD with a genuine 16-bit carry/overflow case: 0x7FFF + 0x0001 -> 0x8000 (V=1, C=0).
        M6809Rig rig;
        rig.Prog({0xCC, 0x7F, 0xFF,   // LDD #$7FFF
              0xC3, 0x00, 0x01});     // ADDD #$0001
        rig.Boot();
        rig.Step(2);
        auto snap = rig.cpu.Snapshot();
        std::uint16_t d = static_cast<std::uint16_t>((snap.a << 8) | snap.b);
        Check(d == 0x8000, "ADDD #$0001: 0x7FFF+1 == 0x8000");
        Check(Has(snap, kFlagN) && Has(snap, kFlagV) && !Has(snap, kFlagC),
              "ADDD 16-bit overflow: N=1,V=1,C=0 (same shape as the 8-bit ADDA case)");
    }
    {
        // CMPX: compares without modifying X, sets flags as if X-operand were computed.
        M6809Rig rig;
        rig.Prog({0x8E, 0x00, 0x10,   // LDX #$0010
              0x8C, 0x00, 0x10});     // CMPX #$0010
        rig.Boot();
        rig.Step(2);
        auto snap = rig.cpu.Snapshot();
        Check(snap.x == 0x0010 && Has(snap, kFlagZ), "CMPX #$0010 (X==0010): Z set, X itself unchanged");
    }
}

void M6809BranchesShortAndLong() {
    std::printf("M6809BranchesShortAndLong\n");
    {
        M6809Rig rig;
        rig.Prog({0x86, 0x00,        // LDA #$00 (sets Z)
              0x27, 0x02,            // BEQ +2 -> skip the next instruction, land at 0x0406
              0x86, 0xFF,            // LDA #$FF (must be skipped)
              0x86, 0x11});          // LDA #$11 (branch target)
        rig.Boot();
        rig.Step(2);  // LDA #$00; BEQ (taken)
        auto snap = rig.cpu.Snapshot();
        Check(snap.pc == 0x0406, "BEQ (Z set): branches to 0x0404+2 == 0x0406");
        rig.Step(1);
        snap = rig.cpu.Snapshot();
        Check(snap.a == 0x11, "BEQ landed past the skipped LDA #$FF");
    }
    {
        M6809Rig rig;
        rig.Prog({0x86, 0x01,        // LDA #$01 (Z clear)
              0x27, 0x02,            // BEQ +2 (NOT taken)
              0x86, 0x22});          // LDA #$22 (falls through here)
        rig.Boot();
        rig.Step(3);  // LDA #$01; BEQ (not taken); LDA #$22
        auto snap = rig.cpu.Snapshot();
        Check(snap.a == 0x22, "BEQ not taken (Z clear): falls through to the next instruction");
    }
    {
        // LBEQ (page-0x10 long branch): 16-bit offset, +1 extra cycle when taken.
        M6809Rig rig;
        rig.Prog({0x86, 0x00,                 // LDA #$00 (Z set)
              0x10, 0x27, 0x00, 0x03,         // LBEQ +3 -> skip the 3-byte LDA extended below
              0xB6, 0x99, 0x99,               // LDA $9999 (must be skipped)
              0x86, 0x33});                   // LDA #$33 (branch target)
        rig.Boot();
        rig.Step(2);
        auto snap = rig.cpu.Snapshot();
        Check(snap.pc == 0x0409, "LBEQ (taken): PC == 0x0406 (after the 4-byte LBEQ)+3 == 0x0409");
        rig.Step(1);
        snap = rig.cpu.Snapshot();
        Check(snap.a == 0x33, "LBEQ landed past the skipped extended LDA");
    }
    {
        // BRA is unconditional regardless of flags.
        M6809Rig rig;
        rig.Prog({0x20, 0x01,    // BRA +1 -> skip one byte
              0x86,              // (stray opcode byte, must never be fetched as an opcode)
              0x86, 0x44});      // LDA #$44 (branch target)
        rig.Boot();
        rig.Step(2);
        auto snap = rig.cpu.Snapshot();
        Check(snap.a == 0x44, "BRA always branches, regardless of CC");
    }
}

void M6809JsrBsrRts() {
    std::printf("M6809JsrBsrRts\n");
    M6809Rig rig;
    rig.mem[0x0600] = 0x39;  // RTS
    rig.Prog({0x10, 0xCE, 0x01, 0xFF,   // LDS #$01FF
          0xBD, 0x06, 0x00});          // JSR $0600 (extended)
    rig.Boot();
    rig.Step(1);  // LDS
    rig.Step(1);  // JSR
    auto snap = rig.cpu.Snapshot();
    Check(snap.pc == 0x0600, "JSR $0600: PC jumps to the subroutine");
    Check(snap.s == 0x01FD, "JSR pushed a 2-byte return address (S -= 2)");
    // LDS (0x10,0xCE,0x01,0xFF) occupies 0x0400-0403; JSR $0600 (0xBD,0x06,0x00) occupies 0x0404-0406;
    // the return address pushed is 0x0407 (the instruction right after the 3-byte JSR).
    Check(rig.mem[0x01FD] == 0x04 && rig.mem[0x01FE] == 0x07,
          "JSR pushed the return address (0x0407, high byte first) onto the stack");
    rig.Step(1);  // RTS
    snap = rig.cpu.Snapshot();
    Check(snap.pc == 0x0407, "RTS pulls PC back to the instruction after JSR");
    Check(snap.s == 0x01FF, "RTS restores S to its pre-JSR value");
}

void M6809PushPullMultipleRegisters() {
    std::printf("M6809PushPullMultipleRegisters\n");
    {
        M6809Rig rig;
        rig.Prog({0x10, 0xCE, 0x02, 0x00,    // LDS #$0200
              0x86, 0xAA,                    // LDA #$AA
              0xC6, 0xBB,                    // LDB #$BB
              0x34, 0x06,                    // PSHS A,B (mask 0x06)
              0x86, 0x00, 0xC6, 0x00,        // LDA #$00; LDB #$00 (clobber both)
              0x35, 0x06});                  // PULS A,B
        rig.Boot();
        rig.Step(3);  // LDS, LDA, LDB
        rig.Step(1);  // PSHS
        auto snap = rig.cpu.Snapshot();
        Check(snap.s == 0x01FE, "PSHS A,B: S -= 2");
        Check(rig.mem[0x01FE] == 0xAA && rig.mem[0x01FF] == 0xBB, "PSHS A,B: A then B, in ascending address order");
        rig.Step(2);  // clobber
        rig.Step(1);  // PULS
        snap = rig.cpu.Snapshot();
        Check(snap.a == 0xAA && snap.b == 0xBB && snap.s == 0x0200, "PULS A,B: restores A/B and S exactly");
    }
    {
        // PSHS/PULS bit 0x40 ("the other" U/S register) - saves/restores U across a PSHS/PULS pair.
        M6809Rig rig;
        rig.Prog({0x10, 0xCE, 0x02, 0x00,    // LDS #$0200
              0xCE, 0x12, 0x34,              // LDU #$1234
              0x34, 0x40,                    // PSHS U (mask 0x40 - "the other stack register")
              0xCE, 0x00, 0x00,              // LDU #$0000 (clobber U)
              0x35, 0x40});                  // PULS U
        rig.Boot();
        rig.Step(2);  // LDS, LDU
        rig.Step(1);  // PSHS
        auto snap = rig.cpu.Snapshot();
        Check(snap.s == 0x01FE, "PSHS U (bit 0x40): S -= 2");
        Check(rig.mem[0x01FE] == 0x12 && rig.mem[0x01FF] == 0x34, "PSHS U wrote U's value onto the S stack");
        rig.Step(1);  // clobber
        rig.Step(1);  // PULS
        snap = rig.cpu.Snapshot();
        Check(snap.u == 0x1234 && snap.s == 0x0200, "PULS U restores U from the S stack, S back to 0x0200");
    }
}

void M6809LeaSetsFlagsAndLdsTracking() {
    std::printf("M6809LeaSetsFlagsAndLdsTracking\n");
    {
        M6809Rig rig;
        rig.Prog({0x8E, 0xFF, 0xFB,       // LDX #$FFFB
              0x30, 0x05});               // LEAX 5,X -> EA == 0xFFFB+5 == 0x0000 (wraps), Z set
        rig.Boot();
        rig.Step(2);
        auto snap = rig.cpu.Snapshot();
        Check(snap.x == 0x0000 && Has(snap, kFlagZ), "LEAX 5,X wrapping to 0: Z set");
    }
    {
        M6809Rig rig;
        rig.Prog({0x8E, 0x00, 0x10,   // LDX #$0010
              0x30, 0x05});           // LEAX 5,X -> EA == 0x0015, nonzero
        rig.Boot();
        rig.Step(2);
        auto snap = rig.cpu.Snapshot();
        Check(snap.x == 0x0015 && !Has(snap, kFlagZ), "LEAX 5,X to a nonzero address: Z clear");
    }
    {
        // LEAS sets ldsEncountered (like LDS/TFR-to-S) but does NOT touch CC flags.
        M6809Rig rig;
        rig.Prog({0x8E, 0x03, 0x00,   // LDX #$0300
              0x32, 0x00});           // LEAS ,X  (postbyte 0x00: X base, no offset)
        rig.Boot();
        rig.Step(2);
        auto snap = rig.cpu.Snapshot();
        Check(snap.s == 0x0300, "LEAS ,X sets S from X's value");
        Check(snap.ldsEncountered, "LEAS sets ldsEncountered");
    }
}

void M6809TfrAndExg() {
    std::printf("M6809TfrAndExg\n");
    {
        M6809Rig rig;
        rig.Prog({0x8E, 0x12, 0x34,   // LDX #$1234
              0x1F, 0x12});           // TFR X,Y  (param 0x12: src=1(X)<<4? wait high nibble=src,low=dst)
        rig.Boot();
        rig.Step(2);
        auto snap = rig.cpu.Snapshot();
        Check(snap.y == 0x1234 && snap.x == 0x1234, "TFR X,Y copies X into Y (X unchanged)");
    }
    {
        // TFR to S sets ldsEncountered.
        M6809Rig rig;
        rig.Prog({0x8E, 0x02, 0x00,   // LDX #$0200
              0x1F, 0x14});           // TFR X,S (dst nibble 4 == S)
        rig.Boot();
        rig.Step(2);
        auto snap = rig.cpu.Snapshot();
        Check(snap.s == 0x0200 && snap.ldsEncountered, "TFR X,S sets S and ldsEncountered");
    }
    {
        // EXG A,B swaps the two 8-bit accumulators.
        M6809Rig rig;
        rig.Prog({0x86, 0x11,     // LDA #$11
              0xC6, 0x22,         // LDB #$22
              0x1E, 0x89});       // EXG A,B (src nibble 8==A, dst nibble 9==B)
        rig.Boot();
        rig.Step(3);
        auto snap = rig.cpu.Snapshot();
        Check(snap.a == 0x22 && snap.b == 0x11, "EXG A,B swaps A and B");
    }
}

void M6809SwiAndRtiRoundTrip() {
    std::printf("M6809SwiAndRtiRoundTrip\n");
    M6809Rig rig;
    rig.Vector(0xFFFA, 0x0700);  // SWI vector
    rig.mem[0x0700] = 0x3B;      // RTI, at the SWI handler
    rig.Prog({0x10, 0xCE, 0x02, 0x00,   // LDS #$0200
          0x86, 0x99,                  // LDA #$99 (distinctive register content to verify the round trip)
          0x3F});                      // SWI
    rig.Boot();
    rig.Step(2);  // LDS, LDA
    auto snap = rig.cpu.Snapshot();
    Check(snap.pc == 0x0406, "sanity: PC sits at the 1-byte SWI opcode before it executes");
    // SWI's own return address is 0x0407 (0x0406 + its own 1 opcode byte, no operand bytes).
    const std::uint16_t kReturnAddress = 0x0407;

    rig.Step(1);  // SWI
    snap = rig.cpu.Snapshot();
    Check(snap.pc == 0x0700, "SWI vectors to 0x0700");
    Check(Has(snap, kFlagE) && Has(snap, kFlagI) && Has(snap, kFlagF), "SWI: CC has E set and masks I+F");
    Check(snap.s == 0x0200 - 12, "SWI pushed the entire 12-byte state (PC,U/S-other,Y,X,DP,B,A,CC)");

    rig.Step(1);  // RTI
    snap = rig.cpu.Snapshot();
    Check(snap.pc == kReturnAddress, "RTI restores PC to the instruction after SWI");
    Check(snap.a == 0x99, "RTI restored A across the full state round trip");
    Check(snap.s == 0x0200, "RTI restores S to its pre-SWI value");
    // SWI sets CC_E BEFORE pushing (matches MAME's own m6809.hxx: "m_cc |= CC_E;" precedes
    // "goto PUSH_REGISTERS"), so the PUSHED cc byte already has E=1 - RTI restores exactly what was
    // pushed, so E comes back SET. This is real 6809 behavior (E==1 marks "this was a full-state
    // save"), not a bug - CC_E only reads as clear again once something (e.g. the program itself)
    // explicitly clears it.
    Check(Has(snap, kFlagE), "RTI restores CC exactly as SWI pushed it, including E==1");
}

void M6809NmiMaskedUntilFirstStackWrite() {
    std::printf("M6809NmiMaskedUntilFirstStackWrite\n");
    M6809Rig rig;
    rig.Vector(0xFFFC, 0x0800);
    rig.Prog({0x12, 0x12,                    // NOP; NOP
          0x10, 0xCE, 0x01, 0xFF,            // LDS #$01FF (first write to S)
          0x12, 0x12, 0x12});                // NOP; NOP; NOP
    rig.Boot();

    rig.cpu.SetInputLine(M6809InputLine::Nmi, M6809InputLineState::Assert);
    rig.Step(1);  // NOP (1 byte)
    auto snap = rig.cpu.Snapshot();
    Check(snap.nmiServiceCount == 0, "NMI asserted before any S write: edge is gated, never latched");
    Check(snap.pc == 0x0401, "execution continued normally (not vectored to NMI)");

    rig.cpu.SetInputLine(M6809InputLine::Nmi, M6809InputLineState::Clear);
    rig.cpu.SetInputLine(M6809InputLine::Nmi, M6809InputLineState::Assert);
    rig.Step(1);  // NOP
    snap = rig.cpu.Snapshot();
    Check(snap.nmiServiceCount == 0, "a second edge, still before the LDS, is still gated");

    rig.Step(1);  // LDS #$01FF
    snap = rig.cpu.Snapshot();
    Check(snap.ldsEncountered, "LDS sets ldsEncountered");
    Check(snap.nmiServiceCount == 0, "the gate lifting on its own (without a fresh edge) does not retroactively latch NMI");

    rig.cpu.SetInputLine(M6809InputLine::Nmi, M6809InputLineState::Clear);
    rig.cpu.SetInputLine(M6809InputLine::Nmi, M6809InputLineState::Assert);
    rig.Step(1);
    snap = rig.cpu.Snapshot();
    Check(snap.nmiServiceCount == 1, "a fresh edge after ldsEncountered becomes true is serviced");
    Check(snap.pc == 0x0800, "NMI vectored to 0x0800");
    Check(Has(snap, kFlagE) && Has(snap, kFlagI) && Has(snap, kFlagF), "NMI: CC has E set, masks I+F");
}

void M6809FirqPushesPartialState() {
    std::printf("M6809FirqPushesPartialState\n");
    M6809Rig rig;
    rig.Vector(0xFFF6, 0x0900);
    rig.Prog({0x10, 0xCE, 0x02, 0x00,   // LDS #$0200
          0x1C, 0xAF,                  // ANDCC #$AF (clear I and F to unmask FIRQ)
          0x12});                      // NOP (FIRQ should be recognized before this ever runs)
    rig.Boot();
    rig.Step(2);  // LDS, ANDCC
    auto snap = rig.cpu.Snapshot();
    Check(!Has(snap, kFlagI) && !Has(snap, kFlagF), "ANDCC #$AF cleared I and F");

    rig.cpu.SetInputLine(M6809InputLine::Firq, M6809InputLineState::Assert);
    rig.Step(1);
    snap = rig.cpu.Snapshot();
    Check(snap.firqServiceCount == 1, "FIRQ recognized once I/F are clear");
    Check(snap.pc == 0x0900, "FIRQ vectored to 0x0900");
    Check(!Has(snap, kFlagE), "FIRQ pushes the PARTIAL state (E stays clear), unlike NMI/IRQ/SWI");
    Check(snap.s == 0x0200 - 3, "FIRQ pushed only 3 bytes (CC + 2-byte PC), the partial-state mask 0x81");
    Check(Has(snap, kFlagI) && Has(snap, kFlagF), "FIRQ masks both I and F on entry");
}

void M6809IrqMaskingAndPriorityOverFirq() {
    std::printf("M6809IrqMaskingAndPriorityOverFirq\n");
    {
        // FIRQ takes priority over IRQ when both are pending and both are unmasked.
        M6809Rig rig;
        rig.Vector(0xFFF8, 0x0A00);
        rig.Vector(0xFFF6, 0x0B00);
        rig.Prog({0x10, 0xCE, 0x02, 0x00,  // LDS #$0200
              0x1C, 0xAF,                 // ANDCC #$AF (clear I and F - unmask both)
              0x12});                     // NOP
        rig.Boot();
        rig.Step(2);
        rig.cpu.SetInputLine(M6809InputLine::Irq, M6809InputLineState::Assert);
        rig.cpu.SetInputLine(M6809InputLine::Firq, M6809InputLineState::Assert);
        rig.Step(1);
        auto snap = rig.cpu.Snapshot();
        Check(snap.firqServiceCount == 1 && snap.irqServiceCount == 0,
              "FIRQ and IRQ asserted together, both unmasked: FIRQ wins (higher priority)");
        Check(snap.pc == 0x0B00, "PC vectored to the FIRQ handler, not the IRQ handler");
    }
    {
        // IRQ fires (and pushes the FULL state, unlike FIRQ) while F stays set - IRQ never touches F.
        M6809Rig rig;
        rig.Vector(0xFFF8, 0x0A00);
        rig.Prog({0x10, 0xCE, 0x02, 0x00,  // LDS #$0200 (CC == I|F == 0x50 at this point)
              0x1C, 0xEF,                 // ANDCC #$EF: clear I only, F stays set (0x50 & 0xEF == 0x40)
              0x12});                     // NOP
        rig.Boot();
        rig.Step(2);
        auto snap = rig.cpu.Snapshot();
        Check(!Has(snap, kFlagI) && Has(snap, kFlagF), "ANDCC #$EF cleared only I; F is still set");

        rig.cpu.SetInputLine(M6809InputLine::Firq, M6809InputLineState::Assert);  // masked by F, must not fire
        rig.cpu.SetInputLine(M6809InputLine::Irq, M6809InputLineState::Assert);
        rig.Step(1);
        snap = rig.cpu.Snapshot();
        Check(snap.irqServiceCount == 1 && snap.firqServiceCount == 0,
              "IRQ fires while FIRQ (also asserted) stays masked by F");
        Check(snap.pc == 0x0A00, "PC vectored to the IRQ handler");
        Check(Has(snap, kFlagE), "IRQ pushes the FULL state (E set), unlike FIRQ's partial state");
        Check(snap.s == 0x0200 - 12, "IRQ pushed the full 12-byte state");
        Check(Has(snap, kFlagI) && Has(snap, kFlagF), "IRQ sets I on entry; F is untouched (was already set)");
    }
}

void M6809DaaAfterBcdAddition() {
    std::printf("M6809DaaAfterBcdAddition\n");
    // Classic BCD idiom: ADDA of two packed-BCD bytes, then DAA corrects the result to packed BCD.
    // 0x28 (BCD 28) + 0x14 (BCD 14) = 0x3C raw; DAA should correct this to 0x42 (28+14=42 decimal).
    M6809Rig rig;
    rig.Prog({0x86, 0x28,   // LDA #$28
          0x8B, 0x14,       // ADDA #$14 -> raw 0x3C
          0x19});           // DAA
    rig.Boot();
    rig.Step(2);
    auto snap = rig.cpu.Snapshot();
    Check(snap.a == 0x3C, "ADDA #$14: 0x28+0x14 == 0x3C (raw hex, not yet BCD-corrected)");
    rig.Step(1);
    snap = rig.cpu.Snapshot();
    Check(snap.a == 0x42, "DAA corrects 0x3C to the packed-BCD result 0x42 (28+14=42 decimal)");
    Check(!Has(snap, kFlagC), "DAA: no decimal carry out of this addition");
}

void M6809MulOperation() {
    std::printf("M6809MulOperation\n");
    M6809Rig rig;
    rig.Prog({0x86, 0x03,   // LDA #$03
          0xC6, 0x03,       // LDB #$03
          0x3D});           // MUL -> D = A*B = 9 = 0x0009 (low byte 0x09, bit 7 clear)
    rig.Boot();
    rig.Step(2);
    std::uint64_t c = rig.Step(1);
    auto snap = rig.cpu.Snapshot();
    Check(snap.a == 0x00 && snap.b == 0x09, "MUL: 3*3 == 9 == D=0x0009");
    Check(!Has(snap, kFlagC) && !Has(snap, kFlagZ), "MUL: C from D's bit 7 (clear here), Z from D==0 (not here)");
    Check(c == 11, "MUL costs 11 cycles");

    // MUL setting C: any product with bit 7 of B set has C=1 (MAME's own "C from D bit 7" rule).
    M6809Rig rig2;
    rig2.Prog({0x86, 0x02, 0xC6, 0x40, 0x3D});  // LDA #$02; LDB #$40; MUL -> D=0x0080
    rig2.Boot();
    rig2.Step(3);
    auto snap2 = rig2.cpu.Snapshot();
    Check(snap2.a == 0x00 && snap2.b == 0x80, "MUL: 2*64 == 128 == D=0x0080");
    Check(Has(snap2, kFlagC), "MUL: D's bit 7 set -> C set");
}

void M6809SexSignExtends() {
    std::printf("M6809SexSignExtends\n");
    {
        M6809Rig rig;
        rig.Prog({0xC6, 0x80, 0x1D});  // LDB #$80 (-128); SEX
        rig.Boot();
        rig.Step(2);
        auto snap = rig.cpu.Snapshot();
        Check(snap.a == 0xFF && snap.b == 0x80, "SEX of a negative B (0x80): A becomes 0xFF (sign-extended)");
        Check(Has(snap, kFlagN) && !Has(snap, kFlagZ), "SEX: N follows the sign, Z clear (D != 0)");
    }
    {
        M6809Rig rig;
        rig.Prog({0xC6, 0x00, 0x1D});  // LDB #$00; SEX
        rig.Boot();
        rig.Step(2);
        auto snap = rig.cpu.Snapshot();
        Check(snap.a == 0x00 && snap.b == 0x00, "SEX of B==0: A stays 0x00");
        Check(!Has(snap, kFlagN) && Has(snap, kFlagZ), "SEX of 0: N clear, Z set (D == 0)");
    }
}

void M6809SyncWaitsForPendingInterrupt() {
    std::printf("M6809SyncWaitsForPendingInterrupt\n");
    M6809Rig rig;
    rig.Vector(0xFFF8, 0x0C00);
    // LDS is required here: without it S stays 0x0000 from reset, and the interrupt entry's
    // PushRegisters() would decrement S down through 0xFFFF, 0xFFFE, ... corrupting the interrupt
    // vector table itself (0xFFF6-0xFFFF) that InterruptVectorFetch() is about to read from.
    rig.Prog({0x10, 0xCE, 0x01, 0xFF,  // LDS #$01FF
          0x1C, 0xEF,                 // ANDCC #$EF (clear I, unmask IRQ - CC starts as I|F from reset)
          0x13,                       // SYNC
          0x86, 0x77});               // LDA #$77 (executes once SYNC releases)
    rig.Boot();
    rig.Step(1);  // LDS
    rig.Step(1);  // ANDCC

    std::uint64_t c1 = rig.Step(1);  // SYNC, no interrupt pending yet - should just spin in place
    auto snap = rig.cpu.Snapshot();
    Check(snap.syncWaiting, "SYNC with no pending interrupt: enters the wait state instead of advancing");
    // SYNC is a 1-byte opcode; its own DummyReadOpcodeArg(0) reads at the CURRENT pc without
    // advancing it further, so PC sits right past the SYNC opcode byte itself (0x0406 -> 0x0407).
    Check(snap.pc == 0x0407, "PC sits just past SYNC's own 1-byte opcode while waiting");
    Check(c1 >= 1, "Execute() still consumed (at least) the requested budget while waiting");

    c1 = rig.Step(1);  // still no interrupt - still waiting, budget still consumed
    snap = rig.cpu.Snapshot();
    Check(snap.syncWaiting, "SYNC keeps waiting across multiple Execute() calls with no interrupt");

    rig.cpu.SetInputLine(M6809InputLine::Irq, M6809InputLineState::Assert);
    rig.Step(1);  // this call should notice the pending IRQ, clear syncWaiting, and service it
    snap = rig.cpu.Snapshot();
    Check(!snap.syncWaiting, "asserting IRQ clears syncWaiting");
    Check(snap.irqServiceCount == 1, "the pending IRQ is serviced once SYNC releases");
    Check(snap.pc == 0x0C00, "PC vectored to the IRQ handler after SYNC released");
}

void M6809CwaiPushesStateAndWaits() {
    std::printf("M6809CwaiPushesStateAndWaits\n");
    M6809Rig rig;
    rig.Vector(0xFFF8, 0x0D00);
    rig.Prog({0x10, 0xCE, 0x02, 0x00,   // LDS #$0200
          0x86, 0x5A,                  // LDA #$5A (distinctive content to verify the pushed state)
          0x3C, 0xAF});                // CWAI #$AF (ANDs CC with 0xAF - clears I and F, unmasking both)
    rig.Boot();
    rig.Step(2);  // LDS, LDA

    std::uint64_t c = rig.Step(1);  // CWAI itself: masks CC, pushes the ENTIRE state, then waits
    auto snap = rig.cpu.Snapshot();
    Check(snap.cwaiWaiting, "CWAI enters the wait state after pushing");
    Check(snap.s == 0x0200 - 12, "CWAI pushed the entire 12-byte state before waiting");
    // Stack frame layout (low to high address, from PushRegisters' own bit-check order 0x80..0x01):
    // [S+0]=CC [S+1]=A [S+2]=B [S+3]=DP [S+4..5]=X [S+6..7]=Y [S+8..9]=U [S+10..11]=PC.
    Check(rig.mem[0x0200 - 12 + 1] == 0x5A, "the pushed state includes A's value (0x5A) at offset S+1");
    Check(c >= 1, "Execute() consumed the requested budget even while CWAI is waiting");

    rig.cpu.SetInputLine(M6809InputLine::Irq, M6809InputLineState::Assert);
    rig.Step(1);
    snap = rig.cpu.Snapshot();
    Check(!snap.cwaiWaiting, "asserting IRQ clears cwaiWaiting");
    Check(snap.irqServiceCount == 1, "the pending IRQ is serviced once CWAI releases");
    Check(snap.pc == 0x0D00, "PC vectored to the IRQ handler");
    Check(snap.s == 0x0200 - 12, "CWAI's own push is NOT duplicated by the interrupt entry - S unchanged from the wait");
}

void M6809CyclesConsumedThisCallAdvancesInsideExecute() {
    std::printf("M6809CyclesConsumedThisCallAdvancesInsideExecute\n");
    M6809Rig rig;
    rig.Prog({0x86, 0x11});  // LDA #$11 (2 cycles: opcode fetch, then the immediate operand read)
    rig.Boot();
    Check(rig.cpu.CyclesConsumedThisCall() == 0, "CyclesConsumedThisCall() is 0 outside any Execute() call");

    std::uint64_t observedDuringCallback = 0;
    rig.cpu = MameDerivedM6809Adapter();  // fresh instance so the read-observer can see the opcode fetch
    rig.cpu.SetMemoryCallbacks([&rig](std::uint16_t addr) -> std::uint8_t { return rig.mem[addr]; },
                                [&rig](std::uint16_t addr, std::uint8_t v) { rig.mem[addr] = v; });
    rig.cpu.SetOpcodeFetchObserverForTest(
        [&](std::uint16_t, std::uint8_t) { observedDuringCallback = rig.cpu.CyclesConsumedThisCall(); });
    rig.Boot();
    rig.Step(1);
    // The observer fires right after ReadOpcode() returns, and ReadOpcode() itself already called
    // Eat(1) for the fetch - so by the time the callback sees CyclesConsumedThisCall(), that first
    // cycle is already counted (matches MameDerivedZ80Adapter's own "cycles so far, mid-instruction"
    // semantics, not "cycles before this instruction started").
    Check(observedDuringCallback == 1, "the opcode fetch's own 1 cycle is already counted when the observer fires");
    Check(rig.cpu.CyclesConsumedThisCall() == 0, "back to 0 once Execute() has returned");
}

void M6809DeterministicAcrossRuns() {
    std::printf("M6809DeterministicAcrossRuns\n");
    auto run = []() {
        M6809Rig rig;
        rig.Vector(0xFFF8, 0x0E00);
        rig.mem[0x0E00] = 0x3B;  // RTI
        rig.Prog({0x10, 0xCE, 0x01, 0xFF,  // LDS #$01FF
              0x1C, 0xEF,                  // ANDCC #$EF (unmask IRQ)
              0x86, 0x01,                  // LDA #$01
              0x4C,                        // INCA
              0x8B, 0x05,                  // ADDA #$05
              0x30, 0x89, 0x00, 0x10,      // LEAX $0010,X (X starts at 0 -> X=0x0010)
              0x34, 0x17,                  // PSHS A,X,CC (mask 0x17: CC|A|X... exercised for bus traffic)
              0x35, 0x17});                // PULS A,X,CC
        rig.Boot();
        rig.cpu.SetInputLine(M6809InputLine::Irq, M6809InputLineState::Assert);
        rig.Step(9);
        rig.cpu.SetInputLine(M6809InputLine::Irq, M6809InputLineState::Clear);
        return rig.cpu.Snapshot();
    };

    auto a = run();
    auto b = run();
    Check(a.pc == b.pc && a.a == b.a && a.b == b.b && a.x == b.x && a.s == b.s && a.cc == b.cc &&
              a.irqServiceCount == b.irqServiceCount,
          "two independent instances running the same program reach bit-identical register state");
}
