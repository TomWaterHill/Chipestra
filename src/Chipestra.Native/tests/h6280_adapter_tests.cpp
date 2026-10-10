// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// IH6280Core + MAME-derived HuC6280 backend native tests. All programs are hand-written synthetic
// byte sequences (no real ROM/game code) - same "not ROM-dependent" convention as z80_adapter_tests.cpp /
// m68000_adapter_tests.cpp / i8039_adapter_tests.cpp.
//
// Expected values (opcode bytes, cycle counts, flag results) are derived independently from the HuC6280
// instruction semantics as documented in MAME's h6280.cpp per-opcode
// table (opcode byte and cycle count per instruction) and worked out by hand - not by reading back what
// this adapter computes. Timing: the core starts in low-speed mode, every CPU cycle costs 4 input clocks.

#include "h6280_adapter_tests.h"

#include "devices/cpu/h6280/mame_derived_h6280_adapter.h"
#include "test_support.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <vector>

namespace {

constexpr std::uint8_t kFlagC = 0x01, kFlagZ = 0x02, kFlagI = 0x04, kFlagD = 0x08, kFlagT = 0x20, kFlagV = 0x40,
                       kFlagN = 0x80;

// 21-bit physical memory (2MB) behind the core's callbacks. After Reset() every mapper register is 0, so logical
// bank 7 (0xE000-0xFFFF) maps to physical 0x000000-0x001FFF: programs live at physical 0x0400 (= logical 0xE400)
// and the vectors at physical 0x1FF6-0x1FFF (= logical 0xFFF6-0xFFFF).
struct H6280Rig {
    std::vector<std::uint8_t> mem = std::vector<std::uint8_t>(0x200000, 0);
    std::size_t externalWrites = 0;  // callback writes (the core's own I/O page never reaches them)
    MameDerivedH6280Adapter cpu;

    H6280Rig() {
        cpu.SetMemoryCallbacks([this](std::uint32_t addr) -> std::uint8_t { return mem[addr]; },
                               [this](std::uint32_t addr, std::uint8_t v) {
                                   mem[addr] = v;
                                   ++externalWrites;
                               });
    }

    void Put(std::uint32_t phys, std::initializer_list<std::uint8_t> bytes) {
        std::size_t i = 0;
        for (std::uint8_t b : bytes) {
            mem[phys + i++] = b;
        }
    }

    void Prog(std::initializer_list<std::uint8_t> bytes) { Put(0x0400, bytes); }

    void Vector(std::uint32_t vectorPhys, std::uint16_t target) {
        mem[vectorPhys] = static_cast<std::uint8_t>(target & 0xFF);
        mem[vectorPhys + 1] = static_cast<std::uint8_t>(target >> 8);
    }

    void Boot() {
        Vector(0x1FFE, 0xE400);
        cpu.Reset();
    }

    std::uint64_t Step(int n) {
        std::uint64_t total = 0;
        for (int i = 0; i < n; ++i) {
            total += cpu.Execute(1);
        }
        return total;
    }
};

bool Has(const MameDerivedH6280Adapter::RegisterSnapshot& s, std::uint8_t flag) { return (s.p & flag) != 0; }

}  // namespace

void H6280ResetState() {
    std::printf("H6280ResetState\n");
    H6280Rig rig;
    rig.Boot();
    const auto snap = rig.cpu.Snapshot();
    Check(snap.pc == 0xE400, "reset: PC comes from the reset vector at physical 0x1FFE (mapper register 7 == 0)");
    Check(snap.p == 0x14, "reset: P == I|B");
    Check(snap.s == 0xFF, "reset: S == 0xFF (stack starts at 0x1FF)");
    Check(snap.clocksPerCycle == 4, "reset: low speed mode (4 clocks per cycle)");
    bool allZero = true;
    for (int i = 0; i < 8; ++i) {
        allZero = allZero && snap.mmr[i] == 0;
    }
    Check(allZero, "reset: all eight mapper registers are 0");
    Check(snap.timerStatus == 0 && snap.irqMask == 0, "reset: timer off, interrupt mask 0");
}

void H6280LoadImmediateFlagsAndCycles() {
    std::printf("H6280LoadImmediateFlagsAndCycles\n");
    H6280Rig rig;
    rig.Prog({0xA9, 0x00, 0xA9, 0x80, 0xA9, 0x01});  // LDA #$00 / LDA #$80 / LDA #$01, 2 cycles each
    rig.Boot();

    std::uint64_t clocks = rig.Step(1);
    auto snap = rig.cpu.Snapshot();
    Check(snap.a == 0x00 && Has(snap, kFlagZ) && !Has(snap, kFlagN), "LDA #$00: A == 0, Z set, N clear");
    Check(clocks == 8, "LDA #imm takes 2 cycles == 8 clocks in low speed mode");

    clocks = rig.Step(1);
    snap = rig.cpu.Snapshot();
    Check(snap.a == 0x80 && Has(snap, kFlagN) && !Has(snap, kFlagZ), "LDA #$80: N set, Z clear");
    Check(clocks == 8, "second LDA #imm also 8 clocks");

    rig.Step(1);
    snap = rig.cpu.Snapshot();
    Check(snap.a == 0x01 && !Has(snap, kFlagN) && !Has(snap, kFlagZ), "LDA #$01: N and Z clear");
    Check(snap.pc == 0xE406, "PC advanced past the three 2-byte instructions");
}

void H6280MemoryMapperTamTma() {
    std::printf("H6280MemoryMapperTamTma\n");
    H6280Rig rig;
    rig.Prog({0xA9, 0xF8, 0x53, 0x02, 0xA9, 0x00, 0x43, 0x02});  // LDA #$F8; TAM #$02; LDA #$00; TMA #$02
    rig.Boot();
    rig.Step(2);
    auto snap = rig.cpu.Snapshot();
    Check(snap.mmr[1] == 0xF8, "TAM #$02 copies A into mapper register 1");
    bool othersZero = true;
    for (int i = 0; i < 8; ++i) {
        if (i != 1) {
            othersZero = othersZero && snap.mmr[i] == 0;
        }
    }
    Check(othersZero, "TAM writes only the selected mapper register");
    rig.Step(2);
    snap = rig.cpu.Snapshot();
    Check(snap.a == 0xF8, "TMA #$02 copies mapper register 1 back into A");
}

void H6280ZeroPageAndStackUseMapperRegister1() {
    std::printf("H6280ZeroPageAndStackUseMapperRegister1\n");
    H6280Rig rig;
    // LDA #$F8; TAM #$02; LDA #$AB; STA $10 (zero page); PHA
    rig.Prog({0xA9, 0xF8, 0x53, 0x02, 0xA9, 0xAB, 0x85, 0x10, 0x48});
    rig.Boot();
    rig.Step(5);
    const auto snap = rig.cpu.Snapshot();
    Check(rig.mem[0x1F0010] == 0xAB, "zero page store lands at (mapper register 1 << 13) | 0x10 = 0x1F0010");
    Check(rig.mem[0x1F01FF] == 0xAB, "first push goes to the stack page (0x1F0100 | S=0xFF)");
    Check(snap.s == 0xFE, "S decremented by the push");
}

void H6280AddressingModesLoadFromMappedRam() {
    std::printf("H6280AddressingModesLoadFromMappedRam\n");
    H6280Rig rig;
    // LDA #$F8; TAM #$02; LDX #$05; LDA $20,X; LDA $2040; LDA $2040,X; LDA ($30); LDY #$05; LDA ($30),Y
    rig.Prog({0xA9, 0xF8, 0x53, 0x02, 0xA2, 0x05, 0xB5, 0x20, 0xAD, 0x40, 0x20, 0xBD, 0x40, 0x20, 0xB2, 0x30, 0xA0,
              0x05, 0xB1, 0x30});
    rig.Put(0x1F0025, {0x11});        // zero page 0x25 (= $20 + X)
    rig.Put(0x1F0030, {0x40, 0x20});  // zero page pointer at $30/$31 -> logical 0x2040
    rig.Put(0x1F0040, {0x33});        // logical 0x2040 (bank 1 -> physical 0x1F0040)
    rig.Put(0x1F0045, {0x44});        // logical 0x2045
    rig.Boot();
    rig.Step(3);
    rig.Step(1);
    Check(rig.cpu.Snapshot().a == 0x11, "LDA $20,X (zero page indexed)");
    rig.Step(1);
    Check(rig.cpu.Snapshot().a == 0x33, "LDA $2040 (absolute through the mapper)");
    rig.Step(1);
    Check(rig.cpu.Snapshot().a == 0x44, "LDA $2040,X (absolute indexed)");
    rig.Step(1);
    Check(rig.cpu.Snapshot().a == 0x33, "LDA ($30) (zero page indirect)");
    rig.Step(2);
    Check(rig.cpu.Snapshot().a == 0x44, "LDA ($30),Y (zero page indirect indexed)");
}

void H6280BlockTransferTiiCyclesAndData() {
    std::printf("H6280BlockTransferTiiCyclesAndData\n");
    H6280Rig rig;
    // LDA #$F8; TAM #$02; TII src=$2100 dst=$2200 len=4
    rig.Prog({0xA9, 0xF8, 0x53, 0x02, 0x73, 0x00, 0x21, 0x00, 0x22, 0x04, 0x00});
    rig.Put(0x1F0100, {1, 2, 3, 4});
    rig.Boot();
    rig.Step(2);
    const std::uint64_t clocks = rig.Step(1);
    Check(clocks == static_cast<std::uint64_t>((6 * 4 + 17) * 4), "TII costs (6*length + 17) cycles == 164 clocks for 4 bytes");
    Check(rig.mem[0x1F0200] == 1 && rig.mem[0x1F0201] == 2 && rig.mem[0x1F0202] == 3 && rig.mem[0x1F0203] == 4,
          "TII copies the four bytes");
    Check(rig.mem[0x1F0100] == 1 && rig.mem[0x1F0103] == 4, "TII leaves the source unchanged");
}

void H6280BlockTransferTinTddTiaTai() {
    std::printf("H6280BlockTransferTinTddTiaTai\n");
    H6280Rig rig;
    // LDA #$F8; TAM #$02; TIN, TDD, TIA, TAI (each: opcode, src lo/hi, dst lo/hi, len lo/hi)
    rig.Prog({0xA9, 0xF8, 0x53, 0x02,                                //
              0xD3, 0x00, 0x21, 0x00, 0x22, 0x04, 0x00,               // TIN $2100 -> $2200 x4
              0xC3, 0x03, 0x21, 0x03, 0x23, 0x04, 0x00,               // TDD $2103 -> $2303 x4 (both decrement)
              0xE3, 0x00, 0x21, 0x00, 0x24, 0x04, 0x00,               // TIA $2100 -> $2400/$2401 alternating x4
              0xF3, 0x00, 0x21, 0x00, 0x25, 0x04, 0x00});             // TAI $2100/$2101 alternating -> $2500.. x4
    rig.Put(0x1F0100, {1, 2, 3, 4});
    rig.Boot();
    rig.Step(2);
    rig.Step(1);
    Check(rig.mem[0x1F0200] == 4 && rig.mem[0x1F0201] == 0,
          "TIN: source increments, destination fixed (last byte 4 remains at the single destination address)");
    rig.Step(1);
    Check(rig.mem[0x1F0300] == 1 && rig.mem[0x1F0301] == 2 && rig.mem[0x1F0302] == 3 && rig.mem[0x1F0303] == 4,
          "TDD: both addresses decrement, so the block arrives in order");
    rig.Step(1);
    Check(rig.mem[0x1F0400] == 3 && rig.mem[0x1F0401] == 4 && rig.mem[0x1F0402] == 0,
          "TIA: source increments, destination alternates between two addresses (last writes 3 then 4)");
    rig.Step(1);
    Check(rig.mem[0x1F0500] == 1 && rig.mem[0x1F0501] == 2 && rig.mem[0x1F0502] == 1 && rig.mem[0x1F0503] == 2,
          "TAI: source alternates between two addresses, destination increments");
}

void H6280BranchesAndBitBranches() {
    std::printf("H6280BranchesAndBitBranches\n");
    H6280Rig rig;
    rig.Prog({0xA9, 0x00,        // E400 LDA #$00 (Z set)
              0xF0, 0x02,        // E402 BEQ +2 (taken -> E406)
              0xA9, 0xFF,        // E404 (skipped)
              0xD0, 0x02,        // E406 BNE +2 (not taken)
              0x80, 0x02,        // E408 BRA +2 (-> E40C)
              0xA9, 0xFF,        // E40A (skipped)
              0x0F, 0x10, 0x02,  // E40C BBR0 $10,+2 (bit 0 of zp[$10] is set -> not taken)
              0x8F, 0x10, 0x02,  // E40F BBS0 $10,+2 (taken -> E414)
              0xA9, 0xFF,        // E412 (skipped)
              0xA9, 0x42});      // E414 LDA #$42
    rig.Put(0x0010, {0x01});     // mapper register 1 is still 0 here: zero page is physical 0x0010
    rig.Boot();
    rig.Step(1);
    Check(rig.Step(1) == 16, "BEQ taken costs 4 cycles == 16 clocks");
    Check(rig.cpu.Snapshot().pc == 0xE406, "BEQ landed on its target");
    Check(rig.Step(1) == 8, "BNE not taken costs 2 cycles == 8 clocks");
    Check(rig.Step(1) == 16, "BRA costs 4 cycles == 16 clocks");
    Check(rig.cpu.Snapshot().pc == 0xE40C, "BRA landed on its target");
    Check(rig.Step(1) == 24, "BBR0 not taken costs 6 cycles == 24 clocks");
    Check(rig.Step(1) == 32, "BBS0 taken costs 8 cycles == 32 clocks");
    Check(rig.cpu.Snapshot().pc == 0xE414, "BBS0 landed on its target");
    rig.Step(1);
    Check(rig.cpu.Snapshot().a == 0x42, "the skipped LDA #$FF instructions never ran");
}

void H6280DecimalAndBinaryArithmeticFlags() {
    std::printf("H6280DecimalAndBinaryArithmeticFlags\n");
    H6280Rig rig;
    rig.Prog({0xF8, 0x18, 0xA9, 0x19, 0x69, 0x28,  // SED; CLC; LDA #$19; ADC #$28
              0xD8, 0x18, 0xA9, 0x7F, 0x69, 0x01,  // CLD; CLC; LDA #$7F; ADC #$01
              0x38, 0xA9, 0x10, 0xE9, 0x01,        // SEC; LDA #$10; SBC #$01
              0x18, 0xA9, 0xFF, 0x69, 0x01});      // CLC; LDA #$FF; ADC #$01
    rig.Boot();
    rig.Step(3);
    Check(rig.Step(1) == 12, "decimal ADC #imm takes 2+1 cycles == 12 clocks");
    auto snap = rig.cpu.Snapshot();
    Check(snap.a == 0x47 && !Has(snap, kFlagC), "SED: $19 + $28 == $47 (BCD), no carry");
    rig.Step(4);
    snap = rig.cpu.Snapshot();
    Check(snap.a == 0x80 && Has(snap, kFlagV) && Has(snap, kFlagN) && !Has(snap, kFlagC) && !Has(snap, kFlagZ),
          "binary ADC: $7F + $01 == $80 with V and N set, C and Z clear");
    rig.Step(3);
    snap = rig.cpu.Snapshot();
    Check(snap.a == 0x0F && Has(snap, kFlagC) && !Has(snap, kFlagV), "SBC: $10 - $01 with carry set == $0F, C stays set");
    rig.Step(3);
    snap = rig.cpu.Snapshot();
    Check(snap.a == 0x00 && Has(snap, kFlagC) && Has(snap, kFlagZ), "ADC: $FF + $01 == $00 with C and Z set");
}

void H6280TransferFlagAndRegisterSwaps() {
    std::printf("H6280TransferFlagAndRegisterSwaps\n");
    H6280Rig rig;
    rig.Prog({0xA2, 0x01, 0xA0, 0x02, 0x02,        // LDX #1; LDY #2; SXY
              0xA9, 0x05, 0x22, 0x42,              // LDA #5; SAX; SAY
              0x62, 0x82, 0xC2,                    // CLA; CLX; CLY
              0xA9, 0xF8, 0x53, 0x02,              // LDA #$F8; TAM #$02
              0xA2, 0x10, 0xA9, 0x05,              // LDX #$10; LDA #$05
              0xF4, 0x09, 0x0F,                    // SET; ORA #$0F  (T mode: zp[X] |= imm, A untouched)
              0x03, 0x11, 0x13, 0x22, 0x23, 0x33});  // ST0 #$11; ST1 #$22; ST2 #$33 (VDC ports - no VDC here)
    rig.Put(0x1F0010, {0xF0});
    rig.Boot();
    rig.Step(3);
    auto snap = rig.cpu.Snapshot();
    Check(snap.x == 2 && snap.y == 1, "SXY swaps X and Y");
    rig.Step(3);
    snap = rig.cpu.Snapshot();
    // after SAX: A=X(2), X=5; after SAY: A=Y(1), Y=A(2)
    Check(snap.a == 1 && snap.x == 5 && snap.y == 2, "SAX then SAY swap A with X then A with Y");
    rig.Step(3);
    snap = rig.cpu.Snapshot();
    Check(snap.a == 0 && snap.x == 0 && snap.y == 0, "CLA/CLX/CLY clear the registers");
    rig.Step(4);  // LDA #$F8; TAM; LDX #$10; LDA #$05
    rig.Step(1);  // SET
    Check(Has(rig.cpu.Snapshot(), kFlagT), "SET sets the T flag");
    const std::uint64_t clocks = rig.Step(1);  // ORA #$0F in T mode
    snap = rig.cpu.Snapshot();
    Check(rig.mem[0x1F0010] == 0xFF && snap.a == 0x05, "T mode: ORA #imm ORs into zero page [X] and leaves A alone");
    Check(!Has(snap, kFlagT), "the T flag is cleared after the memory operation");
    Check(clocks == 20, "ORA #imm in T mode costs 2+3 cycles == 20 clocks");
    const std::size_t writesBefore = rig.externalWrites;
    Check(rig.Step(1) == 20 && rig.Step(1) == 20 && rig.Step(1) == 20, "ST0/ST1/ST2 cost 5 cycles == 20 clocks each");
    Check(rig.externalWrites == writesBefore, "ST0/ST1/ST2 target the VDC: nothing reaches the memory callbacks");
}

void H6280BitOpsTstTsbTrbRmbSmb() {
    std::printf("H6280BitOpsTstTsbTrbRmbSmb\n");
    H6280Rig rig;
    rig.Prog({0xA9, 0xF8, 0x53, 0x02,        // LDA #$F8; TAM #$02
              0x83, 0x0F, 0x10,              // TST #$0F,$10
              0xA9, 0x0F, 0x04, 0x10,        // LDA #$0F; TSB $10
              0x14, 0x10,                    // TRB $10
              0x07, 0x11, 0x87, 0x11,        // RMB0 $11; SMB0 $11
              0x77, 0x11, 0xF7, 0x11});      // RMB7 $11; SMB7 $11
    rig.Put(0x1F0010, {0xC0});
    rig.Put(0x1F0011, {0xFF});
    rig.Boot();
    rig.Step(2);
    Check(rig.Step(1) == 28, "TST #imm,zp costs 7 cycles == 28 clocks");
    auto snap = rig.cpu.Snapshot();
    Check(Has(snap, kFlagN) && Has(snap, kFlagV) && Has(snap, kFlagZ) && !Has(snap, kFlagT),
          "TST #$0F,[$C0]: N and V from memory bits 7/6, Z set because $C0 & $0F == 0");
    rig.Step(2);
    snap = rig.cpu.Snapshot();
    Check(rig.mem[0x1F0010] == 0xCF && Has(snap, kFlagN) && Has(snap, kFlagV) && !Has(snap, kFlagZ),
          "TSB sets the accumulator bits in memory (flags come from the old value)");
    rig.Step(1);
    Check(rig.mem[0x1F0010] == 0xC0, "TRB clears the accumulator bits in memory");
    rig.Step(1);
    Check(rig.mem[0x1F0011] == 0xFE, "RMB0 clears bit 0");
    rig.Step(1);
    Check(rig.mem[0x1F0011] == 0xFF, "SMB0 sets bit 0");
    rig.Step(1);
    Check(rig.mem[0x1F0011] == 0x7F, "RMB7 clears bit 7");
    rig.Step(1);
    Check(rig.mem[0x1F0011] == 0xFF, "SMB7 sets bit 7");
}

void H6280StackSubroutinesAndTransfers() {
    std::printf("H6280StackSubroutinesAndTransfers\n");
    H6280Rig rig;
    rig.Prog({0xA9, 0xF8, 0x53, 0x02,        // E400 LDA #$F8; TAM #$02
              0x20, 0x30, 0xE4,              // E404 JSR $E430
              0xBA,                          // E407 TSX
              0xA2, 0x5A, 0xDA,              // E408 LDX #$5A; PHX
              0xA2, 0x00, 0xFA,              // E40B LDX #$00; PLX
              0x4C, 0x20, 0xE4});            // E40E JMP $E420
    rig.Put(0x0430, {0xA2, 0x07, 0x60});     // E430 LDX #$07; RTS
    rig.Put(0x0420, {0x44, 0x02, 0xEA, 0xEA, 0xEA});  // E420 BSR +2 ; NOP NOP NOP (target E424)
    rig.Boot();
    rig.Step(3);  // LDA, TAM, JSR
    auto snap = rig.cpu.Snapshot();
    Check(snap.pc == 0xE430 && snap.s == 0xFD, "JSR jumps and pushes two bytes");
    Check(rig.mem[0x1F01FF] == 0xE4 && rig.mem[0x1F01FE] == 0x06, "JSR pushes the return address minus one ($E406), high byte first");
    rig.Step(2);  // LDX #7, RTS
    snap = rig.cpu.Snapshot();
    Check(snap.pc == 0xE407 && snap.s == 0xFF && snap.x == 7, "RTS returns to the instruction after the JSR");
    rig.Step(1);  // TSX
    Check(rig.cpu.Snapshot().x == 0xFF, "TSX copies S into X");
    rig.Step(2);  // LDX #$5A; PHX
    Check(rig.mem[0x1F01FF] == 0x5A, "PHX pushes X");
    rig.Step(2);  // LDX #0; PLX
    Check(rig.cpu.Snapshot().x == 0x5A, "PLX pulls into X");
    rig.Step(1);  // JMP
    Check(rig.cpu.Snapshot().pc == 0xE420, "JMP absolute");
    const std::uint64_t clocks = rig.Step(1);  // BSR
    Check(clocks == 32, "BSR costs 8 cycles == 32 clocks");
    Check(rig.cpu.Snapshot().pc == 0xE424, "BSR branches relative to the byte after its operand");
}

void H6280InternalIoPageIsNotForwarded() {
    std::printf("H6280InternalIoPageIsNotForwarded\n");
    H6280Rig rig;
    // mapper register 2 = $FF puts logical 0x4000-0x5FFF on physical 0x1FE000-0x1FFFFF (the core's I/O page)
    rig.Prog({0xA9, 0xFF, 0x53, 0x04,        // LDA #$FF; TAM #$04
              0xA9, 0x05, 0x8D, 0x00, 0x4C,  // LDA #5; STA $4C00  (timer preload)
              0xA9, 0x02, 0x8D, 0x02, 0x54,  // LDA #2; STA $5402  (interrupt mask: IRQ1 disabled)
              0xAD, 0x00, 0x48,              // LDA $4800          (PSG page read = I/O buffer)
              0xAD, 0x03, 0x54});            // LDA $5403          (interrupt status)
    rig.Boot();
    rig.Step(2);
    const std::size_t writesBefore = rig.externalWrites;
    rig.Step(2);
    auto snap = rig.cpu.Snapshot();
    Check(snap.timerLoad == (5 + 1) * 1024 && snap.timerStatus == 0,
          "timer preload register: load == (5+1)*1024*scale(1), counter not enabled yet");
    rig.Step(2);
    snap = rig.cpu.Snapshot();
    Check(snap.irqMask == 0x02, "interrupt mask register write stored");
    Check(rig.externalWrites == writesBefore, "timer / interrupt register writes never reach the memory callbacks");
    rig.Step(1);
    Check(rig.cpu.Snapshot().a == 0x02, "reading the PSG page returns the last value written to the I/O buffer");
    rig.cpu.SetInputLine(H6280InputLine::Irq1, H6280InputLineState::Assert);
    rig.Step(1);
    snap = rig.cpu.Snapshot();
    Check((snap.a & 0x07) == 0x02 && snap.irq1, "interrupt status register reports IRQ1 (bit 1) while it is asserted (even if masked)");
}

void H6280InterruptsVectorsMaskPriorityAndRti() {
    std::printf("H6280InterruptsVectorsMaskPriorityAndRti\n");
    // Common layout: E400 CLI; E401 NOP; E402 BRA E401. Handlers: IRQ1 E500 (INX; RTI), IRQ2 E520 (INY; RTI),
    // NMI E560 (INC A; RTI).
    auto build = [](H6280Rig& rig) {
        rig.Prog({0x58, 0xEA, 0x80, 0xFD});
        rig.Put(0x0500, {0xE8, 0x40});
        rig.Put(0x0520, {0xC8, 0x40});
        rig.Put(0x0560, {0x1A, 0x40});
        rig.Vector(0x1FF8, 0xE500);
        rig.Vector(0x1FF6, 0xE520);
        rig.Vector(0x1FFC, 0xE560);
    };
    auto stepUntilCount = [](H6280Rig& rig, int limit) {
        for (int i = 0; i < limit; ++i) {
            rig.Step(1);
            if (rig.cpu.Snapshot().irqServiceCount + rig.cpu.Snapshot().nmiServiceCount > 0) {
                return true;
            }
        }
        return false;
    };

    {  // IRQ1: vector 0xFFF8, RTI returns to the loop
        H6280Rig rig;
        build(rig);
        rig.Boot();
        rig.Step(1);  // CLI
        rig.cpu.SetInputLine(H6280InputLine::Irq1, H6280InputLineState::Assert);
        Check(stepUntilCount(rig, 10), "IRQ1 is taken within a few instructions once I is clear");
        Check(rig.cpu.Snapshot().pc == 0xE500, "IRQ1 jumps through the vector at 0xFFF8");
        rig.cpu.SetInputLine(H6280InputLine::Irq1, H6280InputLineState::Clear);
        rig.Step(10);
        const auto snap = rig.cpu.Snapshot();
        Check(snap.x == 1 && snap.irqServiceCount == 1, "handler ran exactly once (INX), the level is gone after RTI");
        Check(snap.pc >= 0xE401 && snap.pc <= 0xE403, "RTI resumed the interrupted loop");
    }
    {  // priority: IRQ1 before IRQ2 when both are pending
        H6280Rig rig;
        build(rig);
        rig.Boot();
        rig.Step(1);
        rig.cpu.SetInputLine(H6280InputLine::Irq2, H6280InputLineState::Assert);
        rig.cpu.SetInputLine(H6280InputLine::Irq1, H6280InputLineState::Assert);
        stepUntilCount(rig, 10);
        Check(rig.cpu.Snapshot().pc == 0xE500, "with IRQ1 and IRQ2 pending, IRQ1 (0xFFF8) is taken first");
        rig.cpu.SetInputLine(H6280InputLine::Irq1, H6280InputLineState::Clear);
        bool reachedIrq2 = false;
        for (int i = 0; i < 30 && !reachedIrq2; ++i) {
            rig.Step(1);
            reachedIrq2 = rig.cpu.Snapshot().pc == 0xE520;
        }
        Check(reachedIrq2, "IRQ2 (vector 0xFFF6) follows once IRQ1 is gone");
    }
    {  // mask register: bit set = disabled. Mask $02 disables IRQ1, mask $00 enables it.
        for (int maskValue : {0x02, 0x00}) {
            H6280Rig rig;
            build(rig);
            // replace the loop with: LDA #$FF; TAM #$04; LDA #mask; STA $5402; CLI; NOP; BRA
            rig.Prog({0xA9, 0xFF, 0x53, 0x04, 0xA9, static_cast<std::uint8_t>(maskValue), 0x8D, 0x02, 0x54, 0x58, 0xEA,
                      0x80, 0xFD});
            rig.Boot();
            rig.Step(6);  // through CLI
            rig.cpu.SetInputLine(H6280InputLine::Irq1, H6280InputLineState::Assert);
            rig.Step(30);
            if (maskValue == 0x02) {
                Check(rig.cpu.Snapshot().irqServiceCount == 0, "IRQ1 masked in the interrupt mask register is never taken");
            } else {
                Check(rig.cpu.Snapshot().irqServiceCount >= 1, "IRQ1 with mask 0 is taken");
            }
        }
    }
    {  // NMI: edge, vector 0xFFFC
        H6280Rig rig;
        build(rig);
        rig.Boot();
        rig.Step(1);
        rig.cpu.SetInputLine(H6280InputLine::Nmi, H6280InputLineState::Assert);
        stepUntilCount(rig, 10);
        auto snap = rig.cpu.Snapshot();
        Check(snap.pc == 0xE560 && snap.nmiServiceCount == 1, "NMI jumps through the vector at 0xFFFC");
        rig.Step(10);
        snap = rig.cpu.Snapshot();
        Check(snap.a == 1 && snap.nmiServiceCount == 1, "the NMI handler ran once (INC A); NMI is an edge, not a level");
    }
    {  // I flag blocks maskable interrupts
        H6280Rig rig;
        rig.Prog({0xEA, 0x80, 0xFD});
        rig.Vector(0x1FF8, 0xE500);
        rig.Boot();
        rig.cpu.SetInputLine(H6280InputLine::Irq1, H6280InputLineState::Assert);
        rig.Step(30);
        Check(rig.cpu.Snapshot().irqServiceCount == 0 && Has(rig.cpu.Snapshot(), kFlagI), "IRQ1 is not taken while the I flag is set");
    }
    {  // BRK uses the IRQ2/BRK vector and sets I, clears D
        H6280Rig rig;
        rig.Prog({0x00});
        rig.Vector(0x1FF6, 0xE520);
        rig.Boot();
        rig.Step(1);
        const auto snap = rig.cpu.Snapshot();
        Check(snap.pc == 0xE520 && Has(snap, kFlagI) && !Has(snap, kFlagD), "BRK jumps through 0xFFF6 with I set and D clear");
    }
}

void H6280TimerFiresTiqAndScaleDoublesPeriod() {
    std::printf("H6280TimerFiresTiqAndScaleDoublesPeriod\n");
    for (int scale : {1, 2}) {
        H6280Rig rig;
        rig.cpu.SetTimerScale(scale);
        // E400: mapper 2 = $FF; preload 1 (period (1+1)*1024*scale clocks); enable; CLI; loop.
        rig.Prog({0xA9, 0xFF, 0x53, 0x04, 0xA9, 0x01, 0x8D, 0x00, 0x4C, 0xA9, 0x01, 0x8D, 0x01, 0x4C, 0x58, 0xEA, 0x80,
                  0xFD});
        // TIQ handler E540: LDA #0; STA $5403 (acknowledge); INX; RTI
        rig.Put(0x0540, {0xA9, 0x00, 0x8D, 0x03, 0x54, 0xE8, 0x40});
        rig.Vector(0x1FFA, 0xE540);
        rig.Boot();
        rig.Step(6);  // through the enable write: the counter reloads when this store lands
        Check(rig.cpu.Snapshot().timerStatus == 1, "timer enable register write starts the counter");
        rig.Step(1);  // CLI
        const std::uint64_t period = 2048ull * static_cast<std::uint64_t>(scale);
        std::uint64_t clocks = 0;
        std::uint64_t firstFire = 0;
        for (int i = 0; i < 5000 && clocks < 3 * period + 400; ++i) {
            clocks += rig.cpu.Execute(1);
            if (firstFire == 0 && rig.cpu.Snapshot().irqServiceCount == 1) {
                firstFire = clocks;
            }
        }
        // 3 periods elapse in 3*period clocks; the loop above ran a little past it.
        Check(firstFire >= period - 40 && firstFire <= period + 200,
              scale == 1 ? "timer scale 1: TIQ is taken about 2048 clocks after the counter was enabled"
                         : "timer scale 2: the period doubles (about 4096 clocks)");
        const auto snap = rig.cpu.Snapshot();
        Check(snap.x == 3 && snap.irqServiceCount == 3, "the acknowledged, reloading timer fires once per period (3 fires)");
    }
}

void H6280SpeedModeCshCsl() {
    std::printf("H6280SpeedModeCshCsl\n");
    H6280Rig rig;
    rig.Prog({0xD4, 0xEA, 0x54, 0xEA});  // CSH; NOP; CSL; NOP
    rig.Boot();
    Check(rig.Step(1) == 12, "CSH costs 3 cycles at the old (low) speed == 12 clocks");
    Check(rig.cpu.Snapshot().clocksPerCycle == 1, "CSH selects high speed (1 clock per cycle)");
    Check(rig.Step(1) == 2, "NOP in high speed mode is 2 clocks");
    Check(rig.Step(1) == 3, "CSL costs 3 cycles == 3 clocks at high speed");
    Check(rig.cpu.Snapshot().clocksPerCycle == 4, "CSL restores low speed");
    Check(rig.Step(1) == 8, "NOP back at 8 clocks");
}

void H6280CyclesConsumedThisCallAdvancesInsideExecute() {
    std::printf("H6280CyclesConsumedThisCallAdvancesInsideExecute\n");
    H6280Rig rig;
    rig.Prog({0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA, 0xEA});
    std::vector<std::uint64_t> samples;
    rig.cpu.SetMemoryCallbacks(
        [&](std::uint32_t addr) -> std::uint8_t {
            samples.push_back(rig.cpu.CyclesConsumedThisCall());
            return rig.mem[addr];
        },
        [&](std::uint32_t addr, std::uint8_t v) { rig.mem[addr] = v; });
    rig.Boot();
    samples.clear();
    const std::uint64_t total = rig.cpu.Execute(40);
    bool monotonic = !samples.empty();
    for (std::size_t i = 1; i < samples.size(); ++i) {
        monotonic = monotonic && samples[i] >= samples[i - 1];
    }
    Check(monotonic, "CyclesConsumedThisCall never decreases while Execute() runs");
    Check(!samples.empty() && samples.front() == 0 && samples.back() > 0 && samples.back() <= total,
          "it starts at 0 for the first fetch and reaches (at most) the clocks consumed by the call");
    Check(total >= 40, "Execute() consumes at least the budget");
    Check(rig.cpu.CyclesConsumedThisCall() == 0, "outside Execute() it reads 0");
}

void H6280DeterministicAcrossRuns() {
    std::printf("H6280DeterministicAcrossRuns\n");
    auto run = [](H6280Rig& rig) {
        rig.cpu.SetTimerScale(2);
        rig.Prog({0xA9, 0xFF, 0x53, 0x04, 0xA9, 0x03, 0x8D, 0x00, 0x4C, 0xA9, 0x01, 0x8D, 0x01, 0x4C, 0xA9, 0xF8, 0x53,
                  0x02, 0x58, 0xE8, 0x18, 0x69, 0x07, 0x85, 0x20, 0x80, 0xF8});
        rig.Put(0x0540, {0xA9, 0x00, 0x8D, 0x03, 0x54, 0xC8, 0x40});
        rig.Vector(0x1FFA, 0xE540);
        rig.Boot();
        rig.cpu.Execute(60000);
    };
    H6280Rig a, b;
    run(a);
    run(b);
    const auto sa = a.cpu.Snapshot();
    const auto sb = b.cpu.Snapshot();
    Check(sa.pc == sb.pc && sa.a == sb.a && sa.x == sb.x && sa.y == sb.y && sa.s == sb.s && sa.p == sb.p &&
              sa.timerValue == sb.timerValue && sa.irqServiceCount == sb.irqServiceCount,
          "two independent runs of the same program end in identical CPU state");
    Check(a.mem == b.mem, "and identical memory");
    Check(sa.irqServiceCount > 0, "the program really exercised the timer interrupt");
}
