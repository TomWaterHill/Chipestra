// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// RP2A03 CPU (IM6502Core + MAME-derived backend) native tests. All programs are hand-written
// synthetic byte sequences (no real ROM/game code).
//
// Expected values (cycle counts, flag results, stack contents) are derived independently from
// the documented 6502 behaviour: the arithmetic is checked against a small reference model
// written in this file over the whole operand space, and the cycle counts against the
// standard per-addressing-mode timing rules. Nothing is read back from the adapter to build
// an expectation.

#include "rp2a03_adapter_tests.h"

#include "devices/cpu/m6502/mame_derived_rp2a03_adapter.h"
#include "test_support.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <utility>
#include <vector>

namespace {

constexpr std::uint8_t kN = 0x80, kV = 0x40, kB = 0x10, kD = 0x08, kI = 0x04, kZ = 0x02, kC = 0x01;

struct Rig {
    std::vector<std::uint8_t> mem = std::vector<std::uint8_t>(0x10000, 0);
    MameDerivedRp2a03Adapter cpu;
    std::function<void(std::uint16_t, std::uint8_t)> onWrite;
    std::function<void(std::uint16_t)> onRead;
    std::vector<std::uint16_t> reads;
    std::vector<std::pair<std::uint16_t, std::uint8_t>> writes;
    bool logging = false;
    std::uint64_t bootCycles = 0;

    Rig() {
        cpu.SetMemoryCallbacks(
            [this](std::uint16_t addr) -> std::uint8_t {
                if (logging) reads.push_back(addr);
                if (onRead) onRead(addr);
                return mem[addr];
            },
            [this](std::uint16_t addr, std::uint8_t v) {
                if (logging) writes.emplace_back(addr, v);
                mem[addr] = v;
                if (onWrite) onWrite(addr, v);
            });
    }

    void Clear() { std::fill(mem.begin(), mem.end(), std::uint8_t{0}); }

    void Put(std::uint16_t addr, std::initializer_list<std::uint8_t> bytes) {
        std::uint16_t i = 0;
        for (std::uint8_t b : bytes) mem[static_cast<std::uint16_t>(addr + i++)] = b;
    }

    void Prog(std::initializer_list<std::uint8_t> bytes) { Put(0x0400, bytes); }

    void Vector(std::uint16_t vectorAddr, std::uint16_t target) {
        mem[vectorAddr] = static_cast<std::uint8_t>(target & 0xFF);
        mem[static_cast<std::uint16_t>(vectorAddr + 1)] = static_cast<std::uint8_t>(target >> 8);
    }

    // Runs the reset sequence and returns the cycles it takes (reported by the first Execute()).
    void Boot(std::uint16_t entry = 0x0400) {
        Vector(0xFFFC, entry);
        cpu.Reset();
        bootCycles = cpu.Execute(0);
    }

    std::uint64_t Step(int n) {
        std::uint64_t total = 0;
        for (int i = 0; i < n; ++i) total += cpu.Execute(1);
        return total;
    }

    MameDerivedRp2a03Adapter::RegisterSnapshot Snap() const { return cpu.Snapshot(); }
};

// ---- reference model (documented 6502 arithmetic, independent of the adapter) ----

struct Ref {
    std::uint8_t a = 0, p = 0;  // p: only N V Z C are modelled
};

std::uint8_t NzBits(std::uint8_t v) { return static_cast<std::uint8_t>((v & 0x80 ? kN : 0) | (v == 0 ? kZ : 0)); }

Ref RefAdc(std::uint8_t a, std::uint8_t m, bool c) {
    unsigned r = a + m + (c ? 1 : 0);
    std::uint8_t res = static_cast<std::uint8_t>(r);
    Ref out;
    out.a = res;
    out.p = static_cast<std::uint8_t>(NzBits(res) | (r > 0xFF ? kC : 0) | (((a ^ res) & (m ^ res) & 0x80) ? kV : 0));
    return out;
}

Ref RefSbc(std::uint8_t a, std::uint8_t m, bool c) { return RefAdc(a, static_cast<std::uint8_t>(~m), c); }

std::uint8_t RefCmpFlags(std::uint8_t reg, std::uint8_t m) {
    return static_cast<std::uint8_t>(NzBits(static_cast<std::uint8_t>(reg - m)) | (reg >= m ? kC : 0));
}

// P with only the modelled bits replaced; the others (I, D, B, bit 5) keep their reset value.
std::uint8_t ExpectedP(std::uint8_t modelled, bool d) {
    return static_cast<std::uint8_t>(0x30 | kI | (d ? kD : 0) | modelled);
}

// ---- cycle tables ----

struct CycleEntry {
    int op;
    int cycles;
};

void AddGroup(std::vector<CycleEntry>& t, int base, std::initializer_list<std::pair<int, int>> offsetCycles) {
    for (auto [off, cyc] : offsetCycles) t.push_back({base + off, cyc});
}

std::vector<CycleEntry> OfficialTable() {
    std::vector<CycleEntry> t;
    // ORA AND EOR ADC LDA CMP SBC: izx zp imm abs izy zpx aby abx
    for (int base : {0x01, 0x21, 0x41, 0x61, 0xA1, 0xC1, 0xE1})
        AddGroup(t, base, {{0x00, 6}, {0x04, 3}, {0x08, 2}, {0x0C, 4}, {0x10, 5}, {0x14, 4}, {0x18, 4}, {0x1C, 4}});
    // STA
    AddGroup(t, 0x81, {{0x00, 6}, {0x04, 3}, {0x0C, 4}, {0x10, 6}, {0x14, 4}, {0x18, 5}, {0x1C, 5}});
    // ASL ROL LSR ROR: zp acc abs zpx abx
    for (int base : {0x06, 0x26, 0x46, 0x66})
        AddGroup(t, base, {{0x00, 5}, {0x04, 2}, {0x08, 6}, {0x10, 6}, {0x18, 7}});
    // DEC INC: zp abs zpx abx
    for (int base : {0xC6, 0xE6}) AddGroup(t, base, {{0x00, 5}, {0x08, 6}, {0x10, 6}, {0x18, 7}});
    // LDX, LDY, STX, STY, CPX, CPY, BIT
    t.insert(t.end(), {{0xA2, 2}, {0xA6, 3}, {0xAE, 4}, {0xB6, 4}, {0xBE, 4}});
    t.insert(t.end(), {{0xA0, 2}, {0xA4, 3}, {0xAC, 4}, {0xB4, 4}, {0xBC, 4}});
    t.insert(t.end(), {{0x86, 3}, {0x8E, 4}, {0x96, 4}, {0x84, 3}, {0x8C, 4}, {0x94, 4}});
    t.insert(t.end(), {{0xE0, 2}, {0xE4, 3}, {0xEC, 4}, {0xC0, 2}, {0xC4, 3}, {0xCC, 4}, {0x24, 3}, {0x2C, 4}});
    // implied 2-cycle: CLC SEC CLI SEI CLV CLD SED TAX TXA TAY TYA TSX TXS DEX DEY INX INY NOP
    for (int op : {0x18, 0x38, 0x58, 0x78, 0xB8, 0xD8, 0xF8, 0xAA, 0x8A, 0xA8, 0x98, 0xBA, 0x9A, 0xCA, 0x88, 0xE8, 0xC8, 0xEA})
        t.push_back({op, 2});
    // stack and flow
    t.insert(t.end(), {{0x48, 3}, {0x08, 3}, {0x68, 4}, {0x28, 4}, {0x4C, 3}, {0x6C, 5}, {0x20, 6}, {0x60, 6}, {0x40, 6}, {0x00, 7}});
    // branches: reset leaves N=0 V=0 Z=1 C=0, so BPL BVC BCC BEQ are taken (3) and BMI BVS BCS BNE are not (2)
    t.insert(t.end(), {{0x10, 3}, {0x50, 3}, {0x90, 3}, {0xF0, 3}, {0x30, 2}, {0x70, 2}, {0xB0, 2}, {0xD0, 2}});
    return t;
}

std::vector<CycleEntry> UndocumentedTable() {
    std::vector<CycleEntry> t;
    // SLO RLA SRE RRA DCP ISB: zp zpx abs abx aby izx izy
    for (int base : {0x07, 0x27, 0x47, 0x67, 0xC7, 0xE7})
        AddGroup(t, base, {{0x00, 5}, {0x10, 6}, {0x08, 6}, {0x18, 7}, {0x14, 7}, {0xFC, 8}, {0x0C, 8}});
    // LAX: zp zpy abs aby izx izy
    t.insert(t.end(), {{0xA7, 3}, {0xB7, 4}, {0xAF, 4}, {0xBF, 4}, {0xA3, 6}, {0xB3, 5}});
    // SAX: zp zpy abs izx
    t.insert(t.end(), {{0x87, 3}, {0x97, 4}, {0x8F, 4}, {0x83, 6}});
    // immediate: ANC (two opcodes), ASR, ARR, ANE, LXA, SBX, SBC alias, and the 2-byte NOPs
    for (int op : {0x0B, 0x2B, 0x4B, 0x6B, 0x8B, 0xAB, 0xCB, 0xEB, 0x80, 0x82, 0x89, 0xC2, 0xE2})
        t.push_back({op, 2});
    for (int op : {0x1A, 0x3A, 0x5A, 0x7A, 0xDA, 0xFA}) t.push_back({op, 2});
    for (int op : {0x04, 0x44, 0x64}) t.push_back({op, 3});
    for (int op : {0x14, 0x34, 0x54, 0x74, 0xD4, 0xF4, 0x0C}) t.push_back({op, 4});
    for (int op : {0x1C, 0x3C, 0x5C, 0x7C, 0xDC, 0xFC}) t.push_back({op, 4});
    // SHA aby / izy, SHS, SHX, SHY, LAS
    t.insert(t.end(), {{0x9F, 5}, {0x93, 6}, {0x9B, 5}, {0x9E, 5}, {0x9C, 5}, {0xBB, 4}});
    return t;
}

constexpr std::uint8_t kKilOpcodes[] = {0x02, 0x12, 0x22, 0x32, 0x42, 0x52, 0x62, 0x72, 0x92, 0xB2, 0xD2, 0xF2};

// Operand layout that keeps every effective address on one page and in plain RAM
// (X=0x80, Y=0 after reset): zp operand 0x10, absolute operand 0x0210.
void PrepareOneInstruction(Rig& rig, std::uint8_t opcode) {
    rig.Clear();
    rig.Prog({opcode, 0x10, 0x02});
    rig.Put(0x0010, {0x00, 0x03});   // (zp),Y pointer -> 0x0300
    rig.Vector(0xFFFE, 0x0500);
    rig.Boot();
}

}  // namespace

void Rp2a03ResetState() {
    std::printf("Rp2a03ResetState\n");
    Rig rig;
    rig.Prog({0xEA});
    rig.Boot(0x0400);
    auto s = rig.Snap();
    Check(rig.bootCycles == 8, "reset takes 8 cycles (7 for the sequence, 1 for the first opcode fetch)");
    Check(s.pc == 0x0400, "reset: PC comes from the reset vector at 0xFFFC/0xFFFD");
    Check(s.ir == 0xEA, "reset: the first opcode is already fetched");
    Check(s.s == 0xFD, "reset: S is 0xFD (three dummy pushes from 0x00)");
    Check((s.p & kI) != 0, "reset: the I flag is set");
    Check(s.a == 0 && s.x == 0x80 && s.y == 0, "reset: A=0, X=0x80, Y=0 as in the original");
    Check(!s.jammed && s.nmiServiceCount == 0 && s.irqServiceCount == 0, "reset: not jammed, no interrupts served");
}

void Rp2a03ResetBusSequence() {
    std::printf("Rp2a03ResetBusSequence\n");
    Rig rig;
    rig.Prog({0xEA});
    rig.Vector(0xFFFC, 0x0400);
    rig.logging = true;
    rig.cpu.Reset();
    const std::vector<std::uint16_t> expected = {0x0000, 0x0000, 0x0100, 0x01FF, 0x01FE, 0xFFFC, 0xFFFD, 0x0400};
    Check(rig.reads == expected, "reset: bus reads are PC,PC,0x100,0x1FF,0x1FE, both vector bytes, then the first opcode");
    Check(rig.writes.empty(), "reset: nothing is written to memory");
}

void Rp2a03LoadStoreFlagsAndCycles() {
    std::printf("Rp2a03LoadStoreFlagsAndCycles\n");
    Rig rig;
    // LDA #0; LDA #$80; LDX #$FF; LDY #$7F; STA $0200; STX $10; STY $11,X
    rig.Prog({0xA9, 0x00, 0xA9, 0x80, 0xA2, 0xFF, 0xA0, 0x7F, 0x8D, 0x00, 0x02, 0x86, 0x10, 0x94, 0x11});
    rig.Boot();
    Check(rig.Step(1) == 2 && (rig.Snap().p & (kN | kZ)) == kZ, "LDA #0: 2 cycles, Z set, N clear");
    Check(rig.Step(1) == 2 && (rig.Snap().p & (kN | kZ)) == kN, "LDA #$80: N set, Z clear");
    Check(rig.Step(1) == 2 && rig.Snap().x == 0xFF, "LDX #$FF");
    Check(rig.Step(1) == 2 && rig.Snap().y == 0x7F, "LDY #$7F");
    Check(rig.Step(1) == 4 && rig.mem[0x0200] == 0x80, "STA abs: 4 cycles, stores A");
    Check(rig.Step(1) == 3 && rig.mem[0x0010] == 0xFF, "STX zp: 3 cycles");
    Check(rig.Step(1) == 4 && rig.mem[0x0010] == 0x7F, "STY zp,X: 4 cycles, X=$FF wraps inside the zero page to $10");
}

void Rp2a03OfficialOpcodeCycleCounts() {
    std::printf("Rp2a03OfficialOpcodeCycleCounts\n");
    const auto table = OfficialTable();
    Check(table.size() == 151, "the table lists all 151 documented opcodes");
    Rig rig;
    int bad = 0;
    for (const auto& e : table) {
        PrepareOneInstruction(rig, static_cast<std::uint8_t>(e.op));
        const std::uint64_t cycles = rig.Step(1);
        if (cycles != static_cast<std::uint64_t>(e.cycles)) {
            ++bad;
            std::printf("    opcode %02X: expected %d cycles, got %llu\n", e.op, e.cycles,
                        static_cast<unsigned long long>(cycles));
        }
    }
    Check(bad == 0, "every documented opcode takes its documented cycle count (no page crossing)");
}

void Rp2a03UndocumentedOpcodeCycleCounts() {
    std::printf("Rp2a03UndocumentedOpcodeCycleCounts\n");
    const auto table = UndocumentedTable();
    Check(table.size() + 151 + sizeof(kKilOpcodes) == 256, "documented + undocumented + KIL opcodes cover all 256 opcodes");
    Rig rig;
    int bad = 0;
    for (const auto& e : table) {
        PrepareOneInstruction(rig, static_cast<std::uint8_t>(e.op));
        const std::uint64_t cycles = rig.Step(1);
        if (cycles != static_cast<std::uint64_t>(e.cycles)) {
            ++bad;
            std::printf("    opcode %02X: expected %d cycles, got %llu\n", e.op, e.cycles,
                        static_cast<unsigned long long>(cycles));
        }
    }
    Check(bad == 0, "every undocumented opcode takes its cycle count (no page crossing)");
}

void Rp2a03PageCrossPenaltyAndDummyReads() {
    std::printf("Rp2a03PageCrossPenaltyAndDummyReads\n");
    {
        Rig rig;
        rig.Prog({0xA2, 0x20, 0xBD, 0xF0, 0x02, 0xBD, 0x00, 0x03});  // LDX #$20; LDA $02F0,X; LDA $0300,X
        rig.mem[0x0310] = 0x77;
        rig.mem[0x0320] = 0x99;
        rig.Boot();
        rig.Step(1);
        rig.logging = true;
        const auto c1 = rig.Step(1);
        Check(c1 == 5 && rig.Snap().a == 0x77, "LDA abs,X across a page: 5 cycles, value read from the right page");
        bool dummy = false;
        for (auto a : rig.reads) dummy = dummy || a == 0x0210;
        Check(dummy, "LDA abs,X across a page: the uncorrected address 0x0210 is read first");
        Check(rig.Step(1) == 4, "LDA abs,X inside a page: 4 cycles");
    }
    {
        Rig rig;
        rig.Prog({0xA2, 0x01, 0x9D, 0x00, 0x03});  // LDX #1; STA $0300,X
        rig.Boot();
        rig.Step(1);
        rig.logging = true;
        Check(rig.Step(1) == 5, "STA abs,X takes 5 cycles even without a page crossing");
        bool dummy = false;
        for (auto a : rig.reads) dummy = dummy || a == 0x0301;
        Check(dummy, "STA abs,X: a dummy read at the final address happens before the write");
    }
    {
        Rig rig;
        rig.Prog({0xA0, 0x10, 0xB1, 0x20});  // LDY #$10; LDA ($20),Y
        rig.Put(0x0020, {0xF8, 0x04});       // pointer 0x04F8 + 0x10 crosses into page 5
        rig.Boot();
        rig.Step(1);
        Check(rig.Step(1) == 6, "LDA (zp),Y across a page: 6 cycles");
    }
}

void Rp2a03AdcSbcExhaustiveBinary() {
    std::printf("Rp2a03AdcSbcExhaustiveBinary\n");
    Rig rig;
    int badAdc = 0, badSbc = 0;
    for (int d = 0; d < 2; ++d) {
        for (int c = 0; c < 2; ++c) {
            for (int a = 0; a < 256; ++a) {
                for (int m = 0; m < 256; ++m) {
                    // [SED] LDA #a; CLC|SEC; ADC #m / SBC #m
                    for (int op = 0; op < 2; ++op) {
                        std::uint16_t pc = 0x0400;
                        auto put = [&](std::uint8_t b) { rig.mem[pc++] = b; };
                        if (d) put(0xF8);
                        put(0xA9);
                        put(static_cast<std::uint8_t>(a));
                        put(c ? 0x38 : 0x18);
                        put(op == 0 ? 0x69 : 0xE9);
                        put(static_cast<std::uint8_t>(m));
                        rig.Boot();
                        rig.Step(d ? 4 : 3);
                        const auto s = rig.Snap();
                        const Ref ref = op == 0 ? RefAdc(static_cast<std::uint8_t>(a), static_cast<std::uint8_t>(m), c != 0)
                                                : RefSbc(static_cast<std::uint8_t>(a), static_cast<std::uint8_t>(m), c != 0);
                        const bool ok = s.a == ref.a && s.p == ExpectedP(ref.p, d != 0);
                        if (!ok) {
                            int& bad = op == 0 ? badAdc : badSbc;
                            if (bad++ == 0)
                                std::printf("    first mismatch (%s d=%d c=%d a=%02X m=%02X): A=%02X P=%02X, expected A=%02X P=%02X\n",
                                            op == 0 ? "ADC" : "SBC", d, c, a, m, s.a, s.p, ref.a, ExpectedP(ref.p, d != 0));
                        }
                    }
                }
            }
        }
    }
    Check(badAdc == 0, "ADC #imm matches binary arithmetic for all 2x2x65536 operand/carry/D-flag combinations");
    Check(badSbc == 0, "SBC #imm matches binary arithmetic for all 2x2x65536 operand/carry/D-flag combinations");
}

void Rp2a03DecimalFlagIsIgnored() {
    std::printf("Rp2a03DecimalFlagIsIgnored\n");
    Rig rig;
    // SED; LDA #$09; CLC; ADC #$01 -> $0A in binary (a decimal-mode 6502 would give $10); PHP
    rig.Prog({0xF8, 0xA9, 0x09, 0x18, 0x69, 0x01, 0x08});
    rig.Boot();
    rig.Step(4);
    Check(rig.Snap().a == 0x0A, "SED + ADC #$01 on $09 gives binary $0A");
    Check((rig.Snap().p & kD) != 0, "the D flag is still stored");
    rig.Step(1);
    Check((rig.mem[0x01FD] & kD) != 0, "PHP pushes the D flag");

    Rig rig2;
    // SED; LDA #$10; SEC; SBC #$01 -> $0F in binary (decimal would give $09)
    rig2.Prog({0xF8, 0xA9, 0x10, 0x38, 0xE9, 0x01});
    rig2.Boot();
    rig2.Step(4);
    Check(rig2.Snap().a == 0x0F, "SED + SBC #$01 on $10 gives binary $0F");

    Rig rig3;
    // SED; LDA #$FF; SEC; ARR #$FF: the undocumented ARR also ignores D
    rig3.Prog({0xF8, 0xA9, 0xFF, 0x38, 0x6B, 0xFF});
    rig3.Boot();
    rig3.Step(4);
    Check(rig3.Snap().a == 0xFF && (rig3.Snap().p & kC) != 0, "ARR in decimal-flag mode behaves like binary ARR");

    Rig rig4;
    // SED; LDA #$0F; SEC; ISB $10 (mem $0F -> $10; A=$0F-$10 binary = $FF, borrow)
    rig4.Prog({0xF8, 0xA9, 0x0F, 0x38, 0xE7, 0x10});
    rig4.mem[0x10] = 0x0F;
    rig4.Boot();
    rig4.Step(4);
    Check(rig4.mem[0x10] == 0x10 && rig4.Snap().a == 0xFF && (rig4.Snap().p & kC) == 0,
          "ISB with D set subtracts in binary");

    Rig rig5;
    // SED; LDA #$10; SEC; RRA $10 (mem $02 -> ROR with C=1 -> $81, C=0; A=$10+$81+0 = $91)
    rig5.Prog({0xF8, 0xA9, 0x10, 0x38, 0x67, 0x10});
    rig5.mem[0x10] = 0x02;
    rig5.Boot();
    rig5.Step(4);
    Check(rig5.mem[0x10] == 0x81 && rig5.Snap().a == 0x91, "RRA with D set adds in binary");
}

void Rp2a03LogicCompareBitExhaustive() {
    std::printf("Rp2a03LogicCompareBitExhaustive\n");
    Rig rig;
    int bad = 0;
    for (int a = 0; a < 256; ++a) {
        for (int m = 0; m < 256; ++m) {
            // LDA #a; LDX #a; LDY #a; AND #m; (new run) ORA / EOR / CMP / CPX / CPY / BIT
            struct Case {
                std::uint8_t op;
                int kind;
            };
            const Case cases[] = {{0x29, 0}, {0x09, 1}, {0x49, 2}, {0xC9, 3}, {0xE0, 4}, {0xC0, 5}};
            for (const auto& cs : cases) {
                rig.mem[0x0400] = 0xA9;
                rig.mem[0x0401] = static_cast<std::uint8_t>(a);
                rig.mem[0x0402] = 0xA2;
                rig.mem[0x0403] = static_cast<std::uint8_t>(a);
                rig.mem[0x0404] = 0xA0;
                rig.mem[0x0405] = static_cast<std::uint8_t>(a);
                rig.mem[0x0406] = cs.op;
                rig.mem[0x0407] = static_cast<std::uint8_t>(m);
                rig.Boot();
                rig.Step(4);
                const auto s = rig.Snap();
                std::uint8_t expectA = static_cast<std::uint8_t>(a);
                std::uint8_t expectModelled = 0;
                const std::uint8_t keepV = 0;  // V is not touched by these instructions (reset value 0)
                switch (cs.kind) {
                case 0: expectA = static_cast<std::uint8_t>(a & m); expectModelled = NzBits(expectA); break;
                case 1: expectA = static_cast<std::uint8_t>(a | m); expectModelled = NzBits(expectA); break;
                case 2: expectA = static_cast<std::uint8_t>(a ^ m); expectModelled = NzBits(expectA); break;
                default: expectModelled = RefCmpFlags(static_cast<std::uint8_t>(a), static_cast<std::uint8_t>(m)); break;
                }
                // LDY/LDX/LDA set N/Z but the last compare/logic op overwrites them; C stays as reset (0) for logic ops.
                if (s.a != expectA || s.p != ExpectedP(static_cast<std::uint8_t>(expectModelled | keepV), false)) {
                    if (bad++ == 0)
                        std::printf("    first mismatch (kind %d a=%02X m=%02X): A=%02X P=%02X\n", cs.kind, a, m, s.a, s.p);
                }
            }
            // BIT zp: N and V come from memory, Z from A & m
            rig.mem[0x0400] = 0xA9;
            rig.mem[0x0401] = static_cast<std::uint8_t>(a);
            rig.mem[0x0402] = 0x24;
            rig.mem[0x0403] = 0x10;
            rig.mem[0x0404] = 0;
            rig.mem[0x0405] = 0;
            rig.mem[0x0406] = 0;
            rig.mem[0x0407] = 0;
            rig.mem[0x0010] = static_cast<std::uint8_t>(m);
            rig.Boot();
            rig.Step(2);
            const auto s = rig.Snap();
            const std::uint8_t expect = static_cast<std::uint8_t>((m & 0x80 ? kN : 0) | (m & 0x40 ? kV : 0) | ((a & m) == 0 ? kZ : 0));
            if (s.p != ExpectedP(expect, false) || s.a != a) {
                if (bad++ == 0) std::printf("    first BIT mismatch (a=%02X m=%02X): P=%02X\n", a, m, s.p);
            }
            rig.mem[0x0010] = 0;
        }
    }
    Check(bad == 0, "AND, ORA, EOR, CMP, CPX, CPY and BIT match the reference for all 65536 operand pairs");
}

void Rp2a03ShiftRotateIncDec() {
    std::printf("Rp2a03ShiftRotateIncDec\n");
    Rig rig;
    int bad = 0;
    for (int v = 0; v < 256; ++v) {
        for (int c = 0; c < 2; ++c) {
            // LDA #v; CLC|SEC; then each of ASL A, LSR A, ROL A, ROR A in its own run
            const struct {
                std::uint8_t op;
                int kind;
            } ops[] = {{0x0A, 0}, {0x4A, 1}, {0x2A, 2}, {0x6A, 3}};
            for (const auto& o : ops) {
                rig.mem[0x0400] = 0xA9;
                rig.mem[0x0401] = static_cast<std::uint8_t>(v);
                rig.mem[0x0402] = c ? 0x38 : 0x18;
                rig.mem[0x0403] = o.op;
                rig.Boot();
                rig.Step(3);
                const auto s = rig.Snap();
                std::uint8_t res = 0;
                bool carry = false;
                switch (o.kind) {
                case 0: res = static_cast<std::uint8_t>(v << 1); carry = v & 0x80; break;
                case 1: res = static_cast<std::uint8_t>(v >> 1); carry = v & 1; break;
                case 2: res = static_cast<std::uint8_t>((v << 1) | c); carry = v & 0x80; break;
                default: res = static_cast<std::uint8_t>((v >> 1) | (c ? 0x80 : 0)); carry = v & 1; break;
                }
                const std::uint8_t modelled = static_cast<std::uint8_t>(NzBits(res) | (carry ? kC : 0));
                if (s.a != res || s.p != ExpectedP(modelled, false)) {
                    if (bad++ == 0) std::printf("    first mismatch (kind %d v=%02X c=%d): A=%02X P=%02X\n", o.kind, v, c, s.a, s.p);
                }
            }
        }
        // memory forms: INC zp / DEC zp
        rig.mem[0x0400] = 0xE6;
        rig.mem[0x0401] = 0x10;
        rig.mem[0x0402] = 0xC6;
        rig.mem[0x0403] = 0x10;
        rig.mem[0x0010] = static_cast<std::uint8_t>(v);
        rig.Boot();
        rig.Step(1);
        const std::uint8_t inc = static_cast<std::uint8_t>(v + 1);
        if (rig.mem[0x10] != inc || rig.Snap().p != ExpectedP(NzBits(inc), false)) ++bad;
        rig.Step(1);
        if (rig.mem[0x10] != v || rig.Snap().p != ExpectedP(NzBits(static_cast<std::uint8_t>(v)), false)) ++bad;
        rig.mem[0x0010] = 0;
    }
    Check(bad == 0, "ASL, LSR, ROL, ROR on A and INC/DEC on memory match the reference for every value and carry");

    // Read-modify-write writes the old value back before the new one (visible to I/O registers).
    Rig rig2;
    rig2.Prog({0xE6, 0x10});
    rig2.mem[0x10] = 0x41;
    rig2.Boot();
    rig2.logging = true;
    rig2.Step(1);
    Check(rig2.writes.size() == 2 && rig2.writes[0].second == 0x41 && rig2.writes[1].second == 0x42,
          "INC zp writes the unmodified value first and the result second");
}

void Rp2a03AddressingWraparound() {
    std::printf("Rp2a03AddressingWraparound\n");
    {
        Rig rig;
        rig.Prog({0xA2, 0x10, 0xB5, 0xF8});  // LDX #$10; LDA $F8,X
        rig.mem[0x0008] = 0x5A;
        rig.mem[0x0108] = 0xA5;
        rig.Boot();
        rig.Step(2);
        Check(rig.Snap().a == 0x5A, "LDA zp,X wraps inside the zero page ($F8+$10 -> $08)");
    }
    {
        Rig rig;
        rig.Prog({0xA2, 0x04, 0xA1, 0xFD});  // LDX #4; LDA ($FD,X): pointer at $01/$02
        rig.Put(0x0001, {0x00, 0x03});
        rig.mem[0x0300] = 0x6B;
        rig.Boot();
        rig.Step(2);
        Check(rig.Snap().a == 0x6B, "LDA (zp,X) wraps the pointer address inside the zero page");
    }
    {
        Rig rig;
        rig.Prog({0xA2, 0x00, 0xA1, 0xFF});  // LDX #0; LDA ($FF,X): lo at $FF, hi at $00
        rig.mem[0x00FF] = 0x34;
        rig.mem[0x0000] = 0x02;
        rig.mem[0x0234] = 0x3C;
        rig.mem[0x0100] = 0x99;
        rig.Boot();
        rig.Step(2);
        Check(rig.Snap().a == 0x3C, "LDA (zp,X) reads the pointer's high byte from $00 when the low byte is at $FF");
    }
    {
        Rig rig;
        rig.Prog({0xA0, 0x00, 0xB1, 0xFF});  // LDY #0; LDA ($FF),Y
        rig.mem[0x00FF] = 0x34;
        rig.mem[0x0000] = 0x02;
        rig.mem[0x0234] = 0x4D;
        rig.Boot();
        rig.Step(2);
        Check(rig.Snap().a == 0x4D, "LDA (zp),Y reads the pointer's high byte from $00 when the low byte is at $FF");
    }
    {
        Rig rig;
        rig.Prog({0x6C, 0xFF, 0x02});  // JMP ($02FF)
        rig.mem[0x02FF] = 0x00;
        rig.mem[0x0300] = 0x09;  // would be the high byte if the page were crossed
        rig.mem[0x0200] = 0x07;  // the high byte the 6502 actually reads
        rig.Boot();
        rig.Step(1);
        Check(rig.Snap().pc == 0x0700, "JMP (ind) with a pointer at $xxFF takes its high byte from the start of the same page");
    }
}

void Rp2a03BranchesTakenAndPageCross() {
    std::printf("Rp2a03BranchesTakenAndPageCross\n");
    {
        Rig rig;
        rig.Prog({0xA9, 0x01, 0xD0, 0x05});  // LDA #1 (Z=0); BNE +5
        rig.Boot();
        rig.Step(1);
        Check(rig.Step(1) == 3 && rig.Snap().pc == 0x0409, "BNE taken inside the page: 3 cycles");
    }
    {
        Rig rig;
        rig.Prog({0xA9, 0x00, 0xD0, 0x05});  // LDA #0 (Z=1); BNE not taken
        rig.Boot();
        rig.Step(1);
        Check(rig.Step(1) == 2 && rig.Snap().pc == 0x0404, "BNE not taken: 2 cycles");
    }
    {
        Rig rig;
        rig.Put(0x04F0, {0xA9, 0x01, 0xD0, 0x20});  // BNE at $04F2 -> target $04F4+$20 = $0514: crosses a page
        rig.Boot(0x04F0);
        rig.Step(1);
        Check(rig.Step(1) == 4 && rig.Snap().pc == 0x0514, "BNE taken across a page: 4 cycles");
    }
    {
        Rig rig;
        rig.Put(0x0410, {0xA2, 0x03, 0xCA, 0xD0, 0xFD});  // LDX #3; loop: DEX; BNE loop
        rig.Boot(0x0410);
        const std::uint64_t total = rig.Step(1 + 3 + 3);
        // LDX 2 + 3*(DEX 2) + BNE taken twice (3 each) + BNE not taken once (2) = 2+6+6+2
        Check(total == 16 && rig.Snap().x == 0 && rig.Snap().pc == 0x0415, "backward branch loop runs 3 times and takes 16 cycles");
    }
}

void Rp2a03StackAndSubroutines() {
    std::printf("Rp2a03StackAndSubroutines\n");
    {
        Rig rig;
        rig.Prog({0xA9, 0x42, 0x48, 0xA9, 0x00, 0x68});  // LDA #$42; PHA; LDA #0; PLA
        rig.Boot();
        rig.Step(2);
        Check(rig.mem[0x01FD] == 0x42 && rig.Snap().s == 0xFC, "PHA writes to $01FD and decrements S");
        rig.Step(1);
        Check(rig.Step(1) == 4 && rig.Snap().a == 0x42 && rig.Snap().s == 0xFD, "PLA restores A and S");
    }
    {
        Rig rig;
        rig.Prog({0xA2, 0x00, 0x9A, 0xA9, 0x77, 0x48});  // LDX #0; TXS; LDA #$77; PHA
        rig.Boot();
        rig.Step(4);
        Check(rig.mem[0x0100] == 0x77 && rig.Snap().s == 0xFF, "PHA with S=0 writes $0100 and S wraps to $FF");
    }
    {
        Rig rig;
        rig.Prog({0x20, 0x00, 0x06, 0xEA});  // JSR $0600; NOP
        rig.mem[0x0600] = 0x60;              // RTS
        rig.Boot();
        Check(rig.Step(1) == 6 && rig.Snap().pc == 0x0600, "JSR: 6 cycles");
        Check(rig.mem[0x01FD] == 0x04 && rig.mem[0x01FC] == 0x02, "JSR pushes the address of its last byte ($0402), high byte first");
        Check(rig.Step(1) == 6 && rig.Snap().pc == 0x0403 && rig.Snap().s == 0xFD, "RTS: 6 cycles, returns to the byte after JSR");
    }
    {
        Rig rig;
        rig.Prog({0x38, 0xF8, 0x08, 0x18, 0xB8, 0x28});  // SEC; SED; PHP; CLC; CLV; PLP
        rig.Boot();
        rig.Step(3);
        Check(rig.mem[0x01FD] == (0x30 | kI | kD | kC | kZ), "PHP pushes P with the B bit and bit 5 set");
        rig.Step(3);
        const auto s = rig.Snap();
        Check((s.p & (kD | kC)) == (kD | kC), "PLP restores D and C");
    }
    {
        Rig rig;
        rig.Prog({0xA2, 0x80, 0x9A, 0xBA});  // LDX #$80; TXS; TSX
        rig.Boot();
        rig.Step(2);
        rig.Step(1);
        const auto s = rig.Snap();
        Check(s.x == 0x80 && (s.p & kN) != 0, "TSX copies S and sets N");
    }
}

void Rp2a03BrkAndRti() {
    std::printf("Rp2a03BrkAndRti\n");
    Rig rig;
    rig.Prog({0x00, 0xFF, 0xA9, 0x5A});  // BRK (+ padding byte); LDA #$5A
    rig.Vector(0xFFFE, 0x0600);
    rig.mem[0x0600] = 0x40;  // RTI
    rig.Boot();
    const auto c = rig.Step(1);
    const auto s = rig.Snap();
    Check(c == 7, "BRK takes 7 cycles");
    Check(s.pc == 0x0600 && (s.p & kI) != 0, "BRK jumps through the IRQ vector with I set");
    Check(rig.mem[0x01FD] == 0x04 && rig.mem[0x01FC] == 0x02, "BRK pushes the address after the padding byte ($0402)");
    Check((rig.mem[0x01FB] & kB) != 0, "BRK pushes P with the B bit set");
    Check(s.irqServiceCount == 0 && s.nmiServiceCount == 0, "a BRK is not counted as a hardware interrupt");
    Check(rig.Step(1) == 6 && rig.Snap().pc == 0x0402, "RTI: 6 cycles, returns after the padding byte");
    rig.Step(1);
    Check(rig.Snap().a == 0x5A, "execution continues after the BRK");
}

void Rp2a03IrqTakenMaskedAndLevelTriggered() {
    std::printf("Rp2a03IrqTakenMaskedAndLevelTriggered\n");
    {
        Rig rig;
        rig.Prog({0xEA, 0xEA, 0xEA, 0xEA});  // I is set after reset
        rig.Vector(0xFFFE, 0x0700);
        rig.mem[0x0700] = 0x40;
        rig.Boot();
        rig.cpu.SetInputLine(M6502InputLine::Irq, M6502InputLineState::Assert);
        rig.Step(3);
        Check(rig.Snap().irqServiceCount == 0 && rig.Snap().pc == 0x0403, "IRQ is ignored while the I flag is set");
    }
    {
        Rig rig;
        rig.Prog({0x58, 0xEA, 0xEA, 0xEA});  // CLI; NOP; NOP; NOP
        rig.Vector(0xFFFE, 0x0700);
        rig.mem[0x0700] = 0x40;              // RTI
        rig.Boot();
        rig.cpu.SetInputLine(M6502InputLine::Irq, M6502InputLineState::Assert);
        rig.Step(2);   // CLI and the NOP after it: CLI clears I only after its own opcode fetch, so one more instruction runs
        const auto c = rig.Step(1);  // the interrupt sequence
        auto s = rig.Snap();
        Check(c == 7 && s.pc == 0x0700 && s.irqServiceCount == 1, "IRQ is taken one instruction after CLI: 7 cycles, vector $FFFE, counted once");
        Check(rig.mem[0x01FD] == 0x04 && rig.mem[0x01FC] == 0x02, "the pushed PC is the interrupted instruction ($0402)");
        Check((rig.mem[0x01FB] & kB) == 0 && (rig.mem[0x01FB] & kI) == 0, "the pushed P has B clear and the pre-interrupt I flag (clear)");
        Check((s.p & kI) != 0, "I is set inside the handler");
        // The line is still asserted: RTI restores I=0, so the IRQ fires again (level-triggered).
        rig.Step(2);
        Check(rig.Snap().irqServiceCount == 2, "a still-asserted IRQ line is taken again after RTI");
        rig.cpu.SetInputLine(M6502InputLine::Irq, M6502InputLineState::Clear);
        rig.Step(1);  // handler RTI
        rig.Step(3);
        Check(rig.Snap().irqServiceCount == 2, "no further IRQ once the line is released");
    }
    {
        Rig rig;
        rig.Prog({0x58, 0xEA, 0xEA});
        rig.Vector(0xFFFE, 0x0700);
        rig.mem[0x0700] = 0x40;
        rig.Boot();
        rig.cpu.SetInputLine(M6502InputLine::ApuIrq, M6502InputLineState::Assert);
        rig.Step(3);
        Check(rig.Snap().irqServiceCount == 1 && rig.Snap().pc == 0x0700, "the on-chip sound generator's IRQ line is taken like IRQ");
    }
}

void Rp2a03NmiEdgeAndPriority() {
    std::printf("Rp2a03NmiEdgeAndPriority\n");
    {
        Rig rig;
        rig.Prog({0xEA, 0xEA, 0xEA, 0xEA});
        rig.Vector(0xFFFA, 0x0800);
        rig.mem[0x0800] = 0x40;  // RTI
        rig.Boot();
        rig.cpu.SetInputLine(M6502InputLine::Nmi, M6502InputLineState::Assert);
        rig.Step(1);                   // NOP; its opcode fetch sees the NMI
        const auto c = rig.Step(1);    // the interrupt sequence
        auto s = rig.Snap();
        Check(c == 7 && s.pc == 0x0800 && s.nmiServiceCount == 1, "NMI: 7 cycles, vector $FFFA, counted once");
        Check(rig.mem[0x01FD] == 0x04 && rig.mem[0x01FC] == 0x01, "NMI pushes the address of the interrupted instruction ($0401)");
        Check((rig.mem[0x01FB] & kB) == 0, "NMI pushes P with B clear");
        rig.Step(3);  // RTI and two NOPs with the line still asserted
        Check(rig.Snap().nmiServiceCount == 1, "a held NMI line does not retrigger (edge-triggered)");
        rig.cpu.SetInputLine(M6502InputLine::Nmi, M6502InputLineState::Clear);
        rig.cpu.SetInputLine(M6502InputLine::Nmi, M6502InputLineState::Assert);
        rig.Step(2);
        Check(rig.Snap().nmiServiceCount == 2, "releasing and asserting the line again gives a new NMI");
    }
    {
        Rig rig;
        rig.Prog({0x58, 0xEA, 0xEA});  // CLI makes IRQ eligible too
        rig.Vector(0xFFFA, 0x0800);
        rig.Vector(0xFFFE, 0x0700);
        rig.mem[0x0800] = 0x40;
        rig.mem[0x0700] = 0x40;
        rig.Boot();
        rig.cpu.SetInputLine(M6502InputLine::Nmi, M6502InputLineState::Assert);
        rig.cpu.SetInputLine(M6502InputLine::Irq, M6502InputLineState::Assert);
        rig.Step(2);
        Check(rig.Snap().pc == 0x0800 && rig.Snap().nmiServiceCount == 1 && rig.Snap().irqServiceCount == 0,
              "NMI wins over a simultaneous IRQ");
    }
}

void Rp2a03NmiIgnoredBeforeFirstCycle() {
    std::printf("Rp2a03NmiIgnoredBeforeFirstCycle\n");
    Rig rig;
    rig.Prog({0xEA, 0xEA, 0xEA, 0xEA, 0xEA});
    rig.Vector(0xFFFC, 0x0400);
    rig.Vector(0xFFFA, 0x0800);
    rig.cpu.Reset();
    rig.cpu.SetInputLine(M6502InputLine::Nmi, M6502InputLineState::Assert);  // at the moment RESET is released
    rig.cpu.Execute(0);  // the reset cycles
    rig.Step(4);
    Check(rig.Snap().nmiServiceCount == 0 && rig.Snap().pc == 0x0404, "an NMI edge before any cycle has run is not accepted");
}

void Rp2a03NmiDuringBrkTakesNmiVector() {
    std::printf("Rp2a03NmiDuringBrkTakesNmiVector\n");
    Rig rig;
    rig.Prog({0x00, 0x00});
    rig.Vector(0xFFFE, 0x0600);
    rig.Vector(0xFFFA, 0x0800);
    rig.Boot();
    rig.onWrite = [&](std::uint16_t addr, std::uint8_t) {
        if (addr == 0x01FD) rig.cpu.SetInputLine(M6502InputLine::Nmi, M6502InputLineState::Assert);
    };
    rig.Step(1);
    const auto s = rig.Snap();
    Check(s.pc == 0x0800, "an NMI arriving while BRK pushes its state redirects the fetch to the NMI vector");
    Check((rig.mem[0x01FB] & kB) != 0, "the pushed P still has B set (it was a BRK)");
    Check(s.nmiServiceCount == 1 && s.irqServiceCount == 0, "the redirect counts as an NMI, not an IRQ");
}

void Rp2a03SetOverflowPin() {
    std::printf("Rp2a03SetOverflowPin\n");
    Rig rig;
    rig.Prog({0xB8, 0xEA, 0xB8, 0xEA});  // CLV; NOP; CLV; NOP
    rig.Boot();
    rig.cpu.SetInputLine(M6502InputLine::SetOverflow, M6502InputLineState::Assert);
    Check((rig.Snap().p & kV) != 0, "asserting the SO pin sets V");
    rig.Step(1);  // CLV
    Check((rig.Snap().p & kV) == 0, "CLV clears V");
    rig.Step(1);
    Check((rig.Snap().p & kV) == 0, "a held SO pin does not set V again");
    rig.cpu.SetInputLine(M6502InputLine::SetOverflow, M6502InputLineState::Clear);
    rig.cpu.SetInputLine(M6502InputLine::SetOverflow, M6502InputLineState::Assert);
    Check((rig.Snap().p & kV) != 0, "a new SO edge sets V again");
}

void Rp2a03UndocumentedReadModifyWriteOpcodes() {
    std::printf("Rp2a03UndocumentedReadModifyWriteOpcodes\n");
    {
        Rig rig;  // SLO $10: ASL memory, then ORA into A
        rig.Prog({0xA9, 0x10, 0x07, 0x10});
        rig.mem[0x10] = 0x81;
        rig.Boot();
        rig.Step(2);
        auto s = rig.Snap();
        Check(rig.mem[0x10] == 0x02 && s.a == 0x12 && (s.p & kC) != 0, "SLO = ASL memory then ORA");
    }
    {
        Rig rig;  // RLA $10 with C=1
        rig.Prog({0xA9, 0xFF, 0x38, 0x27, 0x10});
        rig.mem[0x10] = 0x81;
        rig.Boot();
        rig.Step(3);
        auto s = rig.Snap();
        Check(rig.mem[0x10] == 0x03 && s.a == 0x03 && (s.p & kC) != 0, "RLA = ROL memory then AND");
    }
    {
        Rig rig;  // SRE $10
        rig.Prog({0xA9, 0x0F, 0x47, 0x10});
        rig.mem[0x10] = 0x03;
        rig.Boot();
        rig.Step(2);
        auto s = rig.Snap();
        Check(rig.mem[0x10] == 0x01 && s.a == 0x0E && (s.p & kC) != 0, "SRE = LSR memory then EOR");
    }
    {
        Rig rig;  // RRA $10 with C=1: ROR 03 -> 81 (C=1), A = 10 + 81 + 1
        rig.Prog({0xA9, 0x10, 0x38, 0x67, 0x10});
        rig.mem[0x10] = 0x03;
        rig.Boot();
        rig.Step(3);
        auto s = rig.Snap();
        Check(rig.mem[0x10] == 0x81 && s.a == 0x92 && (s.p & kC) == 0 && (s.p & kV) == 0, "RRA = ROR memory then ADC with the new carry");
    }
    {
        Rig rig;  // DCP $10: decrement then compare
        rig.Prog({0xA9, 0x0F, 0xC7, 0x10});
        rig.mem[0x10] = 0x10;
        rig.Boot();
        rig.Step(2);
        auto s = rig.Snap();
        Check(rig.mem[0x10] == 0x0F && (s.p & (kZ | kC)) == (kZ | kC) && s.a == 0x0F, "DCP = DEC memory then CMP");
    }
    {
        Rig rig;  // ISB $10 with C=1: 7F -> 80; A = 10 - 80
        rig.Prog({0xA9, 0x10, 0x38, 0xE7, 0x10});
        rig.mem[0x10] = 0x7F;
        rig.Boot();
        rig.Step(3);
        auto s = rig.Snap();
        Check(rig.mem[0x10] == 0x80 && s.a == 0x90 && (s.p & kC) == 0 && (s.p & kV) != 0 && (s.p & kN) != 0,
              "ISB = INC memory then SBC");
    }
}

void Rp2a03UndocumentedLoadStoreAndImmediateOpcodes() {
    std::printf("Rp2a03UndocumentedLoadStoreAndImmediateOpcodes\n");
    {
        Rig rig;  // LAX $10
        rig.Prog({0xA7, 0x10});
        rig.mem[0x10] = 0x8F;
        rig.Boot();
        rig.Step(1);
        auto s = rig.Snap();
        Check(s.a == 0x8F && s.x == 0x8F && (s.p & kN) != 0, "LAX loads A and X together and sets N");
    }
    {
        Rig rig;  // SAX $10: A=F0 & X=3C
        rig.Prog({0xA9, 0xF0, 0xA2, 0x3C, 0x87, 0x10});
        rig.Boot();
        rig.Step(2);
        const auto before = rig.Snap().p;
        rig.Step(1);
        Check(rig.mem[0x10] == 0x30 && rig.Snap().p == before, "SAX stores A AND X and leaves the flags alone");
    }
    {
        Rig rig;  // ANC #: C = N
        rig.Prog({0xA9, 0x80, 0x0B, 0xFF});
        rig.Boot();
        rig.Step(2);
        auto s = rig.Snap();
        Check(s.a == 0x80 && (s.p & kN) != 0 && (s.p & kC) != 0, "ANC ANDs and copies bit 7 into C");
    }
    {
        Rig rig;  // ASR #: A = LSR(A & imm)
        rig.Prog({0xA9, 0xFF, 0x4B, 0x03});
        rig.Boot();
        rig.Step(2);
        auto s = rig.Snap();
        Check(s.a == 0x01 && (s.p & kC) != 0, "ASR ANDs then shifts right");
    }
    {
        Rig rig;  // ARR #: with C=1, A=FF -> FF, C=1, V=0
        rig.Prog({0xA9, 0xFF, 0x38, 0x6B, 0xFF});
        rig.Boot();
        rig.Step(3);
        auto s = rig.Snap();
        Check(s.a == 0xFF && (s.p & kC) != 0 && (s.p & kV) == 0 && (s.p & kN) != 0, "ARR ANDs, rotates right and derives C and V from bits 6 and 5");
    }
    {
        Rig rig;  // SBX #: X = (A & X) - imm
        rig.Prog({0xA9, 0xF0, 0xA2, 0x3C, 0xCB, 0x10});
        rig.Boot();
        rig.Step(3);
        auto s = rig.Snap();
        Check(s.x == 0x20 && (s.p & kC) != 0 && (s.p & (kN | kZ)) == 0, "SBX subtracts the immediate from A AND X without borrow");
    }
    {
        Rig rig;  // LXA #
        rig.Prog({0xAB, 0x55});
        rig.Boot();
        rig.Step(1);
        Check(rig.Snap().a == 0x55 && rig.Snap().x == 0x55, "LXA loads the immediate into A and X");
    }
    {
        Rig rig;  // ANE #: A = A & imm & X
        rig.Prog({0xA9, 0xFF, 0xA2, 0x0F, 0x8B, 0x3C});
        rig.Boot();
        rig.Step(3);
        Check(rig.Snap().a == 0x0C, "ANE ANDs A with the immediate and X");
    }
    {
        Rig rig;  // SBC alias $EB behaves as SBC #imm
        rig.Prog({0xA9, 0x10, 0x38, 0xEB, 0x01});
        rig.Boot();
        rig.Step(3);
        Check(rig.Snap().a == 0x0F, "opcode $EB is a second SBC #imm");
    }
    {
        Rig rig;  // two-byte NOPs consume their operand, three-byte NOPs read their address
        rig.Prog({0x80, 0x55, 0x0C, 0x34, 0x12, 0xEA});
        rig.Boot();
        rig.logging = true;
        rig.Step(2);
        bool read1234 = false;
        for (auto a : rig.reads) read1234 = read1234 || a == 0x1234;
        Check(rig.Snap().pc == 0x0405 && read1234 && rig.writes.empty(), "undocumented NOPs skip their operand bytes and only read");
    }
}

void Rp2a03UndocumentedHighByteStoreOpcodes() {
    std::printf("Rp2a03UndocumentedHighByteStoreOpcodes\n");
    {
        Rig rig;  // SHX $0300,Y with Y=0: value = X & (high+1) = FF & 04
        rig.Prog({0xA2, 0xFF, 0xA0, 0x00, 0x9E, 0x00, 0x03});
        rig.Boot();
        rig.Step(3);
        Check(rig.mem[0x0300] == 0x04, "SHX stores X AND (high byte of the base address + 1)");
    }
    {
        Rig rig;  // SHY $0300,X with X=0
        rig.Prog({0xA0, 0xFF, 0xA2, 0x00, 0x9C, 0x00, 0x03});
        rig.Boot();
        rig.Step(3);
        Check(rig.mem[0x0300] == 0x04, "SHY stores Y AND (high byte of the base address + 1)");
    }
    {
        Rig rig;  // SHA $0300,Y: A & X & (hi+1) = FF & 7F & 04 = 04
        rig.Prog({0xA9, 0xFF, 0xA2, 0x7F, 0xA0, 0x00, 0x9F, 0x00, 0x03});
        rig.Boot();
        rig.Step(4);
        Check(rig.mem[0x0300] == 0x04, "SHA stores A AND X AND (high byte + 1)");
    }
    {
        Rig rig;  // SHS $0300,Y: S = A & X, then the same store as SHA
        rig.Prog({0xA9, 0xFF, 0xA2, 0x0F, 0xA0, 0x00, 0x9B, 0x00, 0x03});
        rig.Boot();
        rig.Step(4);
        Check(rig.Snap().s == 0x0F && rig.mem[0x0300] == 0x04, "SHS sets S to A AND X and stores like SHA");
    }
    {
        Rig rig;  // page crossing: the high byte of the target becomes the stored value
        rig.Prog({0xA2, 0x01, 0xA0, 0x01, 0x9E, 0xFF, 0x01});  // SHX $01FF,Y: value = 01 & 02 = 0 -> stored at $0000
        rig.Boot();
        rig.mem[0x0000] = 0xEE;
        rig.Step(3);
        Check(rig.mem[0x0000] == 0x00 && rig.mem[0x0200] == 0x00, "SHX across a page uses the stored value as the target's high byte");
    }
    {
        Rig rig;  // LAS $0300,Y: A = mem | $51, X = $FF, NZ from mem
        rig.Prog({0xA0, 0x00, 0xBB, 0x00, 0x03});
        rig.mem[0x0300] = 0x12;
        rig.Boot();
        rig.Step(2);
        auto s = rig.Snap();
        Check(s.a == 0x53 && s.x == 0xFF && (s.p & (kN | kZ)) == 0, "LAS follows the implemented behaviour (A = mem OR $51, X = $FF)");
    }
}

void Rp2a03KilJamsTheCpu() {
    std::printf("Rp2a03KilJamsTheCpu\n");
    int notJammed = 0, wrongBudget = 0, notReset = 0;
    for (std::uint8_t op : kKilOpcodes) {
        Rig rig;
        rig.Prog({op, 0xEA});
        rig.Boot();
        const auto first = rig.Step(1);
        if (!rig.Snap().jammed || first != 4) ++notJammed;
        if (rig.cpu.Execute(100) != 100) ++wrongBudget;
        rig.cpu.Reset();
        if (rig.Snap().jammed) ++notReset;
    }
    Check(notJammed == 0, "all 12 KIL opcodes jam the CPU after 4 cycles");
    Check(wrongBudget == 0, "a jammed CPU consumes exactly the requested budget");
    Check(notReset == 0, "Reset() clears the jam");
}

void Rp2a03CyclesConsumedThisCallAdvancesInsideExecute() {
    std::printf("Rp2a03CyclesConsumedThisCallAdvancesInsideExecute\n");
    Rig rig;
    rig.Prog({0xAD, 0x00, 0x20});  // LDA $2000
    rig.Boot();
    std::uint64_t seen = ~0ull;
    rig.onRead = [&](std::uint16_t addr) {
        if (addr == 0x2000) seen = rig.cpu.CyclesConsumedThisCall();
    };
    Check(rig.cpu.CyclesConsumedThisCall() == 0, "CyclesConsumedThisCall is 0 outside Execute()");
    rig.Step(1);
    // opcode fetch of the next instruction happens after the data read, so the data read is the 4th cycle:
    // cycles 1,2 = operand bytes, 3 = the data read (1 already counted before the callback runs).
    Check(seen == 3, "inside the data read, 3 cycles of the instruction have been counted (two operand bytes plus the read)");
    Check(rig.cpu.CyclesConsumedThisCall() == 0, "and 0 again after Execute() returns");
}

void Rp2a03OpcodeObserverSeesEveryInstruction() {
    std::printf("Rp2a03OpcodeObserverSeesEveryInstruction\n");
    Rig rig;
    rig.Prog({0xA9, 0x01, 0xEA, 0x4C, 0x00, 0x04});
    rig.Boot();
    std::vector<std::pair<int, int>> seen;
    rig.cpu.SetOpcodeFetchObserverForTest([&](std::uint16_t pc, std::uint8_t op) { seen.emplace_back(pc, op); });
    rig.Step(4);
    const std::vector<std::pair<int, int>> expected = {{0x0400, 0xA9}, {0x0402, 0xEA}, {0x0403, 0x4C}, {0x0400, 0xA9}};
    Check(seen == expected, "the observer receives (address, opcode) before each instruction");
}

void Rp2a03DeterministicAcrossRuns() {
    std::printf("Rp2a03DeterministicAcrossRuns\n");
    auto run = [](std::vector<std::uint8_t>& memOut) {
        Rig rig;
        // a small loop that mixes memory, stack, interrupts and arithmetic
        rig.Put(0x0400, {0x58, 0xA2, 0x00, 0xE8, 0x8A, 0x69, 0x03, 0x9D, 0x00, 0x03, 0x48, 0x68, 0x4C, 0x03, 0x04});
        rig.Vector(0xFFFE, 0x0700);
        rig.Vector(0xFFFA, 0x0800);
        rig.mem[0x0700] = 0x40;
        rig.mem[0x0800] = 0x40;
        rig.Boot();
        std::uint64_t total = 0;
        for (int i = 0; i < 2000; ++i) {
            if (i % 97 == 0) rig.cpu.SetInputLine(M6502InputLine::Nmi, i % 2 ? M6502InputLineState::Assert : M6502InputLineState::Clear);
            if (i % 131 == 0) rig.cpu.SetInputLine(M6502InputLine::Irq, i % 2 ? M6502InputLineState::Assert : M6502InputLineState::Clear);
            total += rig.cpu.Execute(7);
        }
        memOut = rig.mem;
        auto s = rig.Snap();
        return std::vector<std::uint64_t>{total, s.pc, s.a, s.x, s.p, s.s, s.nmiServiceCount, s.irqServiceCount};
    };
    std::vector<std::uint8_t> m1, m2;
    const auto r1 = run(m1);
    const auto r2 = run(m2);
    Check(r1 == r2 && m1 == m2, "two identical runs end in identical registers, counters and memory");
    Check(r1[6] > 0 && r1[7] > 0, "the run actually served both NMIs and IRQs");
}
