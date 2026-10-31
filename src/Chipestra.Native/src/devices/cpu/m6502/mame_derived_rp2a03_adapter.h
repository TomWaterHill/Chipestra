// license:BSD-3-Clause
// copyright-holders:Olivier Galibert
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/cpu/m6502/m6502.cpp  [copyright-holders:Olivier Galibert]
//   src/devices/cpu/m6502/m6502.h  [copyright-holders:Olivier Galibert]
//   src/devices/cpu/m6502/rp2a03.cpp  [copyright-holders:Olivier Galibert]
//   src/devices/cpu/m6502/rp2a03.h  [copyright-holders:Olivier Galibert]
//   src/devices/cpu/m6502/m6502make.py  [copyright-holders:Olivier Galibert]
//   src/devices/cpu/m6502/om6502.lst, orp2a03.lst, drp2a03.lst  [copyright-holders:Olivier Galibert]
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#pragma once

#include "im6502_core.h"

#include "device_manager.h"

#include <cstdint>
#include <functional>
#include <utility>

// IM6502Core implementation ported from MAME's rp2a03_core_device (the CPU part of the
// Ricoh RP2A03/RP2A03G, mame0289, BSD-3-Clause; see THIRD-PARTY-NOTICES.md): an NMOS 6502
// whose ADC/SBC ignore the D flag (the flag is still stored and pushed), with all of the
// 6502's undocumented opcodes. The sound generator on the same die is NOT part of this class.
//
// MAME builds the instruction dispatcher at its build time: m6502make.py turns the .lst
// files into a cycle-by-cycle state machine that can stop after any bus access. Here every
// instruction runs to completion in one call; the instruction bodies (the .inc files next to
// this header) keep the .lst sequences and their order of bus accesses, one cycle each.
//
// The original executes a KIL opcode by reading 0xFFFF forever; here the CPU is marked
// jammed and later Execute() calls just consume their budget.
class MameDerivedRp2a03Adapter : public IM6502Core, public IDevice {
public:
    MameDerivedRp2a03Adapter();

    void SetMemoryCallbacks(MemoryReadFn read, MemoryWriteFn write) override;
    void Reset() override;
    std::uint64_t Execute(std::uint64_t cycleBudget) override;
    void SetInputLine(M6502InputLine line, M6502InputLineState state) override;

    // Cycles consumed so far by the Execute() call in progress (0 outside one); lets a
    // Machine sync co-processors to the CPU's exact progress from inside a bus callback.
    std::uint64_t CyclesConsumedThisCall() const { return executing_ ? cyclesConsumedThisCall_ : 0; }

    // Register snapshot for tests and diagnostics only (not part of IM6502Core).
    struct RegisterSnapshot {
        std::uint16_t pc = 0;  // address of the next instruction (its opcode is already fetched)
        std::uint8_t a = 0, x = 0, y = 0, p = 0, s = 0, ir = 0;
        bool nmiLine = false, irqLine = false, apuIrqLine = false;
        bool nmiPending = false;
        bool jammed = false;
        std::uint64_t nmiServiceCount = 0;
        std::uint64_t irqServiceCount = 0;
    };
    RegisterSnapshot Snapshot() const;

    // Diagnostic-only observer (zero cost when unset), same precedent as the other CPU adapters.
    // Called before each instruction with its address and opcode.
    using OpcodeFetchObserver = std::function<void(std::uint16_t pc, std::uint8_t opcode)>;
    void SetOpcodeFetchObserverForTest(OpcodeFetchObserver observer) { opcodeFetchObserver_ = std::move(observer); }

private:
    using OpFn = void (MameDerivedRp2a03Adapter::*)();

    static constexpr std::uint8_t kFlagN = 0x80;
    static constexpr std::uint8_t kFlagV = 0x40;
    static constexpr std::uint8_t kFlagE = 0x20;  // reads as 1 (unused bit)
    static constexpr std::uint8_t kFlagB = 0x10;
    static constexpr std::uint8_t kFlagD = 0x08;
    static constexpr std::uint8_t kFlagI = 0x04;
    static constexpr std::uint8_t kFlagZ = 0x02;
    static constexpr std::uint8_t kFlagC = 0x01;

    // One cycle per bus access.
    std::uint8_t Read(int address);
    std::uint8_t ReadPc() { return Read(pc_); }
    std::uint8_t ReadArg(int address) { return Read(address); }
    void Write(int address, int value);
    void Prefetch();  // the opcode fetch that ends an instruction; decides whether an interrupt is taken

    static bool PageChanging(int base, int delta) { return (((base + delta) ^ base) & 0xff00) != 0; }
    static std::uint16_t SetL(int base, int val) { return static_cast<std::uint16_t>((base & 0xff00) | (val & 0xff)); }
    static std::uint16_t SetH(int base, int val) { return static_cast<std::uint16_t>((base & 0x00ff) | ((val & 0xff) << 8)); }
    void DecSp() { sp_ = SetL(sp_, sp_ - 1); }
    void IncSp() { sp_ = SetL(sp_, sp_ + 1); }

    void SetNz(std::uint8_t v);
    void DoAdcNd(std::uint8_t val);
    void DoSbcNd(std::uint8_t val);
    void DoArrNd();
    void DoCmp(std::uint8_t val1, std::uint8_t val2);
    void DoBit(std::uint8_t val);
    std::uint8_t DoAsl(std::uint8_t v);
    std::uint8_t DoLsr(std::uint8_t v);
    std::uint8_t DoRor(std::uint8_t v);
    std::uint8_t DoRol(std::uint8_t v);

    // One member per distinct instruction of the RP2A03 dispatch table, and the table itself.
#include "mame_derived_rp2a03_op_decls.inc"
    static const OpFn kOpTable[256];

    // ---- CPU registers (MAME's m_PC, m_A, ...) ----
    std::uint16_t pc_ = 0;       // program counter
    std::uint16_t npc_ = 0;      // address of the instruction whose opcode is in ir_
    std::uint16_t sp_ = 0x0100;  // stack pointer; the high byte stays 0x01
    std::uint16_t tmp_ = 0;
    std::uint8_t tmp2_ = 0;
    std::uint8_t a_ = 0, x_ = 0x80, y_ = 0, p_ = 0x36;
    std::uint8_t ir_ = 0;        // prefetched opcode

    bool nmiState_ = false, irqState_ = false, apuIrqState_ = false, vState_ = false;
    bool nmiPending_ = false, irqTaken_ = false;
    bool jammed_ = false;

    MemoryReadFn memoryRead_;
    MemoryWriteFn memoryWrite_;
    OpcodeFetchObserver opcodeFetchObserver_;

    bool executing_ = false;
    std::uint64_t cyclesConsumedThisCall_ = 0;
    std::uint64_t pendingResetCycles_ = 0;
    std::uint64_t totalCycles_ = 0;
    std::uint64_t nmiServiceCount_ = 0;
    std::uint64_t irqServiceCount_ = 0;
};
