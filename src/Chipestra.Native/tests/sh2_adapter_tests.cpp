// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill

#include "sh2_adapter_tests.h"

#include "devices/cpu/sh2/mame_derived_sh2_adapter.h"
#include "test_support.h"

#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

namespace {

using Registers = MameDerivedSh2Adapter::Registers;
constexpr std::uint32_t kT = MameDerivedSh2Adapter::kT;

// A bench: 128 KB of RAM (big endian, mirrored by the address mask) and the CPU.
struct Bench {
    MameDerivedSh2Adapter cpu;
    std::vector<std::uint8_t> mem = std::vector<std::uint8_t>(0x20000, 0);

    Bench() {
        ISh2Core::Callbacks cb;
        cb.read8 = [this](std::uint32_t a) { return mem[a & 0x1FFFF]; };
        cb.read16 = [this](std::uint32_t a) { return static_cast<std::uint16_t>(mem[a & 0x1FFFF] << 8 | mem[(a + 1) & 0x1FFFF]); };
        cb.read32 = [this](std::uint32_t a) { return R32(a); };
        cb.write8 = [this](std::uint32_t a, std::uint8_t v) { mem[a & 0x1FFFF] = v; };
        cb.write16 = [this](std::uint32_t a, std::uint16_t v) { mem[a & 0x1FFFF] = static_cast<std::uint8_t>(v >> 8); mem[(a + 1) & 0x1FFFF] = static_cast<std::uint8_t>(v); };
        cb.write32 = [this](std::uint32_t a, std::uint32_t v) { W32(a, v); };
        cpu.SetCallbacks(cb);
    }
    std::uint32_t R32(std::uint32_t a) const { return static_cast<std::uint32_t>(mem[a & 0x1FFFF]) << 24 | mem[(a + 1) & 0x1FFFF] << 16 | mem[(a + 2) & 0x1FFFF] << 8 | mem[(a + 3) & 0x1FFFF]; }
    void W32(std::uint32_t a, std::uint32_t v) { for (int i = 0; i < 4; ++i) mem[(a + i) & 0x1FFFF] = static_cast<std::uint8_t>(v >> (24 - 8 * i)); }
    void W16(std::uint32_t a, std::uint16_t v) { mem[a & 0x1FFFF] = static_cast<std::uint8_t>(v >> 8); mem[(a + 1) & 0x1FFFF] = static_cast<std::uint8_t>(v); }
    void Program(std::uint32_t at, std::initializer_list<std::uint16_t> ops) { for (std::uint16_t op : ops) { W16(at, op); at += 2; } }
    void Boot(std::uint32_t pc = 0x1000, std::uint32_t sp = 0x8000) { W32(0, pc); W32(4, sp); cpu.Reset(); }
    // one instruction; returns its cycles
    std::uint64_t Step() { return cpu.Execute(1); }
    std::uint64_t Steps(int n) { std::uint64_t c = 0; for (int i = 0; i < n; ++i) c += Step(); return c; }
    Registers Regs() const { return cpu.GetRegisters(); }
    void Set(const Registers& r) { cpu.SetRegisters(r); }
};

constexpr std::uint16_t Nm(std::uint16_t base, int n, int m) { return static_cast<std::uint16_t>(base | (n << 8) | (m << 4)); }
constexpr std::uint16_t Nn(std::uint16_t base, int n) { return static_cast<std::uint16_t>(base | (n << 8)); }

const std::uint32_t kValues[] = {0u, 1u, 2u, 0x7FFFFFFFu, 0x80000000u, 0x80000001u, 0xFFFFFFFFu, 0xFFFFFFFEu, 0x12345678u, 0x0000FFFFu, 0xFFFF0000u, 0x00008000u};

std::string Hex(std::uint32_t v) { char b[16]; std::snprintf(b, sizeof b, "%08X", v); return b; }

}  // namespace

void Sh2ResetReadsTheVectors() {
    std::printf("Sh2ResetReadsTheVectors\n");
    Bench b;
    b.Boot(0x1234, 0x7FF0);
    const Registers r = b.Regs();
    Check(r.pc == 0x1234 && r.r[15] == 0x7FF0, "PC and R15 come from vectors 0 and 1 (addresses 0 and 4)");
    Check(r.sr == 0xF0, "SR has the interrupt mask 15 and the other flags clear");
    Check(r.r[0] == 0 && r.pr == 0 && r.gbr == 0 && r.vbr == 0 && r.mach == 0 && r.macl == 0, "the other registers are cleared");
}

void Sh2ArithmeticFlagsAgainstAReferenceModel() {
    std::printf("Sh2ArithmeticFlagsAgainstAReferenceModel\n");
    struct Op { const char* name; std::uint16_t word; };
    const Op ops[] = {{"ADD", Nm(0x300C, 0, 1)}, {"ADDC", Nm(0x300E, 0, 1)}, {"ADDV", Nm(0x300F, 0, 1)}, {"SUB", Nm(0x3008, 0, 1)},
                      {"SUBC", Nm(0x300A, 0, 1)}, {"SUBV", Nm(0x300B, 0, 1)}, {"NEGC", Nm(0x600A, 0, 1)}, {"NEG", Nm(0x600B, 0, 1)}};
    std::size_t wrong = 0;
    std::string first;
    for (const Op& op : ops) {
        for (std::uint32_t a : kValues) for (std::uint32_t c : kValues) for (std::uint32_t t = 0; t < 2; ++t) {
            Bench bench;
            bench.Boot();
            bench.Program(0x1000, {op.word});
            Registers r = bench.Regs();
            r.r[0] = a; r.r[1] = c; r.sr = 0xF0 | t;
            bench.Set(r);
            bench.Step();
            const Registers out = bench.Regs();
            const std::string name = op.name;
            std::uint32_t expected = 0, expectedT = t;
            const std::uint64_t ua = a, uc = c;
            if (name == "ADD") expected = a + c;
            else if (name == "ADDC") { const std::uint64_t s = ua + uc + t; expected = static_cast<std::uint32_t>(s); expectedT = static_cast<std::uint32_t>(s >> 32); }
            else if (name == "ADDV") { expected = a + c; expectedT = ((a ^ expected) & (c ^ expected)) >> 31; }
            else if (name == "SUB") expected = a - c;
            else if (name == "SUBC") { expected = a - c - t; expectedT = ua < uc + t ? 1 : 0; }
            else if (name == "SUBV") { expected = a - c; expectedT = ((a ^ c) & (a ^ expected)) >> 31; }
            else if (name == "NEGC") { expected = 0u - c - t; expectedT = (c != 0 || t != 0) ? 1 : 0; }
            else if (name == "NEG") expected = 0u - c;
            const std::uint32_t flags = out.sr & kT;
            const bool tChanges = name != "ADD" && name != "SUB" && name != "NEG";
            if (out.r[0] != expected || (tChanges ? flags != expectedT : flags != t)) {
                ++wrong;
                if (first.empty()) first = name + " " + Hex(a) + "," + Hex(c) + " T=" + std::to_string(t) + " got " + Hex(out.r[0]) + "/" + std::to_string(flags);
            }
        }
    }
    Check(wrong == 0, wrong == 0 ? "ADD, ADDC, ADDV, SUB, SUBC, SUBV, NEG, NEGC match the reference over the value pairs and both T" : first.c_str());

    // comparisons
    std::size_t badCompare = 0;
    for (std::uint32_t a : kValues) for (std::uint32_t c : kValues) {
        struct Cmp { std::uint16_t word; bool expected; };
        const Cmp cmps[] = {{Nm(0x3000, 0, 1), a == c}, {Nm(0x3002, 0, 1), a >= c}, {Nm(0x3003, 0, 1), static_cast<std::int32_t>(a) >= static_cast<std::int32_t>(c)},
                            {Nm(0x3006, 0, 1), a > c}, {Nm(0x3007, 0, 1), static_cast<std::int32_t>(a) > static_cast<std::int32_t>(c)}, {Nm(0x2008, 0, 1), (a & c) == 0},
                            {Nm(0x200C, 0, 1), ((a ^ c) & 0xFF) == 0 || ((a ^ c) & 0xFF00) == 0 || ((a ^ c) & 0xFF0000) == 0 || ((a ^ c) & 0xFF000000u) == 0},
                            {Nn(0x4015, 0), static_cast<std::int32_t>(a) > 0}, {Nn(0x4011, 0), static_cast<std::int32_t>(a) >= 0}};
        for (const Cmp& cmp : cmps) {
            Bench bench;
            bench.Boot();
            bench.Program(0x1000, {cmp.word});
            Registers r = bench.Regs();
            r.r[0] = a; r.r[1] = c;
            bench.Set(r);
            bench.Step();
            if (((bench.Regs().sr & kT) != 0) != cmp.expected) ++badCompare;
        }
    }
    Check(badCompare == 0, "CMP/EQ, HS, GE, HI, GT, PL, PZ, STR and TST give the reference T");
}

void Sh2ShiftsRotatesAndSwaps() {
    std::printf("Sh2ShiftsRotatesAndSwaps\n");
    struct Case { const char* name; std::uint16_t word; std::uint32_t (*value)(std::uint32_t, std::uint32_t t); std::uint32_t (*tbit)(std::uint32_t, std::uint32_t t); };
    const Case cases[] = {
        {"SHLL", Nn(0x4000, 0), [](std::uint32_t v, std::uint32_t) { return v << 1; }, [](std::uint32_t v, std::uint32_t) { return v >> 31; }},
        {"SHAL", Nn(0x4020, 0), [](std::uint32_t v, std::uint32_t) { return v << 1; }, [](std::uint32_t v, std::uint32_t) { return v >> 31; }},
        {"SHLR", Nn(0x4001, 0), [](std::uint32_t v, std::uint32_t) { return v >> 1; }, [](std::uint32_t v, std::uint32_t) { return v & 1; }},
        {"SHAR", Nn(0x4021, 0), [](std::uint32_t v, std::uint32_t) { return static_cast<std::uint32_t>(static_cast<std::int32_t>(v) >> 1); }, [](std::uint32_t v, std::uint32_t) { return v & 1; }},
        {"ROTL", Nn(0x4004, 0), [](std::uint32_t v, std::uint32_t) { return v << 1 | v >> 31; }, [](std::uint32_t v, std::uint32_t) { return v >> 31; }},
        {"ROTR", Nn(0x4005, 0), [](std::uint32_t v, std::uint32_t) { return v >> 1 | v << 31; }, [](std::uint32_t v, std::uint32_t) { return v & 1; }},
        {"ROTCL", Nn(0x4024, 0), [](std::uint32_t v, std::uint32_t t) { return v << 1 | t; }, [](std::uint32_t v, std::uint32_t) { return v >> 31; }},
        {"ROTCR", Nn(0x4025, 0), [](std::uint32_t v, std::uint32_t t) { return v >> 1 | t << 31; }, [](std::uint32_t v, std::uint32_t) { return v & 1; }},
        {"SHLL2", Nn(0x4008, 0), [](std::uint32_t v, std::uint32_t t) { (void)t; return v << 2; }, nullptr},
        {"SHLR2", Nn(0x4009, 0), [](std::uint32_t v, std::uint32_t t) { (void)t; return v >> 2; }, nullptr},
        {"SHLL8", Nn(0x4018, 0), [](std::uint32_t v, std::uint32_t t) { (void)t; return v << 8; }, nullptr},
        {"SHLR8", Nn(0x4019, 0), [](std::uint32_t v, std::uint32_t t) { (void)t; return v >> 8; }, nullptr},
        {"SHLL16", Nn(0x4028, 0), [](std::uint32_t v, std::uint32_t t) { (void)t; return v << 16; }, nullptr},
        {"SHLR16", Nn(0x4029, 0), [](std::uint32_t v, std::uint32_t t) { (void)t; return v >> 16; }, nullptr},
        {"SWAP.B", Nm(0x6008, 0, 0), [](std::uint32_t v, std::uint32_t) { return (v & 0xFFFF0000u) | (v & 0xFF) << 8 | (v >> 8 & 0xFF); }, nullptr},
        {"SWAP.W", Nm(0x6009, 0, 0), [](std::uint32_t v, std::uint32_t) { return v << 16 | v >> 16; }, nullptr},
        {"NOT", Nm(0x6007, 0, 0), [](std::uint32_t v, std::uint32_t) { return ~v; }, nullptr},
        {"EXTU.B", Nm(0x600C, 0, 0), [](std::uint32_t v, std::uint32_t) { return v & 0xFF; }, nullptr},
        {"EXTU.W", Nm(0x600D, 0, 0), [](std::uint32_t v, std::uint32_t) { return v & 0xFFFF; }, nullptr},
        {"EXTS.B", Nm(0x600E, 0, 0), [](std::uint32_t v, std::uint32_t) { return static_cast<std::uint32_t>(static_cast<std::int8_t>(v)); }, nullptr},
        {"EXTS.W", Nm(0x600F, 0, 0), [](std::uint32_t v, std::uint32_t) { return static_cast<std::uint32_t>(static_cast<std::int16_t>(v)); }, nullptr},
    };
    std::size_t wrong = 0;
    std::string first;
    for (const Case& c : cases) for (std::uint32_t v : kValues) for (std::uint32_t t = 0; t < 2; ++t) {
        Bench bench;
        bench.Boot();
        bench.Program(0x1000, {c.word});
        Registers r = bench.Regs();
        r.r[0] = v; r.sr = 0xF0 | t;
        bench.Set(r);
        bench.Step();
        const Registers out = bench.Regs();
        const std::uint32_t expectedT = c.tbit ? c.tbit(v, t) : t;
        if (out.r[0] != c.value(v, t) || (out.sr & kT) != expectedT) {
            ++wrong;
            if (first.empty()) first = std::string(c.name) + " of " + Hex(v) + " T=" + std::to_string(t) + " gave " + Hex(out.r[0]);
        }
    }
    Check(wrong == 0, wrong == 0 ? "the shifts, rotates, swaps, extensions and NOT match the reference (values and T)" : first.c_str());

    Bench bench;
    bench.Boot();
    bench.Program(0x1000, {Nm(0x200D, 0, 1), Nn(0x4010, 2), Nn(0x4010, 2)});
    Registers r = bench.Regs();
    r.r[0] = 0x11112222; r.r[1] = 0x33334444; r.r[2] = 2;
    bench.Set(r);
    bench.Step();
    Check(bench.Regs().r[0] == 0x44441111, "XTRCT puts the low half of Rm above the high half of Rn");
    bench.Step();
    Check(bench.Regs().r[2] == 1 && (bench.Regs().sr & kT) == 0, "DT decrements and clears T while the value is not zero");
    bench.Step();
    Check(bench.Regs().r[2] == 0 && (bench.Regs().sr & kT) != 0, "DT sets T when the value reaches zero");
}

void Sh2MultiplyAndMultiplyAccumulate() {
    std::printf("Sh2MultiplyAndMultiplyAccumulate\n");
    std::size_t wrong = 0;
    std::string first;
    auto fail = [&](const std::string& what) { ++wrong; if (first.empty()) first = what; };
    for (std::uint32_t a : kValues) for (std::uint32_t c : kValues) {
        struct Op { const char* name; std::uint16_t word; };
        const Op ops[] = {{"MUL.L", Nm(0x0007, 0, 1)}, {"MULS.W", Nm(0x200F, 0, 1)}, {"MULU.W", Nm(0x200E, 0, 1)}, {"DMULS.L", Nm(0x300D, 0, 1)}, {"DMULU.L", Nm(0x3005, 0, 1)}};
        for (const Op& op : ops) {
            Bench bench;
            bench.Boot();
            bench.Program(0x1000, {op.word});
            Registers r = bench.Regs();
            r.r[0] = a; r.r[1] = c; r.mach = 0x55555555; r.macl = 0x66666666;
            bench.Set(r);
            bench.Step();
            const Registers out = bench.Regs();
            const std::string name = op.name;
            std::uint32_t expectedMacl = 0, expectedMach = 0x55555555;
            if (name == "MUL.L") expectedMacl = a * c;
            else if (name == "MULS.W") expectedMacl = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(a)) * static_cast<std::int32_t>(static_cast<std::int16_t>(c)));
            else if (name == "MULU.W") expectedMacl = static_cast<std::uint32_t>(static_cast<std::uint16_t>(a)) * static_cast<std::uint16_t>(c);
            else if (name == "DMULS.L") { const std::int64_t p = static_cast<std::int64_t>(static_cast<std::int32_t>(a)) * static_cast<std::int32_t>(c); expectedMacl = static_cast<std::uint32_t>(p); expectedMach = static_cast<std::uint32_t>(static_cast<std::uint64_t>(p) >> 32); }
            else { const std::uint64_t p = static_cast<std::uint64_t>(a) * c; expectedMacl = static_cast<std::uint32_t>(p); expectedMach = static_cast<std::uint32_t>(p >> 32); }
            if (out.macl != expectedMacl || out.mach != expectedMach) fail(name + " " + Hex(a) + "*" + Hex(c));
        }
    }
    Check(wrong == 0, wrong == 0 ? "MUL.L, MULS.W, MULU.W, DMULS.L and DMULU.L match the 64-bit reference" : first.c_str());

    // MAC.L: MACH:MACL += product of two longs from memory; with S set, saturates at 48 bits
    const std::uint64_t accumulators[] = {0, 1, 0x00007FFFFFFFFFFFull, 0xFFFF800000000000ull, 0xFFFFFFFFFFFFFFFFull, 0x0000123456789ABCull, 0x7FFFFFFF00000000ull};
    wrong = 0;
    first.clear();
    for (std::uint64_t acc : accumulators) for (std::uint32_t a : kValues) for (std::uint32_t c : kValues) for (std::uint32_t s = 0; s < 2; ++s) {
        Bench bench;
        bench.Boot();
        bench.Program(0x1000, {Nm(0x000F, 0, 1)});
        bench.W32(0x2000, a);
        bench.W32(0x2100, c);
        Registers r = bench.Regs();
        r.r[0] = 0x2000; r.r[1] = 0x2100; r.mach = static_cast<std::uint32_t>(acc >> 32); r.macl = static_cast<std::uint32_t>(acc); r.sr = 0xF0 | (s << 1);
        bench.Set(r);
        bench.Step();
        const Registers out = bench.Regs();
        const std::int64_t product = static_cast<std::int64_t>(static_cast<std::int32_t>(a)) * static_cast<std::int32_t>(c);
        std::uint64_t expected;
        const std::int64_t signedAcc = static_cast<std::int64_t>(acc);
        const bool overflow64 = ((signedAcc ^ static_cast<std::int64_t>(acc + static_cast<std::uint64_t>(product))) & (product ^ static_cast<std::int64_t>(acc + static_cast<std::uint64_t>(product)))) < 0;
        std::int64_t sum = static_cast<std::int64_t>(acc + static_cast<std::uint64_t>(product));
        if (s) {
            const std::int64_t high = (1ll << 47) - 1, low = -(1ll << 47);
            if (overflow64) sum = product < 0 ? low : high; else if (sum > high) sum = high; else if (sum < low) sum = low;
        }
        expected = static_cast<std::uint64_t>(sum);
        const std::uint64_t got = static_cast<std::uint64_t>(out.mach) << 32 | out.macl;
        if (got != expected || out.r[0] != 0x2004 || out.r[1] != 0x2104) { ++wrong; if (first.empty()) first = "MAC.L acc " + Hex(static_cast<std::uint32_t>(acc >> 32)) + " " + Hex(a) + "*" + Hex(c) + " S=" + std::to_string(s); }
    }
    Check(wrong == 0, wrong == 0 ? "MAC.L accumulates in 64 bits, saturates at 48 bits with S, and steps both pointers" : first.c_str());

    // MAC.W: 64-bit accumulate; with S the 32-bit MACL saturates (MACH is left alone)
    wrong = 0;
    first.clear();
    for (std::uint32_t macl : kValues) for (std::uint32_t a : kValues) for (std::uint32_t c : kValues) for (std::uint32_t s = 0; s < 2; ++s) {
        Bench bench;
        bench.Boot();
        bench.Program(0x1000, {Nm(0x400F, 0, 1)});
        bench.W16(0x2000, static_cast<std::uint16_t>(a));
        bench.W16(0x2100, static_cast<std::uint16_t>(c));
        Registers r = bench.Regs();
        r.r[0] = 0x2000; r.r[1] = 0x2100; r.mach = 0x00000001; r.macl = macl; r.sr = 0xF0 | (s << 1);
        bench.Set(r);
        bench.Step();
        const Registers out = bench.Regs();
        const std::int64_t product = static_cast<std::int64_t>(static_cast<std::int16_t>(a)) * static_cast<std::int16_t>(c);
        std::uint32_t expectedMach, expectedMacl;
        if (s) {
            std::int64_t sum = static_cast<std::int64_t>(static_cast<std::int32_t>(macl)) + product;
            if (sum > 0x7FFFFFFF) sum = 0x7FFFFFFF; else if (sum < -0x80000000ll) sum = -0x80000000ll;
            expectedMacl = static_cast<std::uint32_t>(sum);
            expectedMach = 1;
        } else {
            const std::uint64_t sum = (1ull << 32 | macl) + static_cast<std::uint64_t>(product);
            expectedMacl = static_cast<std::uint32_t>(sum);
            expectedMach = static_cast<std::uint32_t>(sum >> 32);
        }
        if (out.macl != expectedMacl || out.mach != expectedMach || out.r[0] != 0x2002 || out.r[1] != 0x2102) { ++wrong; if (first.empty()) first = "MAC.W macl " + Hex(macl) + " " + Hex(a & 0xFFFF) + "*" + Hex(c & 0xFFFF) + " S=" + std::to_string(s); }
    }
    Check(wrong == 0, wrong == 0 ? "MAC.W accumulates in 64 bits, saturates MACL with S, and steps both pointers" : first.c_str());
}

void Sh2Div1UnsignedDivision() {
    std::printf("Sh2Div1UnsignedDivision\n");
    // the 32-bit by 16-bit unsigned division of the programming manual: divisor << 16 in R0, dividend in R1, 16 x DIV1, ROTCL, EXTU.W
    std::uint32_t seed = 12345;
    auto next = [&]() { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
    std::size_t wrong = 0;
    std::string first;
    for (int i = 0; i < 400; ++i) {
        const std::uint32_t divisor = 1 + (next() % 0xFFFF);
        const std::uint32_t quotient = next() % 0x10000;
        const std::uint32_t remainder = next() % divisor;
        const std::uint32_t dividend = quotient * divisor + remainder;
        if (static_cast<std::uint64_t>(dividend) >> 16 >= divisor) continue;   // the quotient must fit in 16 bits
        Bench bench;
        bench.Boot();
        std::uint32_t at = 0x1000;
        bench.W16(at, 0x4028); at += 2;                       // SHLL16 R0
        bench.W16(at, 0x0019); at += 2;                       // DIV0U
        for (int k = 0; k < 16; ++k) { bench.W16(at, Nm(0x3004, 1, 0)); at += 2; }   // DIV1 R0,R1
        bench.W16(at, Nn(0x4024, 1)); at += 2;                // ROTCL R1
        bench.W16(at, Nm(0x600D, 1, 1)); at += 2;             // EXTU.W R1,R1
        Registers r = bench.Regs();
        r.r[0] = divisor; r.r[1] = dividend;
        bench.Set(r);
        bench.Steps(20);
        if (bench.Regs().r[1] != dividend / divisor) { ++wrong; if (first.empty()) first = Hex(dividend) + "/" + Hex(divisor) + " gave " + Hex(bench.Regs().r[1]); }
    }
    Check(wrong == 0, wrong == 0 ? "16 DIV1 steps divide a 32-bit dividend by a 16-bit divisor exactly (400 random cases)" : first.c_str());

    // DIV0S + DIV1 for signed: just the Q, M, T setting of DIV0S
    Bench bench;
    bench.Boot();
    bench.Program(0x1000, {Nm(0x2007, 0, 1)});
    Registers r = bench.Regs();
    r.r[0] = 0x80000000; r.r[1] = 0x00000001;
    bench.Set(r);
    bench.Step();
    Check((bench.Regs().sr & MameDerivedSh2Adapter::kQ) != 0 && (bench.Regs().sr & MameDerivedSh2Adapter::kM) == 0 && (bench.Regs().sr & kT) != 0,
          "DIV0S copies the signs into Q and M and sets T when they differ");
}

void Sh2DelayedBranchesAndSubroutines() {
    std::printf("Sh2DelayedBranchesAndSubroutines\n");
    {   // BRA: the instruction after it runs, the two after that are skipped
        Bench b;
        b.Boot();
        b.Program(0x1000, {0xA002, 0x7001, 0x700A, 0x7064, 0xE105});
        const std::uint64_t cycles = b.Steps(3);
        Check(b.Regs().r[0] == 1 && b.Regs().r[1] == 5, "BRA runs the delay slot, skips the following code and lands on the target");
        Check(cycles == 2 + 1 + 1, "BRA takes two cycles, the slot and the target one each");
    }
    {
        Bench b;
        b.Boot();
        b.W32(0x4000 + 4 * 66, 0x3000);
        b.Program(0x3000, {0x0009, 0x0009});
        b.Program(0x1000, {0x0009, 0xA002, 0x0009, 0x0009, 0x0009, 0x0009});
        Registers r = b.Regs();
        r.sr = 0; r.vbr = 0x4000;
        b.Set(r);
        b.Step();   // NOP
        b.Step();   // BRA
        b.cpu.SetInterruptLine(5, true);
        b.Step();   // the slot
        Check(b.Regs().exceptionCount == 0, "an interrupt asserted after the branch waits for the end of the delay slot");
        b.Step();   // taken before the first instruction of the target
        Check(b.Regs().exceptionCount == 1 && b.R32(b.Regs().r[15]) == 0x100A, "then it is taken, and the pushed PC is the branch target");
    }
    {   // JSR / RTS
        Bench b;
        b.Boot();
        b.Program(0x1000, {Nn(0x400B, 2), 0x0009, Nn(0x7000, 1) | 1});   // JSR @R2; NOP; ADD #1,R1
        b.Program(0x2000, {0x7001, 0x000B, 0x0009});                       // ADD #1,R0; RTS; NOP
        Registers r = b.Regs();
        r.r[2] = 0x2000;
        b.Set(r);
        const std::uint64_t cycles = b.Steps(5);   // JSR, NOP, ADD, RTS, NOP
        Check(b.Regs().pr == 0x1004 && b.Regs().pc == 0x1004 && b.Regs().r[0] == 1, "JSR saves the return address after the slot and RTS returns there");
        Check(cycles == 2 + 1 + 1 + 2 + 1, "JSR and RTS take two cycles each");
        b.Step();
        Check(b.Regs().r[1] == 1, "the code after the call runs");
    }
    {   // BT, BF, BT/S, BF/S taken and not taken
        struct Case { std::uint16_t word; bool t; bool taken; bool slot; std::uint64_t cycles; };
        const Case cases[] = {{0x8902, true, true, false, 3}, {0x8902, false, false, false, 1}, {0x8B02, false, true, false, 3}, {0x8B02, true, false, false, 1},
                              {0x8D02, true, true, true, 2}, {0x8D02, false, false, false, 1}, {0x8F02, false, true, true, 2}, {0x8F02, true, false, false, 1}};
        std::size_t wrong = 0;
        for (const Case& c : cases) {
            Bench b;
            b.Boot();
            b.Program(0x1000, {c.word, 0x7001, 0x7010, 0x7010});
            Registers r = b.Regs();
            r.sr = 0xF0 | (c.t ? 1 : 0);
            b.Set(r);
            const std::uint64_t cycles = b.Step();
            const Registers out = b.Regs();
            const bool delayed = (c.word & 0x0400) != 0;
            const std::uint32_t target = 0x1000 + 4 + 2 * 2;
            const bool pcOk = c.taken ? (delayed ? out.inDelaySlot : out.pc == target) : out.pc == 0x1002;
            if (cycles != c.cycles || !pcOk) ++wrong;
        }
        Check(wrong == 0, "BT, BF, BT/S and BF/S: cycles 3 and 1 (taken, not taken), 2 and 1 for the delayed forms, with the slot only when taken");
    }
    {   // BSR, BRAF, BSRF
        Bench b;
        b.Boot();
        b.Program(0x1000, {0xB002, 0x7001});   // BSR +2 with ADD #1,R0 in the slot
        b.Step();
        Check(b.Regs().pr == 0x1004, "BSR saves the address after its slot");
        Bench c;
        c.Boot();
        c.Program(0x1000, {Nn(0x0023, 3), 0x7001, 0x0009, 0x0009, 0x0009, 0x0009, 0x7001});   // BRAF R3 (+0x0A: target 0x1000+4+0x0A)
        Registers r = c.Regs();
        r.r[3] = 0x08;
        c.Set(r);
        c.Steps(3);
        Check(c.Regs().r[0] == 2, "BRAF jumps relative to the slot's address plus 2 after running the slot");
    }
}

void Sh2MemoryAccessForms() {
    std::printf("Sh2MemoryAccessForms\n");
    {   // PC-relative loads and MOVA
        Bench b;
        b.Boot();
        b.Program(0x1000, {Nn(0xD000, 1) | 15, Nn(0x9000, 2) | 0x1F, 0xC700 | 14});
        b.W32(0x1040, 0xDEADBEEF);
        b.W16(0x1044, 0x8001);
        b.Steps(3);
        const Registers r = b.Regs();
        Check(r.r[1] == 0xDEADBEEF, "MOV.L @(disp,PC),Rn reads from the longword-aligned PC + 4 plus disp * 4");
        Check(r.r[2] == 0xFFFF8001, "MOV.W @(disp,PC),Rn reads from PC + 4 plus disp * 2 and sign extends");
        Check(r.r[0] == 0x1040, "MOVA gives the longword-aligned PC + 4 plus disp * 4");
    }
    {   // register indirect forms with increments and decrements
        Bench b;
        b.Boot();
        b.W32(0x2000, 0x11223344);
        b.Program(0x1000, {Nm(0x6006, 2, 1), Nm(0x6006, 3, 3), Nm(0x6004, 4, 1), Nm(0x2006, 5, 6)});
        Registers r = b.Regs();
        r.r[1] = 0x2000; r.r[3] = 0x2000; r.r[5] = 0x2010; r.r[6] = 0xCAFEF00D;
        b.Set(r);
        b.Step();
        Check(b.Regs().r[2] == 0x11223344 && b.Regs().r[1] == 0x2004, "MOV.L @Rm+,Rn loads and steps Rm by 4");
        b.Step();
        Check(b.Regs().r[3] == 0x11223344, "MOV.L @Rn+,Rn leaves the loaded value in Rn (no increment when both are the same register)");
        b.Step();
        Check(b.Regs().r[4] == 0u && b.Regs().r[1] == 0x2005, "MOV.B @Rm+,Rn loads a byte (sign extended) and steps Rm by 1");
        b.Step();
        Check(b.Regs().r[5] == 0x200C && b.R32(0x200C) == 0xCAFEF00D, "MOV.L Rm,@-Rn pre-decrements and stores");
    }
    {   // R0-indexed and displacement forms
        Bench b;
        b.Boot();
        b.W32(0x2108, 0x01020304);
        b.Program(0x1000, {Nm(0x000E, 2, 1), Nm(0x0006, 1, 3), 0x1000 | (4 << 8) | (3 << 4) | 2, 0x5000 | (5 << 8) | (4 << 4) | 2, 0x8000 | (6 << 4) | 3, 0x8400 | (6 << 4) | 3, 0x8100 | (6 << 4) | 3, 0x8500 | (6 << 4) | 3});
        Registers r = b.Regs();
        r.r[0] = 8; r.r[1] = 0x2100; r.r[3] = 0xAABBCCDD; r.r[4] = 0x2200; r.r[6] = 0x2300;
        b.Set(r);
        b.Step();
        Check(b.Regs().r[2] == 0x01020304, "MOV.L @(R0,Rm),Rn");
        b.Step();
        Check(b.R32(0x2108) == 0xAABBCCDD, "MOV.L Rm,@(R0,Rn)");
        b.Step();
        Check(b.R32(0x2208) == 0xAABBCCDD, "MOV.L Rm,@(disp,Rn) scales the displacement by 4");
        b.Step();
        Check(b.Regs().r[5] == 0xAABBCCDD, "MOV.L @(disp,Rm),Rn scales the displacement by 4");
        b.Regs();
        Registers r2 = b.Regs();
        r2.r[0] = 0x1FF;
        b.Set(r2);
        b.Step();   // MOV.B R0,@(3,R6)
        Check(b.mem[0x2303] == 0xFF, "MOV.B R0,@(disp,Rn)");
        b.Step();   // MOV.B @(3,R6),R0
        Check(b.Regs().r[0] == 0xFFFFFFFF, "MOV.B @(disp,Rm),R0 sign extends");
        b.Step();   // MOV.W R0,@(3,R6)
        Check(b.mem[0x2306] == 0xFF && b.mem[0x2307] == 0xFF, "MOV.W R0,@(disp,Rn) scales by 2");
        b.Step();
        Check(b.Regs().r[0] == 0xFFFFFFFF, "MOV.W @(disp,Rm),R0 sign extends");
    }
    {   // GBR forms and the read-modify-write byte instructions
        Bench b;
        b.Boot();
        b.mem[0x3010] = 0xF0;
        b.Program(0x1000, {0xCD3C, 0xCF01, 0xCEFF, 0xCC01, 0xCC02, 0xC200 | 2, 0xC600 | 2, 0xC400 | 0x10});
        Registers r = b.Regs();
        r.gbr = 0x3000; r.r[0] = 0x10;
        b.Set(r);
        b.Step();
        Check(b.mem[0x3010] == 0x30, "AND.B #imm,@(R0,GBR)");
        b.Step();
        Check(b.mem[0x3010] == 0x31, "OR.B #imm,@(R0,GBR)");
        b.Step();
        Check(b.mem[0x3010] == 0xCE, "XOR.B #imm,@(R0,GBR)");
        b.Step();
        Check((b.Regs().sr & kT) != 0, "TST.B #imm,@(R0,GBR) sets T when the masked byte is zero");
        b.Step();
        Check((b.Regs().sr & kT) == 0, "TST.B clears T when a masked bit is set");
        Registers r2 = b.Regs();
        r2.r[0] = 0x55667788;
        b.Set(r2);
        b.Step();   // MOV.L R0,@(2,GBR)
        Check(b.R32(0x3008) == 0x55667788, "MOV.L R0,@(disp,GBR) scales by 4");
        Registers r3 = b.Regs();
        r3.r[0] = 0;
        b.Set(r3);
        b.Step();
        Check(b.Regs().r[0] == 0x55667788, "MOV.L @(disp,GBR),R0");
    }
    {   // TAS.B
        Bench b;
        b.Boot();
        b.Program(0x1000, {Nn(0x401B, 1), Nn(0x401B, 1)});
        Registers r = b.Regs();
        r.r[1] = 0x2000;
        b.Set(r);
        b.mem[0x2000] = 0;
        b.Step();
        Check((b.Regs().sr & kT) != 0 && b.mem[0x2000] == 0x80, "TAS.B sets T for a zero byte and sets bit 7");
        b.mem[0x2000] = 0x7F;
        b.Step();
        Check((b.Regs().sr & kT) == 0 && b.mem[0x2000] == 0xFF, "TAS.B clears T for a non-zero byte");
    }
    {   // control register moves
        Bench b;
        b.Boot();
        b.Program(0x1000, {Nn(0x400E, 1), Nn(0x0002, 2), Nn(0x401E, 1), Nn(0x402E, 1), Nn(0x402A, 1), Nn(0x002A, 3), Nn(0x4003, 4) , Nn(0x4007, 4)});
        Registers r = b.Regs();
        r.r[1] = 0xFFFFFFFF; r.r[4] = 0x2010;
        b.Set(r);
        b.Step();
        Check(b.Regs().sr == 0x3F3, "LDC Rm,SR keeps only the defined flag bits");
        b.Step();
        Check(b.Regs().r[2] == 0x3F3, "STC SR,Rn");
        b.Steps(2);
        Check(b.Regs().gbr == 0xFFFFFFFF && b.Regs().vbr == 0xFFFFFFFF, "LDC Rm,GBR and VBR");
        b.Steps(2);
        Check(b.Regs().r[3] == 0xFFFFFFFF, "LDS Rm,PR and STS PR,Rn");
        b.Step();
        Check(b.Regs().r[4] == 0x200C && b.R32(0x200C) == 0x3F3, "STC.L SR,@-Rn");
        b.W32(0x200C, 0x12);
        b.Step();
        Check(b.Regs().sr == 0x12 && b.Regs().r[4] == 0x2010, "LDC.L @Rm+,SR");
    }
}

void Sh2ExceptionsInterruptsAndReturn() {
    std::printf("Sh2ExceptionsInterruptsAndReturn\n");
    auto make = [](Bench& b) {
        b.Boot();
        b.W32(0x4000 + 4 * 66, 0x3000);   // level 4 or 5: autovector 64 + level / 2
        b.W32(0x4000 + 4 * 67, 0x3100);   // level 6 or 7
        b.W32(0x4000 + 4 * 11, 0x3200);   // NMI
        b.W32(0x4000 + 4 * 4, 0x3300);    // illegal instruction
        b.W32(0x4000 + 4 * 3, 0x3400);    // TRAPA #3
        b.W32(0x4000 + 4 * 0x50, 0x3500); // an on-chip vector
        b.Program(0x1000, {0x0009, 0x0009, 0x0009, 0x0009});
        b.Program(0x3000, {0x7001, 0x002B, 0x0009});   // ADD #1,R0; RTE; NOP
        b.Program(0x3100, {0x7002, 0x002B, 0x0009});
        b.Program(0x3200, {0x7004, 0x002B, 0x0009});
        Registers r = b.Regs();
        r.sr = 0; r.vbr = 0x4000;
        b.Set(r);
    };
    {
        Bench b;
        make(b);
        b.cpu.SetInterruptLine(5, true);
        b.Step();
        Registers r = b.Regs();
        Check(r.exceptionCount == 1 && r.pc == 0x3002, "an asserted interrupt is taken at the next instruction boundary: the autovector 64 + level / 2");
        Check(r.sr == 0x50 && r.r[15] == 0x7FF8 && b.R32(0x7FF8) == 0x1000 && b.R32(0x7FFC) == 0, "it pushes the PC then SR, and raises the mask to the level");
        b.cpu.SetInterruptLine(5, false);
        b.Steps(2);   // RTE and its slot
        r = b.Regs();
        Check(r.pc == 0x1000 && r.sr == 0 && r.r[15] == 0x8000, "RTE restores the PC and SR after its slot");
    }
    {
        Bench b;
        make(b);
        Registers r = b.Regs();
        r.sr = 0x50;
        b.Set(r);
        b.cpu.SetInterruptLine(5, true);
        b.Steps(2);
        Check(b.Regs().exceptionCount == 0, "an interrupt of the level in SR's mask (or below) is not taken, and stays pending");
        b.cpu.SetInterruptLine(6, true);
        b.Step();
        Check(b.Regs().exceptionCount == 1 && b.Regs().pc == 0x3102 && (b.Regs().sr & 0xF0) == 0x60, "a level above the mask is taken, the higher of two pending ones first (vector 64 + 3)");
        // lowering the mask with LDC lets the lower one in (after the higher one has returned)
    }
    {
        Bench b;
        make(b);
        Registers r = b.Regs();
        r.sr = 0xF0;
        b.Set(r);
        b.cpu.SetNmi(true);
        b.Step();
        Check(b.Regs().exceptionCount == 1 && b.Regs().pc == 0x3202 && (b.Regs().sr & 0xF0) == 0xF0, "NMI is taken whatever the mask, with vector 11");
        b.Steps(2);
        b.cpu.SetNmi(true);
        b.Step();
        Check(b.Regs().exceptionCount == 1, "NMI is edge triggered: a line that stays asserted does not repeat it");
    }
    {
        Bench b;
        make(b);
        b.Program(0x3500, {0x0009, 0x0009});
        b.cpu.SetInternalInterrupt(8, 0x50);
        b.Step();
        Check(b.Regs().exceptionCount == 1 && b.Regs().pc == 0x3502, "an on-chip request is taken with its own vector");
        b.Steps(3);
        Check(b.Regs().exceptionCount == 1, "the on-chip request is taken once");
    }
    {
        Bench b;
        make(b);
        b.Program(0x1000, {0xC303, 0x0009});   // TRAPA #3
        b.Step();
        Check(b.Regs().pc == 0x3400 && b.R32(b.Regs().r[15]) == 0x1002, "TRAPA pushes SR and the address after it, and fetches vector n");
        Bench c;
        make(c);
        c.Program(0x1000, {0xF000});
        c.Step();
        Check(c.Regs().pc == 0x3300 && c.R32(c.Regs().r[15]) == 0x1000, "an illegal instruction takes vector 4 and pushes its own address");
    }
    {   // SLEEP waits for an exception and continues after it
        Bench b;
        make(b);
        b.Program(0x1000, {0x001B, 0x7001});   // SLEEP; ADD #1,R0
        b.Steps(3);
        Check(b.Regs().pc == 0x1000, "SLEEP stays on itself while nothing wakes the CPU");
        b.cpu.SetInterruptLine(5, true);
        b.Step();
        b.cpu.SetInterruptLine(5, false);
        b.Steps(2);   // RTE, slot (the handler's first instruction ran with the exception)
        Check(b.Regs().pc == 0x1000, "the interrupt returns to the SLEEP");
        b.Step();
        Check(b.Regs().pc == 0x1002, "and SLEEP then continues with the next instruction");
    }
}

void Sh2CycleCountsPerInstruction() {
    std::printf("Sh2CycleCountsPerInstruction\n");
    // The counts of MAME's interpreter (one cycle plus the extra the instruction takes), except where noted.
    struct Case { const char* name; std::uint16_t word; bool t; std::uint64_t cycles; };
    const Case cases[] = {
        {"NOP", 0x0009, false, 1}, {"ADD", Nm(0x300C, 1, 2), false, 1}, {"MOV.L @Rm,Rn", Nm(0x6002, 1, 2), false, 1}, {"MOV.L Rm,@Rn", Nm(0x2002, 1, 2), false, 1},
        {"MUL.L", Nm(0x0007, 1, 2), false, 2}, {"MULS.W", Nm(0x200F, 1, 2), false, 1}, {"MULU.W", Nm(0x200E, 1, 2), false, 1}, {"DMULS.L", Nm(0x300D, 1, 2), false, 2},
        {"DMULU.L", Nm(0x3005, 1, 2), false, 2}, {"MAC.L", Nm(0x000F, 1, 2), false, 3}, {"MAC.W", Nm(0x400F, 1, 2), false, 3},
        {"BRA", 0xA001, false, 2}, {"BSR", 0xB001, false, 2}, {"BRAF", Nn(0x0023, 1), false, 2}, {"BSRF", Nn(0x0003, 1), false, 2}, {"JSR", Nn(0x400B, 1), false, 2},
        {"JMP (the manual's 2; MAME 1)", Nn(0x402B, 1), false, 2}, {"RTS", 0x000B, false, 2}, {"RTE", 0x002B, false, 4}, {"TRAPA", 0xC301, false, 8},
        {"BT taken", 0x8901, true, 3}, {"BT not taken", 0x8901, false, 1}, {"BF taken", 0x8B01, false, 3}, {"BF not taken", 0x8B01, true, 1},
        {"BT/S taken", 0x8D01, true, 2}, {"BF/S taken", 0x8F01, false, 2}, {"TAS.B", Nn(0x401B, 1), false, 4},
        {"AND.B #imm,@(R0,GBR)", 0xCD01, false, 3}, {"TST.B #imm,@(R0,GBR)", 0xCC01, false, 3}, {"XOR.B #imm,@(R0,GBR)", 0xCE01, false, 3},
        {"OR.B #imm,@(R0,GBR) (the manual's 3; MAME 1)", 0xCF01, false, 3}, {"OR #imm,R0 (the manual's 1; MAME 3)", 0xCB01, false, 1},
        {"LDC.L @Rm+,SR", Nn(0x4007, 1), false, 3}, {"LDC.L @Rm+,GBR", Nn(0x4017, 1), false, 3}, {"LDC.L @Rm+,VBR", Nn(0x4027, 1), false, 3},
        {"STC.L SR,@-Rn", Nn(0x4003, 1), false, 2}, {"STC.L GBR,@-Rn", Nn(0x4013, 1), false, 2}, {"STC.L VBR,@-Rn", Nn(0x4023, 1), false, 2},
        {"LDS.L @Rm+,MACH", Nn(0x4006, 1), false, 1}, {"STS.L MACH,@-Rn", Nn(0x4002, 1), false, 1}, {"SLEEP", 0x001B, false, 3}, {"DT", Nn(0x4010, 1), false, 1},
        {"DIV1", Nm(0x3004, 1, 2), false, 1}, {"CLRMAC", 0x0028, false, 1}, {"illegal instruction", 0xF000, false, 6},
    };
    std::size_t wrong = 0;
    for (const Case& c : cases) {
        Bench b;
        b.Boot();
        b.Program(0x1000, {c.word, 0x0009, 0x0009});
        Registers r = b.Regs();
        r.r[1] = 0x3000; r.r[2] = 0x3100; r.r[15] = 0x8000; r.gbr = 0x3000; r.sr = 0xF0 | (c.t ? 1 : 0);
        b.Set(r);
        const std::uint64_t cycles = b.Step();
        if (cycles != c.cycles) { ++wrong; std::printf("    %s: %llu cycles, expected %llu\n", c.name, static_cast<unsigned long long>(cycles), static_cast<unsigned long long>(c.cycles)); }
    }
    Check(wrong == 0, "every instruction class takes the cycles of the manual's instruction table (OR #imm,R0 1, OR.B 3, JMP 2)");
    // every opcode of the ordinary one-cycle classes (SH-1/SH-2 software manual, section 6.1 and appendix A.1: ALU, logic, shifts,
    // register and memory moves, compares, extends) takes one cycle
    struct OneCycleClass { const char* name; std::uint16_t mask, value; };
    const OneCycleClass classes[] = {
        {"MOV Rm,Rn and the loads and stores through Rn", 0xF00C, 0x6000}, {"MOV #imm,Rn", 0xF000, 0xE000}, {"MOV.L Rm,@(disp,Rn)", 0xF000, 0x1000},
        {"MOV.L @(disp,Rm),Rn", 0xF000, 0x5000}, {"ADD #imm,Rn", 0xF000, 0x7000}, {"ADD/SUB/ADDC/SUBC Rm,Rn", 0xF00C, 0x3008},
        {"AND/OR/XOR/TST Rm,Rn", 0xF00F, 0x2009}, {"AND/OR/XOR/TST Rm,Rn (OR)", 0xF00F, 0x200B}, {"CMP forms", 0xF00F, 0x3000},
        {"MOV.B/W/L @(R0,Rm)", 0xF00C, 0x0004}, {"EXTS/EXTU/NEG/NOT/SWAP", 0xF00C, 0x600C}, {"MOV.B R0,@(disp,Rn)", 0xFF00, 0x8000},
        {"MOV.x R0,@(disp,GBR)/@(disp,GBR),R0", 0xFC00, 0xC000},
    };
    std::size_t oneWrong = 0, tested = 0;
    for (const OneCycleClass& cl : classes) {
        for (std::uint32_t op = 0; op < 0x10000; ++op) {
            if ((op & cl.mask) != cl.value) continue;
            if (cl.value == 0xC000 && ((op >> 8) & 3) == 3) continue;            // TRAPA is not a move
            if (cl.value == 0xC000 && (op & 0x0F00) >= 0x0800) continue;         // the logic forms on R0 and (R0,GBR) are tested above
            if ((op & 0xF00F) == 0x3009) continue;                                // not an instruction
            if ((op & 0xF00F) == 0x0007) continue;                                // MUL.L: two cycles (tested above)
            Bench b;
            b.Boot();
            b.Program(0x1000, {static_cast<std::uint16_t>(op), 0x0009, 0x0009});
            Registers r = b.Regs();
            for (int i = 0; i < 15; ++i) r.r[i] = 0x3000 + 0x40 * i;
            r.r[15] = 0x8000; r.gbr = 0x3000; r.sr = 0xF0;
            b.Set(r);
            const std::uint64_t cycles = b.Step();
            ++tested;
            if (cycles != 1) { if (oneWrong < 5) std::printf("    %s: opcode %04X took %llu cycles\n", cl.name, op, static_cast<unsigned long long>(cycles)); ++oneWrong; }
        }
    }
    std::printf("  (%zu opcodes of the one-cycle classes tested)\n", tested);
    Check(oneWrong == 0, "every opcode of the one-cycle classes takes one cycle");
}
