// license:BSD-3-Clause
// copyright-holders:Karl Stenerud
// copyright-holders:Olivier Galibert
// The original files also state: "MUSASHI ... Copyright Karl Stenerud".
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/cpu/m68000/m68kcpu.cpp  [copyright-holders:Karl Stenerud]
//   src/devices/cpu/m68000/m68kcpu.h  [copyright-holders:Karl Stenerud]
//   src/devices/cpu/m68000/m68kmusashi.h  [copyright-holders:Karl Stenerud]
//   src/devices/cpu/m68000/m68000.h  [copyright-holders:Olivier Galibert]
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#include "mame_derived_m68000_adapter.h"

#include <utility>

// ============================================================================
// Construction / configuration
// ============================================================================

MameDerivedM68000Adapter::MameDerivedM68000Adapter() = default;

void MameDerivedM68000Adapter::SetMemoryCallbacks(MemoryReadFn read, MemoryWriteFn write) {
    memoryRead_ = std::move(read);
    memoryWrite_ = std::move(write);
}

void MameDerivedM68000Adapter::SetAutovectorAcknowledgeCallback(AutovectorAckFn callback) {
    autovectorAck_ = std::move(callback);
}

void MameDerivedM68000Adapter::Reset() {
    for (std::uint32_t& d : regs_.d) d = 0;
    for (std::uint32_t& a : regs_.a) a = 0;
    regs_.usp = 0;
    regs_.ssp = 0;
    regs_.sr = M68000StatusRegister{};  // trace=false, supervisor=true, mask=7, CCR cleared
    halted_ = false;
    interruptLevel_ = 0;

    // RESET exception: initial SSP from the long at address 0, initial PC from the long at
    // address 4.
    regs_.a[7] = Read32(0);
    regs_.ssp = regs_.a[7];
    regs_.pc = Read32(4);
}

void MameDerivedM68000Adapter::SetInterruptLevel(std::uint8_t level) {
    interruptLevel_ = static_cast<std::uint8_t>(level & 0x07);
}

// ============================================================================
// Bus access
// ============================================================================

std::uint8_t MameDerivedM68000Adapter::Read8(std::uint32_t address) const {
    return memoryRead_ ? memoryRead_(address) : 0xFF;
}

std::uint16_t MameDerivedM68000Adapter::Read16(std::uint32_t address) const {
    // Big-endian (68000 bus convention): high byte at the lower address.
    const std::uint16_t hi = Read8(address);
    const std::uint16_t lo = Read8(address + 1);
    return static_cast<std::uint16_t>((hi << 8) | lo);
}

std::uint32_t MameDerivedM68000Adapter::Read32(std::uint32_t address) const {
    const std::uint32_t hi = Read16(address);
    const std::uint32_t lo = Read16(address + 2);
    return (hi << 16) | lo;
}

void MameDerivedM68000Adapter::Write8(std::uint32_t address, std::uint8_t value) const {
    if (memoryWriteObserver_) memoryWriteObserver_(address, value);
    if (memoryWrite_) memoryWrite_(address, value);
}

void MameDerivedM68000Adapter::Write16(std::uint32_t address, std::uint16_t value) const {
    Write8(address, static_cast<std::uint8_t>(value >> 8));
    Write8(address + 1, static_cast<std::uint8_t>(value & 0xFF));
}

void MameDerivedM68000Adapter::Write32(std::uint32_t address, std::uint32_t value) const {
    Write16(address, static_cast<std::uint16_t>(value >> 16));
    Write16(address + 2, static_cast<std::uint16_t>(value & 0xFFFF));
}

std::uint16_t MameDerivedM68000Adapter::FetchOpcodeWord() {
    const std::uint32_t at = regs_.pc;
    const std::uint16_t value = Read16(at);
    regs_.pc += 2;
    lastFetchedOpcodeForTest_ = value;
    if (opcodeFetchObserver_) opcodeFetchObserver_(at, value);
    return value;
}

std::uint16_t MameDerivedM68000Adapter::FetchExtensionWord() {
    const std::uint16_t value = Read16(regs_.pc);
    regs_.pc += 2;
    return value;
}

std::uint32_t MameDerivedM68000Adapter::FetchExtensionLong() {
    const std::uint32_t value = Read32(regs_.pc);
    regs_.pc += 4;
    return value;
}

void MameDerivedM68000Adapter::Push16(std::uint16_t value) {
    regs_.a[7] -= 2;
    Write16(regs_.a[7], value);
}

void MameDerivedM68000Adapter::Push32(std::uint32_t value) {
    regs_.a[7] -= 4;
    Write32(regs_.a[7], value);
}

std::uint16_t MameDerivedM68000Adapter::Pop16() {
    const std::uint16_t value = Read16(regs_.a[7]);
    regs_.a[7] += 2;
    return value;
}

std::uint32_t MameDerivedM68000Adapter::Pop32() {
    const std::uint32_t value = Read32(regs_.a[7]);
    regs_.a[7] += 4;
    return value;
}

// ============================================================================
// Register file access
// ============================================================================

std::uint32_t MameDerivedM68000Adapter::GetDataReg(int n, int size) const {
    const std::uint32_t full = regs_.d[n & 7];
    switch (size) {
        case 1: return full & 0xFF;
        case 2: return full & 0xFFFF;
        default: return full;
    }
}

// Byte/word writes to a data register preserve the untouched upper bits (as on real
// hardware); a .L write replaces the register. Address registers have no partial-write
// form (MOVE.W to An sign-extends, handled at the MOVEA call site).
void MameDerivedM68000Adapter::SetDataReg(int n, int size, std::uint32_t value) {
    std::uint32_t& reg = regs_.d[n & 7];
    switch (size) {
        case 1: reg = (reg & 0xFFFFFF00u) | (value & 0xFF); break;
        case 2: reg = (reg & 0xFFFF0000u) | (value & 0xFFFF); break;
        default: reg = value; break;
    }
}

void MameDerivedM68000Adapter::SetSr(std::uint16_t value) {
    const bool wasSupervisor = regs_.sr.supervisor;
    regs_.sr.FromWord(value);
    if (wasSupervisor != regs_.sr.supervisor) {
        // Swap the active A7 into its shadow and bring in the other shadow as the new A7.
        if (wasSupervisor) {
            regs_.ssp = regs_.a[7];
            regs_.a[7] = regs_.usp;
        } else {
            regs_.usp = regs_.a[7];
            regs_.a[7] = regs_.ssp;
        }
    }
}

// ============================================================================
// Size helpers
// ============================================================================

std::uint32_t MameDerivedM68000Adapter::SignExtend(std::uint32_t value, int size) const {
    switch (size) {
        case 1: return static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(value)));
        case 2: return static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(value)));
        default: return value;
    }
}

std::uint32_t MameDerivedM68000Adapter::MaskToSize(std::uint32_t value, int size) const {
    switch (size) {
        case 1: return value & 0xFF;
        case 2: return value & 0xFFFF;
        default: return value;
    }
}

// ============================================================================
// Snapshot
// ============================================================================

// ============================================================================
// Group4 (0x4xxx): miscellaneous - NEGX/CLR/NEG/NOT, MOVE to/from CCR/SR, NBCD/SWAP/PEA/EXT/MOVEM,
// TST/TAS/ILLEGAL, CHK/LEA, TRAP/LINK/UNLK/MOVE-USP/RESET/NOP/STOP/RTE/RTS/TRAPV/RTR/JSR/JMP.
// ============================================================================

int MameDerivedM68000Adapter::ExecuteMovem(std::uint16_t op) {
    const bool memToReg = (op & 0x0400) != 0;  // bit10
    const bool isLong = (op & 0x0040) != 0;    // bit6
    const int size = isLong ? 4 : 2;
    const int mode = (op >> 3) & 7;
    const int reg = op & 7;
    const std::uint16_t regList = FetchExtensionWord();

    int count = 0;
    if (mode == 4) {
        // -(An): register list order is reversed (A7..A0,D7..D0) and each
        // register is stored via its own predecrement, matching real
        // hardware's documented quirk for this one addressing mode.
        std::uint32_t addr = regs_.a[reg];
        for (int i = 0; i < 16; ++i) {
            if (!(regList & (1u << i))) continue;
            // bit0=A7, bit1=A6, ..., bit7=A0, bit8=D7, bit9=D6, ..., bit15=D0.
            addr -= static_cast<std::uint32_t>(size);
            const std::uint32_t value = (i < 8) ? regs_.a[7 - i] : regs_.d[15 - i];
            if (isLong) Write32(addr, value); else Write16(addr, static_cast<std::uint16_t>(value));
            ++count;
        }
        regs_.a[reg] = addr;
    } else if (mode == 3) {
        // (An)+: natural order D0..D7,A0..A7.
        std::uint32_t addr = regs_.a[reg];
        for (int i = 0; i < 16; ++i) {
            if (!(regList & (1u << i))) continue;
            if (memToReg) {
                const std::uint32_t value = isLong ? Read32(addr) : SignExtend(Read16(addr), 2);
                if (i < 8) regs_.d[i] = value; else regs_.a[i - 8] = value;
            } else {
                const std::uint32_t value = (i < 8) ? regs_.d[i] : regs_.a[i - 8];
                if (isLong) Write32(addr, value); else Write16(addr, static_cast<std::uint16_t>(value));
            }
            addr += static_cast<std::uint32_t>(size);
            ++count;
        }
        regs_.a[reg] = addr;
    } else {
        // Any other control addressing mode (d16,An)/(d8,An,Xn)/abs.W/
        // abs.L/(d16,PC)/(d8,PC,Xn) - resolved once, then walked linearly
        // without further side effects (real hardware behavior: only
        // (An)+/-(An) auto-update the base register).
        Operand ea = ResolveEa(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
        std::uint32_t addr = EffectiveAddressForOperand(ea);
        for (int i = 0; i < 16; ++i) {
            if (!(regList & (1u << i))) continue;
            if (memToReg) {
                const std::uint32_t value = isLong ? Read32(addr) : SignExtend(Read16(addr), 2);
                if (i < 8) regs_.d[i] = value; else regs_.a[i - 8] = value;
            } else {
                const std::uint32_t value = (i < 8) ? regs_.d[i] : regs_.a[i - 8];
                if (isLong) Write32(addr, value); else Write16(addr, static_cast<std::uint16_t>(value));
            }
            addr += static_cast<std::uint32_t>(size);
            ++count;
        }
    }
    return 8 + count * (isLong ? 8 : 4);
}

int MameDerivedM68000Adapter::Group4(std::uint16_t op) {
    if (op == 0x4AFC) {  // ILLEGAL (the one reserved-and-guaranteed-illegal opcode)
        RaiseIllegalInstruction();
        return 4;
    }

    const int sub = (op >> 8) & 0xF;   // bits 11-8
    const int b7_6 = (op >> 6) & 3;
    const int mode = (op >> 3) & 7;
    const int reg = op & 7;

    // --- NEGX / MOVE from SR ---
    if (sub == 0x0) {
        if (b7_6 == 3) {
            Operand dst = ResolveEa(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), 2);
            WriteOperand(dst, 2, GetSr());
            return 8 + EaExtraCycles(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), 2);
        }
        const int size = (b7_6 == 0) ? 1 : (b7_6 == 1) ? 2 : 4;
        Operand dst = ResolveEa(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
        const std::uint32_t a = ReadOperand(dst, size);
        const std::uint32_t result = OpSub(0, a, size, true, true);  // NEGX: 0 - a - X, Z-unless-nonzero rule
        WriteOperand(dst, size, result);
        return 4 + EaExtraCycles(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
    }

    // --- CLR ---
    if (sub == 0x2) {
        const int size = (b7_6 == 0) ? 1 : (b7_6 == 1) ? 2 : (b7_6 == 2) ? 4 : -1;
        if (size < 0) { RaiseIllegalInstruction(); return 4; }
        Operand dst = ResolveEa(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
        WriteOperand(dst, size, 0);
        regs_.sr.n = false;
        regs_.sr.z = true;
        regs_.sr.v = false;
        regs_.sr.c = false;
        return 4 + EaExtraCycles(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
    }

    // --- NEG / MOVE to CCR ---
    if (sub == 0x4) {
        if (b7_6 == 3) {
            Operand src = ResolveEa(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), 2);
            SetCcr(static_cast<std::uint16_t>(ReadOperand(src, 2) & 0xFF));
            return 12 + EaExtraCycles(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), 2);
        }
        const int size = (b7_6 == 0) ? 1 : (b7_6 == 1) ? 2 : 4;
        Operand dst = ResolveEa(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
        const std::uint32_t a = ReadOperand(dst, size);
        const std::uint32_t result = OpSub(0, a, size, false, false);
        WriteOperand(dst, size, result);
        return 4 + EaExtraCycles(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
    }

    // --- NOT / MOVE to SR (privileged) ---
    if (sub == 0x6) {
        if (b7_6 == 3) {
            if (!regs_.sr.supervisor) { RaiseIllegalInstruction(); return 4; }
            Operand src = ResolveEa(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), 2);
            SetSr(static_cast<std::uint16_t>(ReadOperand(src, 2)));
            return 12 + EaExtraCycles(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), 2);
        }
        const int size = (b7_6 == 0) ? 1 : (b7_6 == 1) ? 2 : 4;
        Operand dst = ResolveEa(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
        const std::uint32_t a = ReadOperand(dst, size);
        const std::uint32_t result = MaskToSize(~a, size);
        SetNzForResult(result, size);
        WriteOperand(dst, size, result);
        return 4 + EaExtraCycles(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
    }

    // --- NBCD/SWAP/PEA/EXT.W/EXT.L/MOVEM(reg->mem) ---
    if (sub == 0x8) {
        if (b7_6 == 0) {  // NBCD <ea>
            Operand dst = ResolveEa(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), 1);
            const std::uint8_t operand = static_cast<std::uint8_t>(ReadOperand(dst, 1));
            bool carry = false;
            const std::uint8_t result = BcdSub(0, operand, regs_.sr.x, carry);
            WriteOperand(dst, 1, result);
            ApplyBcdFlags(result, carry);
            return 6 + EaExtraCycles(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), 1);
        }
        if (b7_6 == 1) {
            if (mode == 0) {  // SWAP Dn
                const std::uint32_t v = regs_.d[reg];
                const std::uint32_t swapped = (v >> 16) | (v << 16);
                regs_.d[reg] = swapped;
                SetNzForResult(swapped, 4);
                return 4;
            }
            // PEA <ea>
            Operand ea = ResolveEa(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), 4);
            Push32(EffectiveAddressForOperand(ea));
            return 12 + EaExtraCycles(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), 4);
        }
        if (mode == 0) {  // EXT.W (b7_6==2) / EXT.L (b7_6==3), Dn only
            if (b7_6 == 2) {
                const std::uint32_t v = SignExtend(regs_.d[reg] & 0xFF, 1) & 0xFFFF;
                SetDataReg(reg, 2, v);
                SetNzForResult(v, 2);
            } else {
                const std::uint32_t v = SignExtend(regs_.d[reg] & 0xFFFF, 2);
                regs_.d[reg] = v;
                SetNzForResult(v, 4);
            }
            return 4;
        }
        // MOVEM reg->mem (b7_6==2: word, b7_6==3: long) - mode!=0 here.
        return ExecuteMovem(op);
    }

    // --- TST / TAS ---
    if (sub == 0xA) {
        if (b7_6 == 3) {  // TAS
            Operand dst = ResolveEa(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), 1);
            const std::uint32_t a = ReadOperand(dst, 1);
            SetNzForResult(a, 1);
            WriteOperand(dst, 1, a | 0x80);
            return 4 + EaExtraCycles(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), 1);
        }
        const int size = (b7_6 == 0) ? 1 : (b7_6 == 1) ? 2 : 4;
        Operand src = ResolveEa(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
        SetNzForResult(ReadOperand(src, size), size);
        return 4 + EaExtraCycles(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
    }

    // --- MOVEM mem->reg ---
    if (sub == 0xC) {
        return ExecuteMovem(op);
    }

    // --- CHK (b7_6==2) / LEA (b7_6==3) - bit8=1 forces sub to be odd ---
    if ((sub & 1) == 1) {
        const int destReg = (op >> 9) & 7;
        if (b7_6 == 2) {  // CHK Dn,<ea> (word, 68000 only - bounds check 0..Dn against source)
            Operand src = ResolveEa(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), 2);
            const std::int16_t bound = static_cast<std::int16_t>(ReadOperand(src, 2));
            const std::int16_t value = static_cast<std::int16_t>(GetDataReg(destReg, 2));
            if (value < 0) {
                regs_.sr.n = true;
                TakeException(6);
                return 40;
            }
            if (value > bound) {
                regs_.sr.n = false;
                TakeException(6);
                return 40;
            }
            return 10 + EaExtraCycles(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), 2);
        }
        if (b7_6 == 3 && mode != 0 && mode != 1) {  // LEA <ea>,An (control addressing modes only)
            Operand ea = ResolveEa(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), 4);
            regs_.a[destReg] = EffectiveAddressForOperand(ea);
            return 4 + EaExtraCycles(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), 4);
        }
        RaiseIllegalInstruction();
        return 4;
    }

    // --- sub==0xE: control/misc single-form instructions ---
    if (sub == 0xE) {
        if (op == 0x4E71) return 4;   // NOP
        if (op == 0x4E70) return 132; // RESET (no external devices to reset here)
        if (op == 0x4E72) {           // STOP #imm
            if (!regs_.sr.supervisor) { RaiseIllegalInstruction(); return 4; }
            const std::uint16_t imm = FetchExtensionWord();
            SetSr(imm);
            halted_ = true;
            return 4;
        }
        if (op == 0x4E73) {           // RTE
            if (!regs_.sr.supervisor) { RaiseIllegalInstruction(); return 4; }
            const std::uint16_t sr = Pop16();
            const std::uint32_t pc = Pop32();
            SetSr(sr);
            regs_.pc = pc;
            return 20;
        }
        if (op == 0x4E75) {           // RTS
            regs_.pc = Pop32();
            return 16;
        }
        if (op == 0x4E76) {           // TRAPV
            if (regs_.sr.v) { TakeException(7); return 34; }
            return 4;
        }
        if (op == 0x4E77) {           // RTR
            const std::uint16_t ccr = Pop16();
            SetCcr(ccr);
            regs_.pc = Pop32();
            return 20;
        }
        if ((op & 0xFFF0) == 0x4E40) {  // TRAP #n
            const int vec = op & 0xF;
            TakeException(static_cast<std::uint8_t>(32 + vec));
            return 34;
        }
        if ((op & 0xFFF0) == 0x4E60) {  // MOVE An,USP / MOVE USP,An
            if (!regs_.sr.supervisor) { RaiseIllegalInstruction(); return 4; }
            if (op & 0x08) regs_.a[reg] = regs_.usp; else regs_.usp = regs_.a[reg];
            return 4;
        }
        if ((op & 0xFFF8) == 0x4E50) {  // LINK An,#disp16
            const std::int16_t disp = static_cast<std::int16_t>(FetchExtensionWord());
            Push32(regs_.a[reg]);
            regs_.a[reg] = regs_.a[7];
            regs_.a[7] += static_cast<std::uint32_t>(disp);
            return 16;
        }
        if ((op & 0xFFF8) == 0x4E58) {  // UNLK An
            regs_.a[7] = regs_.a[reg];
            regs_.a[reg] = Pop32();
            return 12;
        }
        if ((op & 0xFFC0) == 0x4E80) {  // JSR <ea>
            Operand ea = ResolveEa(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), 4);
            const std::uint32_t target = EffectiveAddressForOperand(ea);
            Push32(regs_.pc);
            regs_.pc = target;
            return 16 + EaExtraCycles(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), 4);
        }
        if ((op & 0xFFC0) == 0x4EC0) {  // JMP <ea>
            Operand ea = ResolveEa(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), 4);
            regs_.pc = EffectiveAddressForOperand(ea);
            return 8 + EaExtraCycles(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), 4);
        }
        RaiseIllegalInstruction();
        return 4;
    }

    RaiseIllegalInstruction();
    return 4;
}

// ============================================================================
// Group5 (0x5xxx): ADDQ/SUBQ, Scc, DBcc
// ============================================================================

int MameDerivedM68000Adapter::Group5(std::uint16_t op) {
    const int sizeField = (op >> 6) & 3;
    const int mode = (op >> 3) & 7;
    const int reg = op & 7;
    const int cc = (op >> 8) & 0xF;

    if (sizeField == 3) {
        if (mode == 1) {  // DBcc Dn,disp16
            const std::uint32_t base = regs_.pc;
            const std::int16_t disp = static_cast<std::int16_t>(FetchExtensionWord());
            if (!TestCondition(cc)) {
                const std::uint16_t counter = static_cast<std::uint16_t>(GetDataReg(reg, 2) - 1);
                SetDataReg(reg, 2, counter);
                if (counter != 0xFFFF) {
                    regs_.pc = base + static_cast<std::uint32_t>(disp);
                    return 10;
                }
                return 14;
            }
            return 12;
        }
        // Scc <ea>
        Operand dst = ResolveEa(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), 1);
        WriteOperand(dst, 1, TestCondition(cc) ? 0xFFu : 0x00u);
        return 4 + EaExtraCycles(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), 1);
    }

    const int size = (sizeField == 0) ? 1 : (sizeField == 1) ? 2 : 4;
    int data = (op >> 9) & 7;
    if (data == 0) data = 8;
    const bool isSub = (op & 0x0100) != 0;

    Operand dst = ResolveEa(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
    std::uint32_t result;
    if (mode == 1) {
        // ADDQ/SUBQ to An: the entire 32-bit address register participates regardless of the
        // encoded size. Read regs_.a[reg] directly: ReadOperand() narrows a word-size
        // AddrReg operand to 16 bits (right for CMP.W An, wrong here).
        const std::uint32_t fullReg = regs_.a[reg];
        result = isSub ? (fullReg - static_cast<std::uint32_t>(data)) : (fullReg + static_cast<std::uint32_t>(data));
        WriteOperand(dst, 4, result);
    } else {
        const std::uint32_t a = ReadOperand(dst, size);
        result = isSub ? OpSub(a, static_cast<std::uint32_t>(data), size, false, false)
                        : OpAdd(a, static_cast<std::uint32_t>(data), size, false, false);
        WriteOperand(dst, size, result);
    }
    return 4 + EaExtraCycles(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
}

// ============================================================================
// Group6 (0x6xxx): Bcc/BRA/BSR
// ============================================================================

int MameDerivedM68000Adapter::Group6(std::uint16_t op) {
    const int cc = (op >> 8) & 0xF;
    const std::uint32_t base = regs_.pc;  // address right after the opcode word, before any extension word
    std::int32_t disp = static_cast<std::int8_t>(op & 0xFF);
    if ((op & 0xFF) == 0) {
        disp = static_cast<std::int16_t>(FetchExtensionWord());
    }
    const std::uint32_t target = base + static_cast<std::uint32_t>(disp);

    if (cc == 0) {  // BRA
        regs_.pc = target;
        return 10;
    }
    if (cc == 1) {  // BSR
        Push32(regs_.pc);
        regs_.pc = target;
        return 18;
    }
    if (TestCondition(cc)) {
        regs_.pc = target;
        return 10;
    }
    return 8;
}

// ============================================================================
// Group7 (0x7xxx): MOVEQ
// ============================================================================

int MameDerivedM68000Adapter::Group7(std::uint16_t op) {
    const int reg = (op >> 9) & 7;
    const std::uint32_t value = SignExtend(op & 0xFF, 1);
    SetDataReg(reg, 4, value);
    SetNzForResult(value, 4);
    return 4;
}

// ============================================================================
// Group8 (0x8xxx): OR, DIVU, DIVS, SBCD (register-direct form only)
// ============================================================================

int MameDerivedM68000Adapter::Group8(std::uint16_t op) {
    const int reg9 = (op >> 9) & 7;
    const int opmode = (op >> 6) & 7;
    const int mode = (op >> 3) & 7;
    const int reg = op & 7;

    if (opmode == 3 || opmode == 7) {  // DIVU.W / DIVS.W <ea>,Dn
        Operand src = ResolveEa(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), 2);
        const std::uint16_t divisorRaw = static_cast<std::uint16_t>(ReadOperand(src, 2));
        if (divisorRaw == 0) {
            TakeException(5);  // divide by zero
            return 38;
        }
        const std::uint32_t dividend = regs_.d[reg9];
        if (opmode == 3) {  // DIVU
            const std::uint32_t quotient = dividend / divisorRaw;
            const std::uint32_t remainder = dividend % divisorRaw;
            if (quotient > 0xFFFF) {
                regs_.sr.v = true;
            } else {
                regs_.d[reg9] = (remainder << 16) | (quotient & 0xFFFF);
                regs_.sr.c = false;
                regs_.sr.v = false;
                regs_.sr.n = (quotient & 0x8000) != 0;
                regs_.sr.z = (quotient == 0);
            }
        } else {  // DIVS
            const std::int32_t dividendS = static_cast<std::int32_t>(dividend);
            const std::int32_t divisorS = static_cast<std::int16_t>(divisorRaw);
            const std::int32_t quotient = dividendS / divisorS;
            const std::int32_t remainder = dividendS % divisorS;
            if (quotient > 32767 || quotient < -32768) {
                regs_.sr.v = true;
            } else {
                regs_.d[reg9] = (static_cast<std::uint32_t>(remainder) << 16) | (static_cast<std::uint32_t>(quotient) & 0xFFFF);
                regs_.sr.c = false;
                regs_.sr.v = false;
                regs_.sr.n = quotient < 0;
                regs_.sr.z = (quotient == 0);
            }
        }
        return 140;  // DIVU/DIVS are slow; a flat approximation is fine
    }

    if (opmode >= 4) {
        if (mode == 0) {
            // SBCD Dy,Dx (register-direct form only; the -(Ay),-(Ax) form is not implemented
            // and falls through to the generic OR handling below).
            // reg9 = Dx (destination), reg = Dy (source): result = Dx - Dy - X -> Dx.
            const std::uint8_t dstByte = static_cast<std::uint8_t>(GetDataReg(reg9, 1));
            const std::uint8_t srcByte = static_cast<std::uint8_t>(GetDataReg(reg, 1));
            bool carry = false;
            const std::uint8_t result = BcdSub(dstByte, srcByte, regs_.sr.x, carry);
            SetDataReg(reg9, 1, result);
            ApplyBcdFlags(result, carry);
            return 6;
        }
        const int size = (opmode == 4) ? 1 : (opmode == 5) ? 2 : 4;
        Operand dst = ResolveEa(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
        const std::uint32_t a = ReadOperand(dst, size);
        const std::uint32_t result = MaskToSize(a | regs_.d[reg9], size);
        SetNzForResult(result, size);
        WriteOperand(dst, size, result);
        return 8 + EaExtraCycles(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
    }

    const int size = (opmode == 0) ? 1 : (opmode == 1) ? 2 : 4;
    Operand src = ResolveEa(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
    const std::uint32_t result = MaskToSize(ReadOperand(src, size) | GetDataReg(reg9, size), size);
    SetNzForResult(result, size);
    SetDataReg(reg9, size, result);
    return 4 + EaExtraCycles(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
}

// ============================================================================
// Group9 (0x9xxx): SUB, SUBA, SUBX
// ============================================================================

int MameDerivedM68000Adapter::Group9(std::uint16_t op) {
    const int reg9 = (op >> 9) & 7;
    const int opmode = (op >> 6) & 7;
    const int mode = (op >> 3) & 7;
    const int reg = op & 7;

    if (opmode == 3 || opmode == 7) {  // SUBA
        const int size = (opmode == 3) ? 2 : 4;
        Operand src = ResolveEa(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
        const std::uint32_t value = SignExtend(ReadOperand(src, size), size);
        regs_.a[reg9] -= value;
        return 8 + EaExtraCycles(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
    }

    if (opmode >= 4) {
        if (mode == 0 || mode == 4) {
            const int size = (opmode == 4) ? 1 : (opmode == 5) ? 2 : 4;
            if (mode == 0) {
                const std::uint32_t a = GetDataReg(reg9, size);
                const std::uint32_t b = GetDataReg(reg, size);
                const std::uint32_t result = OpSub(a, b, size, true, true);
                SetDataReg(reg9, size, result);
            } else {
                Operand srcOp = ResolveEa(4, static_cast<std::uint8_t>(reg), size);
                Operand dstOp = ResolveEa(4, static_cast<std::uint8_t>(reg9), size);
                const std::uint32_t b = ReadOperand(srcOp, size);
                const std::uint32_t a = ReadOperand(dstOp, size);
                const std::uint32_t result = OpSub(a, b, size, true, true);
                WriteOperand(dstOp, size, result);
            }
            return 8;
        }
        const int size = (opmode == 4) ? 1 : (opmode == 5) ? 2 : 4;
        Operand dst = ResolveEa(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
        const std::uint32_t a = ReadOperand(dst, size);
        const std::uint32_t result = OpSub(a, GetDataReg(reg9, size), size, false, false);
        WriteOperand(dst, size, result);
        return 8 + EaExtraCycles(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
    }

    const int size = (opmode == 0) ? 1 : (opmode == 1) ? 2 : 4;
    Operand src = ResolveEa(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
    const std::uint32_t result = OpSub(GetDataReg(reg9, size), ReadOperand(src, size), size, false, false);
    SetDataReg(reg9, size, result);
    return 4 + EaExtraCycles(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
}

// ============================================================================
// GroupB (0xBxxx): CMP, CMPA, EOR, CMPM
// ============================================================================

int MameDerivedM68000Adapter::GroupB(std::uint16_t op) {
    const int reg9 = (op >> 9) & 7;
    const int opmode = (op >> 6) & 7;
    const int mode = (op >> 3) & 7;
    const int reg = op & 7;

    if (opmode == 3 || opmode == 7) {  // CMPA
        const int size = (opmode == 3) ? 2 : 4;
        Operand src = ResolveEa(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
        const std::uint32_t value = SignExtend(ReadOperand(src, size), size);
        OpCmp(regs_.a[reg9], value, 4);
        return 6 + EaExtraCycles(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
    }

    if (opmode >= 4) {
        const int size = (opmode == 4) ? 1 : (opmode == 5) ? 2 : 4;
        if (mode == 1) {  // CMPM (Ay)+,(Ax)+
            Operand srcOp = ResolveEa(3, static_cast<std::uint8_t>(reg), size);
            Operand dstOp = ResolveEa(3, static_cast<std::uint8_t>(reg9), size);
            OpCmp(ReadOperand(dstOp, size), ReadOperand(srcOp, size), size);
            return 12;
        }
        Operand dst = ResolveEa(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
        const std::uint32_t a = ReadOperand(dst, size);
        const std::uint32_t result = MaskToSize(a ^ GetDataReg(reg9, size), size);
        SetNzForResult(result, size);
        WriteOperand(dst, size, result);
        return 8 + EaExtraCycles(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
    }

    const int size = (opmode == 0) ? 1 : (opmode == 1) ? 2 : 4;
    Operand src = ResolveEa(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
    OpCmp(GetDataReg(reg9, size), ReadOperand(src, size), size);
    return 4 + EaExtraCycles(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
}

// ============================================================================
// GroupC (0xCxxx): AND, MULU, MULS, EXG, ABCD (register-direct form only)
// ============================================================================

int MameDerivedM68000Adapter::GroupC(std::uint16_t op) {
    const int reg9 = (op >> 9) & 7;
    const int opmode = (op >> 6) & 7;
    const int mode = (op >> 3) & 7;
    const int reg = op & 7;

    if (opmode == 3 || opmode == 7) {  // MULU.W / MULS.W <ea>,Dn
        Operand src = ResolveEa(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), 2);
        const std::uint16_t raw = static_cast<std::uint16_t>(ReadOperand(src, 2));
        std::uint32_t result;
        if (opmode == 3) {
            result = static_cast<std::uint32_t>(raw) * static_cast<std::uint32_t>(regs_.d[reg9] & 0xFFFF);
        } else {
            const std::int32_t a = static_cast<std::int16_t>(raw);
            const std::int32_t b = static_cast<std::int16_t>(regs_.d[reg9] & 0xFFFF);
            result = static_cast<std::uint32_t>(a * b);
        }
        regs_.d[reg9] = result;
        SetNzForResult(result, 4);
        return 70 + EaExtraCycles(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), 2);
    }

    if (opmode >= 4) {
        if (opmode == 4 && mode == 0) {
            // ABCD Dy,Dx (register-direct form only; the -(Ay),-(Ax) form is not implemented).
            // reg9 = Dx (destination), reg = Dy (source): result = Dx + Dy + X -> Dx.
            const std::uint8_t dstByte = static_cast<std::uint8_t>(GetDataReg(reg9, 1));
            const std::uint8_t srcByte = static_cast<std::uint8_t>(GetDataReg(reg, 1));
            bool carry = false;
            const std::uint8_t result = BcdAdd(dstByte, srcByte, regs_.sr.x, carry);
            SetDataReg(reg9, 1, result);
            ApplyBcdFlags(result, carry);
            return 6;
        }
        if (opmode == 5 && mode == 0) {  // EXG Dx,Dy
            std::swap(regs_.d[reg9], regs_.d[reg]);
            return 6;
        }
        if (opmode == 5 && mode == 1) {  // EXG Ax,Ay
            std::swap(regs_.a[reg9], regs_.a[reg]);
            return 6;
        }
        if (opmode == 6 && mode == 1) {  // EXG Dx,Ay
            std::swap(regs_.d[reg9], regs_.a[reg]);
            return 6;
        }
        const int size = (opmode == 4) ? 1 : (opmode == 5) ? 2 : 4;
        Operand dst = ResolveEa(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
        const std::uint32_t a = ReadOperand(dst, size);
        const std::uint32_t result = MaskToSize(a & regs_.d[reg9], size);
        SetNzForResult(result, size);
        WriteOperand(dst, size, result);
        return 8 + EaExtraCycles(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
    }

    const int size = (opmode == 0) ? 1 : (opmode == 1) ? 2 : 4;
    Operand src = ResolveEa(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
    const std::uint32_t result = MaskToSize(ReadOperand(src, size) & GetDataReg(reg9, size), size);
    SetNzForResult(result, size);
    SetDataReg(reg9, size, result);
    return 4 + EaExtraCycles(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
}

// ============================================================================
// GroupD (0xDxxx): ADD, ADDA, ADDX
// ============================================================================

int MameDerivedM68000Adapter::GroupD(std::uint16_t op) {
    const int reg9 = (op >> 9) & 7;
    const int opmode = (op >> 6) & 7;
    const int mode = (op >> 3) & 7;
    const int reg = op & 7;

    if (opmode == 3 || opmode == 7) {  // ADDA
        const int size = (opmode == 3) ? 2 : 4;
        Operand src = ResolveEa(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
        const std::uint32_t value = SignExtend(ReadOperand(src, size), size);
        regs_.a[reg9] += value;
        return 8 + EaExtraCycles(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
    }

    if (opmode >= 4) {
        if (mode == 0 || mode == 4) {
            const int size = (opmode == 4) ? 1 : (opmode == 5) ? 2 : 4;
            if (mode == 0) {
                const std::uint32_t a = GetDataReg(reg9, size);
                const std::uint32_t b = GetDataReg(reg, size);
                const std::uint32_t result = OpAdd(a, b, size, true, true);
                SetDataReg(reg9, size, result);
            } else {
                Operand srcOp = ResolveEa(4, static_cast<std::uint8_t>(reg), size);
                Operand dstOp = ResolveEa(4, static_cast<std::uint8_t>(reg9), size);
                const std::uint32_t b = ReadOperand(srcOp, size);
                const std::uint32_t a = ReadOperand(dstOp, size);
                const std::uint32_t result = OpAdd(a, b, size, true, true);
                WriteOperand(dstOp, size, result);
            }
            return 8;
        }
        const int size = (opmode == 4) ? 1 : (opmode == 5) ? 2 : 4;
        Operand dst = ResolveEa(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
        const std::uint32_t a = ReadOperand(dst, size);
        const std::uint32_t result = OpAdd(a, GetDataReg(reg9, size), size, false, false);
        WriteOperand(dst, size, result);
        return 8 + EaExtraCycles(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
    }

    const int size = (opmode == 0) ? 1 : (opmode == 1) ? 2 : 4;
    Operand src = ResolveEa(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
    const std::uint32_t result = OpAdd(GetDataReg(reg9, size), ReadOperand(src, size), size, false, false);
    SetDataReg(reg9, size, result);
    return 4 + EaExtraCycles(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
}

// ============================================================================
// GroupE (0xExxx): shift/rotate (ASx/LSx/ROXx/ROx), register and memory forms
// ============================================================================

std::uint32_t MameDerivedM68000Adapter::ShiftStep(std::uint32_t value, int size, int type, bool left) {
    const std::uint32_t signBit = (size == 1) ? 0x80u : (size == 2) ? 0x8000u : 0x80000000u;
    const std::uint32_t sizeMask = (size == 1) ? 0xFFu : (size == 2) ? 0xFFFFu : 0xFFFFFFFFu;
    value &= sizeMask;
    std::uint32_t result = value;
    bool carryOut = false;
    bool overflow = false;

    switch (type) {
        case 0:  // AS
            if (left) {
                const bool oldSign = (value & signBit) != 0;
                carryOut = oldSign;
                result = (value << 1) & sizeMask;
                overflow = oldSign != ((result & signBit) != 0);
            } else {
                carryOut = (value & 1) != 0;
                result = ((value >> 1) | ((value & signBit) ? signBit : 0)) & sizeMask;
            }
            regs_.sr.x = carryOut;
            break;
        case 1:  // LS
            if (left) {
                carryOut = (value & signBit) != 0;
                result = (value << 1) & sizeMask;
            } else {
                carryOut = (value & 1) != 0;
                result = (value >> 1) & sizeMask;
            }
            regs_.sr.x = carryOut;
            break;
        case 2:  // ROX (rotate through X)
            if (left) {
                carryOut = (value & signBit) != 0;
                result = ((value << 1) | (regs_.sr.x ? 1u : 0u)) & sizeMask;
            } else {
                carryOut = (value & 1) != 0;
                result = ((value >> 1) | (regs_.sr.x ? signBit : 0u)) & sizeMask;
            }
            regs_.sr.x = carryOut;
            break;
        case 3:  // RO (plain rotate, X unaffected)
        default:
            if (left) {
                carryOut = (value & signBit) != 0;
                result = ((value << 1) | (carryOut ? 1u : 0u)) & sizeMask;
            } else {
                carryOut = (value & 1) != 0;
                result = ((value >> 1) | (carryOut ? signBit : 0u)) & sizeMask;
            }
            break;
    }

    regs_.sr.c = carryOut;
    regs_.sr.n = (result & signBit) != 0;
    regs_.sr.z = (result == 0);
    regs_.sr.v = (type == 0) ? overflow : false;
    return result;
}

int MameDerivedM68000Adapter::GroupE(std::uint16_t op) {
    const int sizeField = (op >> 6) & 3;
    const bool left = (op & 0x0100) != 0;

    if (sizeField == 3) {
        // Memory shift/rotate: single bit, word size only, ea in bits 5-0, shift type in
        // bits 10-9.
        const int type = (op >> 9) & 3;
        const int mode = (op >> 3) & 7;
        const int reg = op & 7;
        Operand dst = ResolveEa(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), 2);
        const std::uint32_t result = ShiftStep(ReadOperand(dst, 2), 2, type, left);
        WriteOperand(dst, 2, result);
        return 8 + EaExtraCycles(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), 2);
    }

    const int size = (sizeField == 0) ? 1 : (sizeField == 1) ? 2 : 4;
    const int countOrReg = (op >> 9) & 7;
    const bool regCount = (op & 0x0020) != 0;
    const int type = (op >> 3) & 3;
    const int reg = op & 7;
    const int count = regCount ? static_cast<int>(GetDataReg(countOrReg, 4) & 63)
                                : (countOrReg == 0 ? 8 : countOrReg);

    std::uint32_t value = GetDataReg(reg, size);
    if (count == 0) {
        regs_.sr.c = false;
        SetNzForResult(value, size);
    }
    bool anyOverflow = false;
    for (int i = 0; i < count; ++i) {
        value = ShiftStep(value, size, type, left);
        anyOverflow = anyOverflow || regs_.sr.v;
    }
    if (type == 0 && count > 0) regs_.sr.v = anyOverflow;
    SetDataReg(reg, size, value);
    return 6 + 2 * count;
}

MameDerivedM68000Adapter::RegisterSnapshot MameDerivedM68000Adapter::Snapshot() const {
    RegisterSnapshot snap;
    for (int i = 0; i < 8; ++i) snap.d[i] = regs_.d[i];
    for (int i = 0; i < 8; ++i) snap.a[i] = regs_.a[i];
    snap.usp = regs_.sr.supervisor ? regs_.usp : regs_.a[7];
    snap.ssp = regs_.sr.supervisor ? regs_.a[7] : regs_.ssp;
    snap.pc = regs_.pc;
    snap.sr = GetSr();
    snap.halted = halted_;
    snap.interruptServiceCount = interruptServiceCount_;
    snap.illegalInstructionCount = illegalInstructionCount_;
    return snap;
}

// ============================================================================
// Addressing modes
// ============================================================================
//
// mode/reg fields per the standard 68000 6-bit effective-address encoding:
//   mode 0: Dn              mode 4: -(An)             mode 7,reg 0: (xxx).W
//   mode 1: An               mode 5: (d16,An)          mode 7,reg 1: (xxx).L
//   mode 2: (An)             mode 6: (d8,An,Xn)        mode 7,reg 2: (d16,PC)
//   mode 3: (An)+                                       mode 7,reg 3: (d8,PC,Xn)
//                                                        mode 7,reg 4: #imm

MameDerivedM68000Adapter::Operand MameDerivedM68000Adapter::ResolveEa(std::uint8_t mode, std::uint8_t reg, int size) {
    Operand op;
    switch (mode) {
        case 0:
            op.kind = OperandKind::DataReg;
            op.reg = reg;
            return op;
        case 1:
            op.kind = OperandKind::AddrReg;
            op.reg = reg;
            return op;
        case 2:
            op.kind = OperandKind::Memory;
            op.address = regs_.a[reg];
            return op;
        case 3: {
            op.kind = OperandKind::Memory;
            op.address = regs_.a[reg];
            // A7 always steps by 2 minimum (even for .B) to stay word-aligned.
            const std::uint32_t step = (size == 1 && reg == 7) ? 2u : static_cast<std::uint32_t>(size);
            regs_.a[reg] += step;
            return op;
        }
        case 4: {
            const std::uint32_t step = (size == 1 && reg == 7) ? 2u : static_cast<std::uint32_t>(size);
            regs_.a[reg] -= step;
            op.kind = OperandKind::Memory;
            op.address = regs_.a[reg];
            return op;
        }
        case 5: {
            const std::int16_t disp = static_cast<std::int16_t>(FetchExtensionWord());
            op.kind = OperandKind::Memory;
            op.address = regs_.a[reg] + static_cast<std::uint32_t>(disp);
            return op;
        }
        case 6: {
            const std::uint16_t ext = FetchExtensionWord();
            const int xn = (ext >> 12) & 7;
            const bool xnIsAddr = (ext & 0x8000) != 0;
            const bool xnIsLong = (ext & 0x0800) != 0;
            std::uint32_t xnValue = xnIsAddr ? regs_.a[xn] : regs_.d[xn];
            if (!xnIsLong) xnValue = SignExtend(xnValue & 0xFFFF, 2);
            const std::int8_t disp = static_cast<std::int8_t>(ext & 0xFF);
            op.kind = OperandKind::Memory;
            op.address = regs_.a[reg] + xnValue + static_cast<std::uint32_t>(disp);
            return op;
        }
        case 7:
            switch (reg) {
                case 0: {
                    const std::int16_t addr = static_cast<std::int16_t>(FetchExtensionWord());
                    op.kind = OperandKind::Memory;
                    op.address = static_cast<std::uint32_t>(addr);  // sign-extended, matches real hardware
                    return op;
                }
                case 1: {
                    op.kind = OperandKind::Memory;
                    op.address = FetchExtensionLong();
                    return op;
                }
                case 2: {
                    const std::uint32_t base = regs_.pc;
                    const std::int16_t disp = static_cast<std::int16_t>(FetchExtensionWord());
                    op.kind = OperandKind::Memory;
                    op.address = base + static_cast<std::uint32_t>(disp);
                    return op;
                }
                case 3: {
                    const std::uint32_t base = regs_.pc;
                    const std::uint16_t ext = FetchExtensionWord();
                    const int xn = (ext >> 12) & 7;
                    const bool xnIsAddr = (ext & 0x8000) != 0;
                    const bool xnIsLong = (ext & 0x0800) != 0;
                    std::uint32_t xnValue = xnIsAddr ? regs_.a[xn] : regs_.d[xn];
                    if (!xnIsLong) xnValue = SignExtend(xnValue & 0xFFFF, 2);
                    const std::int8_t disp = static_cast<std::int8_t>(ext & 0xFF);
                    op.kind = OperandKind::Memory;
                    op.address = base + xnValue + static_cast<std::uint32_t>(disp);
                    return op;
                }
                case 4: {
                    op.kind = OperandKind::Immediate;
                    op.immediate = (size == 4) ? FetchExtensionLong()
                                                : static_cast<std::uint32_t>(FetchExtensionWord() & 0xFFFF);
                    return op;
                }
                default:
                    RaiseIllegalInstruction();
                    return op;
            }
        default:
            RaiseIllegalInstruction();
            return op;
    }
}

std::uint32_t MameDerivedM68000Adapter::ReadOperand(const Operand& op, int size) const {
    switch (op.kind) {
        case OperandKind::DataReg: return GetDataReg(op.reg, size);
        case OperandKind::AddrReg: return (size == 2) ? SignExtend(regs_.a[op.reg & 7] & 0xFFFF, 2) : regs_.a[op.reg & 7];
        case OperandKind::Memory:
            switch (size) {
                case 1: return Read8(op.address);
                case 2: return Read16(op.address);
                default: return Read32(op.address);
            }
        case OperandKind::Immediate: return op.immediate;
    }
    return 0;
}

void MameDerivedM68000Adapter::WriteOperand(const Operand& op, int size, std::uint32_t value) {
    switch (op.kind) {
        case OperandKind::DataReg:
            SetDataReg(op.reg, size, value);
            return;
        case OperandKind::AddrReg:
            // MOVEA/An-destination writes always sign-extend a .W value to
            // the full 32-bit register (no partial-write form exists for An).
            regs_.a[op.reg & 7] = (size == 2) ? SignExtend(value & 0xFFFF, 2) : value;
            return;
        case OperandKind::Memory:
            switch (size) {
                case 1: Write8(op.address, static_cast<std::uint8_t>(value)); return;
                case 2: Write16(op.address, static_cast<std::uint16_t>(value)); return;
                default: Write32(op.address, value); return;
            }
        case OperandKind::Immediate:
            return;  // write to an immediate is not a valid encoding; no-op defensively
    }
}

std::uint32_t MameDerivedM68000Adapter::EffectiveAddressForOperand(const Operand& op) const {
    // Memory-kind operands only (LEA/PEA/JMP/JSR never use register-direct or immediate EAs).
    return op.address;
}

int MameDerivedM68000Adapter::EaExtraCycles(std::uint8_t mode, std::uint8_t reg, int size) const {
    const bool isLong = size == 4;
    switch (mode) {
        case 0: case 1: return 0;
        case 2: return isLong ? 8 : 4;
        case 3: return isLong ? 8 : 4;
        case 4: return isLong ? 10 : 6;
        case 5: return isLong ? 12 : 8;
        case 6: return isLong ? 14 : 10;
        case 7:
            switch (reg) {
                case 0: return isLong ? 12 : 8;
                case 1: return isLong ? 16 : 12;
                case 2: return isLong ? 12 : 8;
                case 3: return isLong ? 14 : 10;
                case 4: return isLong ? 8 : 4;
                default: return 0;
            }
        default: return 0;
    }
}

// ============================================================================
// Execute() / dispatch
// ============================================================================

std::uint64_t MameDerivedM68000Adapter::Execute(std::uint64_t cycleBudget) {
    cyclesConsumedThisCall_ = 0;
    while (cyclesConsumedThisCall_ < cycleBudget) {
        CheckPendingInterrupt();
        if (halted_) {
            // STOP: nothing to execute until an interrupt wakes the CPU; consume the rest of
            // the budget so the caller doesn't spin.
            cyclesConsumedThisCall_ = cycleBudget;
            break;
        }
        ExecuteOneInstruction();
    }
    return cyclesConsumedThisCall_;
}

void MameDerivedM68000Adapter::ExecuteOneInstruction() {
    const std::uint16_t op = FetchOpcodeWord();
    int cycles = 4;
    switch ((op >> 12) & 0xF) {
        case 0x0: cycles = Group0(op); break;
        case 0x1: cycles = Group1(op); break;
        case 0x2: cycles = Group2(op); break;
        case 0x3: cycles = Group3(op); break;
        case 0x4: cycles = Group4(op); break;
        case 0x5: cycles = Group5(op); break;
        case 0x6: cycles = Group6(op); break;
        case 0x7: cycles = Group7(op); break;
        case 0x8: cycles = Group8(op); break;
        case 0x9: cycles = Group9(op); break;
        case 0xA:
            RaiseIllegalInstruction();
            cycles = 4;
            break;
        case 0xB: cycles = GroupB(op); break;
        case 0xC: cycles = GroupC(op); break;
        case 0xD: cycles = GroupD(op); break;
        case 0xE: cycles = GroupE(op); break;
        default:  // 0xF: line-F, coprocessor/FPU escape - illegal on a bare 68000
            RaiseIllegalInstruction();
            cycles = 4;
            break;
    }
    cyclesConsumedThisCall_ += static_cast<std::uint64_t>(cycles);
}

// ============================================================================
// Condition codes
// ============================================================================

bool MameDerivedM68000Adapter::TestCondition(int cc) const {
    const M68000StatusRegister& sr = regs_.sr;
    switch (cc & 0x0F) {
        case 0x0: return true;                                    // T
        case 0x1: return false;                                   // F
        case 0x2: return !sr.c && !sr.z;                          // HI
        case 0x3: return sr.c || sr.z;                            // LS
        case 0x4: return !sr.c;                                   // CC (HI or same / no carry)
        case 0x5: return sr.c;                                    // CS
        case 0x6: return !sr.z;                                   // NE
        case 0x7: return sr.z;                                    // EQ
        case 0x8: return !sr.v;                                   // VC
        case 0x9: return sr.v;                                    // VS
        case 0xA: return !sr.n;                                   // PL
        case 0xB: return sr.n;                                    // MI
        case 0xC: return sr.n == sr.v;                            // GE
        case 0xD: return sr.n != sr.v;                            // LT
        case 0xE: return !sr.z && (sr.n == sr.v);                 // GT
        case 0xF: return sr.z || (sr.n != sr.v);                  // LE
        default: return false;
    }
}

// ============================================================================
// Exceptions / interrupts
// ============================================================================

void MameDerivedM68000Adapter::TakeException(std::uint8_t vector, bool isInterruptAutovector,
                                              std::uint8_t autovectorLevel) {
    const std::uint16_t savedSr = GetSr();
    const std::uint32_t savedPc = regs_.pc;

    // Enter supervisor mode, clear trace, before pushing (real hardware
    // pushes the *old* SR value but the CPU is already running in
    // supervisor mode - i.e. the push uses the post-switch SSP, but the
    // pushed SR content reflects the pre-exception state).
    if (!regs_.sr.supervisor) {
        regs_.usp = regs_.a[7];
        regs_.a[7] = regs_.ssp;
    }
    regs_.sr.supervisor = true;
    regs_.sr.trace = false;

    Push32(savedPc);
    Push16(savedSr);

    if (isInterruptAutovector) {
        regs_.sr.interruptMask = autovectorLevel;
        if (autovectorAck_) autovectorAck_(autovectorLevel);
    }

    regs_.pc = Read32(static_cast<std::uint32_t>(vector) * 4);
}

void MameDerivedM68000Adapter::CheckPendingInterrupt() {
    if (interruptLevel_ == 0) return;
    // Level 7 is edge-sensitive/always-taken on real hardware; levels 1-6
    // are masked when <= the current SR interrupt mask.
    if (interruptLevel_ != 7 && interruptLevel_ <= regs_.sr.interruptMask) return;

    ++interruptServiceCount_;
    halted_ = false;  // STOP is woken by any interrupt at a high enough priority
    TakeException(static_cast<std::uint8_t>(24 + interruptLevel_), /*isInterruptAutovector=*/true, interruptLevel_);
    // Standard published autovectored-interrupt overhead (approximate); charged here
    // because CheckPendingInterrupt() runs outside ExecuteOneInstruction()'s cycle path.
    cyclesConsumedThisCall_ += 44;
}

// ============================================================================
// ALU / flag helpers
// ============================================================================
//
// N/Z/V/C/X are computed explicitly per standard 68000 semantics (not Musashi's
// bit-position sentinel trick). ADDX/SUBX/NEGX (withExtend=true) leave Z unchanged when
// the result is zero and clear it when nonzero, so a multi-word chain reports "whole
// result was zero" correctly.

std::uint32_t MameDerivedM68000Adapter::OpAdd(std::uint32_t a, std::uint32_t b, int size, bool withExtend,
                                               bool useExtendCarryIn) {
    const std::uint64_t rangeMask = (size == 1) ? 0xFFull : (size == 2) ? 0xFFFFull : 0xFFFFFFFFull;
    const std::uint32_t signBit = (size == 1) ? 0x80u : (size == 2) ? 0x8000u : 0x80000000u;
    a &= static_cast<std::uint32_t>(rangeMask);
    b &= static_cast<std::uint32_t>(rangeMask);
    const std::uint64_t carryIn = (useExtendCarryIn && regs_.sr.x) ? 1ull : 0ull;
    const std::uint64_t wide = static_cast<std::uint64_t>(a) + static_cast<std::uint64_t>(b) + carryIn;
    const std::uint32_t result = static_cast<std::uint32_t>(wide & rangeMask);
    const bool carry = (wide & ~rangeMask) != 0;
    const bool overflow = (((a ^ result) & (b ^ result)) & signBit) != 0;

    regs_.sr.c = carry;
    regs_.sr.x = carry;
    regs_.sr.v = overflow;
    regs_.sr.n = (result & signBit) != 0;
    if (withExtend) {
        if (result != 0) regs_.sr.z = false;
    } else {
        regs_.sr.z = (result == 0);
    }
    return result;
}

std::uint32_t MameDerivedM68000Adapter::OpSub(std::uint32_t a, std::uint32_t b, int size, bool withExtend,
                                               bool useExtendCarryIn) {
    const std::uint64_t rangeMask = (size == 1) ? 0xFFull : (size == 2) ? 0xFFFFull : 0xFFFFFFFFull;
    const std::uint32_t signBit = (size == 1) ? 0x80u : (size == 2) ? 0x8000u : 0x80000000u;
    a &= static_cast<std::uint32_t>(rangeMask);
    b &= static_cast<std::uint32_t>(rangeMask);
    const std::int64_t carryIn = (useExtendCarryIn && regs_.sr.x) ? 1 : 0;
    const std::int64_t wide =
        static_cast<std::int64_t>(a) - static_cast<std::int64_t>(b) - carryIn;
    const std::uint32_t result = static_cast<std::uint32_t>(wide) & static_cast<std::uint32_t>(rangeMask);
    const bool borrow = wide < 0;
    const bool overflow = (((a ^ b) & (a ^ result)) & signBit) != 0;

    regs_.sr.c = borrow;
    regs_.sr.x = borrow;
    regs_.sr.v = overflow;
    regs_.sr.n = (result & signBit) != 0;
    if (withExtend) {
        if (result != 0) regs_.sr.z = false;
    } else {
        regs_.sr.z = (result == 0);
    }
    return result;
}

// CMP (and CMPA/CMPI/CMPM) - same math as a non-extended SUB, but the X
// flag is never touched (real hardware behavior: CMP updates N/Z/V/C only).
void MameDerivedM68000Adapter::OpCmp(std::uint32_t a, std::uint32_t b, int size) {
    const bool savedX = regs_.sr.x;
    OpSub(a, b, size, /*withExtend=*/false, /*useExtendCarryIn=*/false);
    regs_.sr.x = savedX;
}

void MameDerivedM68000Adapter::SetNzForResult(std::uint32_t result, int size) {
    const std::uint32_t signBit = (size == 1) ? 0x80u : (size == 2) ? 0x8000u : 0x80000000u;
    result = MaskToSize(result, size);
    regs_.sr.n = (result & signBit) != 0;
    regs_.sr.z = (result == 0);
    regs_.sr.v = false;
    regs_.sr.c = false;
}

// ============================================================================
// MOVE (Group1/2/3 share this - identical encoding, differ only in size)
// ============================================================================

int MameDerivedM68000Adapter::ExecuteMove(std::uint16_t op, int size) {
    const int srcMode = (op >> 3) & 7;
    const int srcReg = op & 7;
    const int destMode = (op >> 6) & 7;
    const int destReg = (op >> 9) & 7;

    Operand src = ResolveEa(static_cast<std::uint8_t>(srcMode), static_cast<std::uint8_t>(srcReg), size);
    const std::uint32_t value = ReadOperand(src, size);

    Operand dst = ResolveEa(static_cast<std::uint8_t>(destMode), static_cast<std::uint8_t>(destReg), size);
    if (destMode == 1) {
        WriteOperand(dst, size, value);  // MOVEA: no flags touched
    } else {
        SetNzForResult(value, size);
        WriteOperand(dst, size, value);
    }
    return 4 + EaExtraCycles(static_cast<std::uint8_t>(srcMode), static_cast<std::uint8_t>(srcReg), size) +
           EaExtraCycles(static_cast<std::uint8_t>(destMode), static_cast<std::uint8_t>(destReg), size);
}

int MameDerivedM68000Adapter::Group1(std::uint16_t op) { return ExecuteMove(op, 1); }
int MameDerivedM68000Adapter::Group2(std::uint16_t op) { return ExecuteMove(op, 4); }
int MameDerivedM68000Adapter::Group3(std::uint16_t op) { return ExecuteMove(op, 2); }

// ============================================================================
// Group0 (0x0xxx): ORI/ANDI/EORI/SUBI/ADDI/CMPI (+ ...to CCR/SR forms),
// static/dynamic BTST/BCHG/BCLR/BSET, MOVEP.
// ============================================================================

int MameDerivedM68000Adapter::ExecuteBitOp(std::uint16_t op, int bitNumber, bool /*dynamicBitNumber*/) {
    const int opSel = (op >> 6) & 3;  // 00=BTST,01=BCHG,10=BCLR,11=BSET
    const int mode = (op >> 3) & 7;
    const int reg = op & 7;
    // Bit number is modulo 32 against a Dn destination, modulo 8 against a
    // memory-byte destination (real hardware behavior - the two operand
    // shapes have different natural widths).
    const bool isDataReg = (mode == 0);
    const int size = isDataReg ? 4 : 1;
    bitNumber = isDataReg ? (bitNumber & 31) : (bitNumber & 7);

    Operand dst = ResolveEa(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
    std::uint32_t value = ReadOperand(dst, size);
    const std::uint32_t mask = 1u << bitNumber;
    regs_.sr.z = (value & mask) == 0;

    switch (opSel) {
        case 0: break;                         // BTST: test only
        case 1: value ^= mask; WriteOperand(dst, size, value); break;  // BCHG
        case 2: value &= ~mask; WriteOperand(dst, size, value); break; // BCLR
        case 3: value |= mask; WriteOperand(dst, size, value); break;  // BSET
        default: break;
    }
    return 4 + EaExtraCycles(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
}

int MameDerivedM68000Adapter::Group0(std::uint16_t op) {
    const int b11_9 = (op >> 9) & 7;
    const int bit8 = (op >> 8) & 1;

    if (bit8 == 1) {
        const int mode = (op >> 3) & 7;
        if (mode == 1) {
            // MOVEP: bits 11-9=Dy, bits 8-6=opmode (100=word mem->reg, 101=long mem->reg,
            // 110=word reg->mem, 111=long reg->mem), bits 2-0=Ax. EA is (d16,Ax). Only every
            // other byte (EA, EA+2, ...) is transferred: an 8-bit peripheral on half of the
            // 16-bit bus.
            const int dy = (op >> 9) & 7;
            const int opmode = (op >> 6) & 7;
            const int ax = op & 7;
            const std::int16_t disp = static_cast<std::int16_t>(FetchExtensionWord());
            const std::uint32_t ea = regs_.a[ax] + static_cast<std::uint32_t>(disp);
            const bool isLong = (opmode & 1) != 0;   // 101/111 = long, 100/110 = word
            const bool toMemory = (opmode & 2) != 0; // 110/111 = reg->mem, 100/101 = mem->reg
            const int byteCount = isLong ? 4 : 2;
            if (toMemory) {
                const std::uint32_t value = GetDataReg(dy, 4);
                for (int i = 0; i < byteCount; ++i) {
                    const int shift = 8 * (byteCount - 1 - i);
                    Write8(ea + static_cast<std::uint32_t>(2 * i), static_cast<std::uint8_t>(value >> shift));
                }
            } else {
                std::uint32_t value = 0;
                for (int i = 0; i < byteCount; ++i) {
                    value = (value << 8) | Read8(ea + static_cast<std::uint32_t>(2 * i));
                }
                SetDataReg(dy, isLong ? 4 : 2, value);
            }
            return isLong ? 24 : 16;  // Motorola's published MOVEP timing
        }
        // Dynamic BTST/BCHG/BCLR/BSET: bit number comes from Dn (b11_9).
        const int bitNumber = static_cast<int>(GetDataReg(b11_9, 4));
        return ExecuteBitOp(op, bitNumber, /*dynamicBitNumber=*/true);
    }

    if (b11_9 == 4) {
        // Static BTST/BCHG/BCLR/BSET: bit number is an extension word.
        const int bitNumber = static_cast<int>(FetchExtensionWord() & 0xFF);
        return ExecuteBitOp(op, bitNumber, /*dynamicBitNumber=*/false);
    }

    // ORI/ANDI/EORI/SUBI/ADDI/CMPI to <ea> (b11_9: 0=ORI,1=ANDI,2=SUBI,3=ADDI,5=EORI,6=CMPI; 7=reserved/illegal)
    const int sizeField = (op >> 6) & 3;
    if (sizeField == 3 || b11_9 == 7) {
        RaiseIllegalInstruction();
        return 4;
    }
    const int size = (sizeField == 0) ? 1 : (sizeField == 1) ? 2 : 4;
    const int mode = (op >> 3) & 7;
    const int reg = op & 7;

    // #imm to CCR/SR: fixed ea = mode 111, reg 100 (the "#imm" slot),
    // repurposed for ORI/ANDI/EORI only (real hardware behavior - SUBI/
    // ADDI/CMPI never target CCR/SR this way).
    const bool isImmSlot = (mode == 7 && reg == 4);
    if (isImmSlot && (b11_9 == 0 || b11_9 == 1 || b11_9 == 5)) {
        const std::uint16_t imm = FetchExtensionWord();
        if (size == 1) {
            // ...to CCR (only the low byte of the extension word is used).
            std::uint16_t ccr = GetSr() & 0x1F;
            switch (b11_9) {
                case 0: ccr |= (imm & 0xFF); break;
                case 1: ccr &= (imm & 0xFF); break;
                case 5: ccr ^= (imm & 0xFF); break;
                default: break;
            }
            SetCcr(ccr);
            return 20;
        }
        // ...to SR (privileged).
        if (!regs_.sr.supervisor) {
            RaiseIllegalInstruction();
            return 4;
        }
        std::uint16_t sr = GetSr();
        switch (b11_9) {
            case 0: sr |= imm; break;
            case 1: sr &= imm; break;
            case 5: sr ^= imm; break;
            default: break;
        }
        SetSr(sr);
        return 20;
    }

    const std::uint32_t imm = (size == 4) ? FetchExtensionLong() : static_cast<std::uint32_t>(FetchExtensionWord());
    Operand dst = ResolveEa(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
    const std::uint32_t a = ReadOperand(dst, size);
    std::uint32_t result = 0;
    switch (b11_9) {
        case 0: result = a | imm; SetNzForResult(result, size); WriteOperand(dst, size, result); break;  // ORI
        case 1: result = a & imm; SetNzForResult(result, size); WriteOperand(dst, size, result); break;  // ANDI
        case 5: result = a ^ imm; SetNzForResult(result, size); WriteOperand(dst, size, result); break;  // EORI
        case 2: result = OpSub(a, imm, size, false, false); WriteOperand(dst, size, result); break;      // SUBI
        case 3: result = OpAdd(a, imm, size, false, false); WriteOperand(dst, size, result); break;      // ADDI
        case 6: OpCmp(a, imm, size); break;                                                              // CMPI
        default: break;
    }
    return 8 + EaExtraCycles(static_cast<std::uint8_t>(mode), static_cast<std::uint8_t>(reg), size);
}

void MameDerivedM68000Adapter::RaiseIllegalInstruction() {
    ++illegalInstructionCount_;
    // FetchOpcodeWord() already advanced PC past the opcode word; real hardware pushes the
    // address of the illegal instruction itself, so back it up by 2.
    regs_.pc -= 2;
    if (illegalInstructionObserver_) illegalInstructionObserver_(regs_.pc, lastFetchedOpcodeForTest_);
    TakeException(4);  // vector 4: illegal instruction
}

std::uint8_t MameDerivedM68000Adapter::BcdAdd(std::uint8_t dst, std::uint8_t src, bool extend,
                                               bool& carryOut) const {
    int lo = (dst & 0xF) + (src & 0xF) + (extend ? 1 : 0);
    int carryLo = 0;
    if (lo > 9) {
        lo -= 10;
        carryLo = 1;
    }
    int hi = ((dst >> 4) & 0xF) + ((src >> 4) & 0xF) + carryLo;
    carryOut = false;
    if (hi > 9) {
        hi -= 10;
        carryOut = true;
    }
    return static_cast<std::uint8_t>((hi << 4) | lo);
}

std::uint8_t MameDerivedM68000Adapter::BcdSub(std::uint8_t dst, std::uint8_t src, bool extend,
                                               bool& carryOut) const {
    int lo = (dst & 0xF) - (src & 0xF) - (extend ? 1 : 0);
    int borrowLo = 0;
    if (lo < 0) {
        lo += 10;
        borrowLo = 1;
    }
    int hi = ((dst >> 4) & 0xF) - ((src >> 4) & 0xF) - borrowLo;
    carryOut = false;
    if (hi < 0) {
        hi += 10;
        carryOut = true;
    }
    return static_cast<std::uint8_t>((hi << 4) | lo);
}

void MameDerivedM68000Adapter::ApplyBcdFlags(std::uint8_t result, bool carry) {
    regs_.sr.n = (result & 0x80) != 0;
    regs_.sr.x = carry;
    regs_.sr.c = carry;
    regs_.sr.v = false;
    if (result != 0) regs_.sr.z = false;
}
