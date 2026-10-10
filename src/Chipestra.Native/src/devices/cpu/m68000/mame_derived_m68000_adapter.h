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

#pragma once

#include "im68000_core.h"
#include "m68000_registers.h"

#include "device_manager.h"

#include <cstdint>
#include <functional>

// IM68000Core implementation. Unlike MameDerivedZ80Adapter, the *decoder* here is a
// Chipestra-authored addressing-mode-and-dispatch structure: MAME's equivalent
// (m68kops.cpp) is a huge generated file with one function per (opcode, addressing mode)
// pair, which does not lend itself to a direct port. What is ported is the semantics:
// flag computation, exception/interrupt stack-frame layout and effective-address rules,
// cross-checked against Musashi's hand-written core (m68kcpu.h/m68kcpu.cpp, mame0289,
// BSD-3-Clause; see THIRD-PARTY-NOTICES.md). No MAME type appears in this header or in
// mame_derived_m68000_adapter.cpp.
//
// 68020+-only instructions (CAS/CAS2) are not implemented; they trap as illegal
// instructions, as do line-A and line-F opcodes (0xA000-0xAFFF/0xF000-0xFFFF), neither
// meaningful on a bare 68000.
//
// Also implements IDevice so a Machine's DeviceManager can own it uniformly.
class MameDerivedM68000Adapter : public IM68000Core, public IDevice {
public:
    MameDerivedM68000Adapter();

    void SetMemoryCallbacks(MemoryReadFn read, MemoryWriteFn write) override;
    void SetAutovectorAcknowledgeCallback(AutovectorAckFn callback) override;
    void Reset() override;
    std::uint64_t Execute(std::uint64_t cycleBudget) override;
    void SetInterruptLevel(std::uint8_t level) override;

    // Register snapshot for synthetic tests only - not part of IM68000Core.
    struct RegisterSnapshot {
        std::uint32_t d[8] = {};
        std::uint32_t a[8] = {};
        std::uint32_t usp = 0;
        std::uint32_t ssp = 0;
        std::uint32_t pc = 0;
        std::uint16_t sr = 0;
        bool halted = false;  // STOP instruction, or a double bus fault equivalent
        std::uint64_t interruptServiceCount = 0;
        std::uint64_t illegalInstructionCount = 0;
    };
    RegisterSnapshot Snapshot() const;

    // Cycles consumed so far by the in-progress Execute() call; lets a Machine sync
    // co-processors mid-block.
    std::uint64_t CyclesConsumedThisCall() const { return cyclesConsumedThisCall_; }

    // Test/diagnostic hook: invoked for every opcode-word fetch with the PC it was read
    // from and the word itself. Null by default.
    using OpcodeFetchObserver = std::function<void(std::uint32_t pc, std::uint16_t opcode)>;
    void SetOpcodeFetchObserverForTest(OpcodeFetchObserver observer) { opcodeFetchObserver_ = std::move(observer); }

    // Test/diagnostic hook: invoked for every CPU memory write (byte granularity) with the
    // target address and byte. Null by default.
    using MemoryWriteObserver = std::function<void(std::uint32_t address, std::uint8_t value)>;
    void SetMemoryWriteObserverForTest(MemoryWriteObserver observer) { memoryWriteObserver_ = std::move(observer); }

    // Test/diagnostic hook: invoked from RaiseIllegalInstruction() with the address of the
    // offending opcode and the opcode word (before PC is redirected). Null by default.
    using IllegalInstructionObserver = std::function<void(std::uint32_t pc, std::uint16_t opcode)>;
    void SetIllegalInstructionObserverForTest(IllegalInstructionObserver observer) {
        illegalInstructionObserver_ = std::move(observer);
    }

private:
    // One decoded effective address: resolved once, then read/written through the handle,
    // because some addressing modes have side effects ((An)+/-(An)) that must happen
    // exactly once even when an instruction both reads and writes the operand.
    enum class OperandKind { DataReg, AddrReg, Memory, Immediate };
    struct Operand {
        OperandKind kind = OperandKind::DataReg;
        int reg = 0;               // DataReg/AddrReg: register index 0-7
        std::uint32_t address = 0;  // Memory: the resolved effective address
        std::uint32_t immediate = 0;  // Immediate: the fetched value itself
    };

    M68000Registers regs_;
    bool halted_ = false;
    std::uint8_t interruptLevel_ = 0;

    std::uint64_t interruptServiceCount_ = 0;
    std::uint64_t illegalInstructionCount_ = 0;

    MemoryReadFn memoryRead_;
    MemoryWriteFn memoryWrite_;
    AutovectorAckFn autovectorAck_;
    OpcodeFetchObserver opcodeFetchObserver_;
    MemoryWriteObserver memoryWriteObserver_;
    IllegalInstructionObserver illegalInstructionObserver_;
    // The opcode word most recently returned by FetchOpcodeWord(); RaiseIllegalInstruction()
    // has no opcode parameter, so it is mirrored here for illegalInstructionObserver_.
    std::uint16_t lastFetchedOpcodeForTest_ = 0;

    std::uint64_t cyclesConsumedThisCall_ = 0;
    std::uint64_t cyclesRemainingThisExecute_ = 0;

    // --- bus access -------------------------------------------------------
    std::uint8_t Read8(std::uint32_t address) const;
    std::uint16_t Read16(std::uint32_t address) const;
    std::uint32_t Read32(std::uint32_t address) const;
    void Write8(std::uint32_t address, std::uint8_t value) const;
    void Write16(std::uint32_t address, std::uint16_t value) const;
    void Write32(std::uint32_t address, std::uint32_t value) const;
    std::uint16_t FetchOpcodeWord();  // reads PC, advances PC by 2, fires the opcode-fetch observer
    std::uint16_t FetchExtensionWord();  // reads PC, advances PC by 2 (no observer call)
    std::uint32_t FetchExtensionLong();  // reads PC, advances PC by 4

    void Push16(std::uint16_t value);
    void Push32(std::uint32_t value);
    std::uint16_t Pop16();
    std::uint32_t Pop32();

    // --- register file access (size-aware; byte/word writes to Dn preserve the upper bits) ---
    std::uint32_t GetDataReg(int n, int size) const;
    void SetDataReg(int n, int size, std::uint32_t value);
    std::uint32_t GetAddrReg(int n) const { return regs_.a[n & 7]; }
    void SetAddrReg(int n, std::uint32_t value) { regs_.a[n & 7] = value; }

    // --- SR/CCR (handles the USP<->SSP swap on an S-bit change) -----------
    std::uint16_t GetSr() const { return regs_.sr.ToWord(); }
    void SetSr(std::uint16_t value);  // full SR (privileged - caller checks supervisor mode)
    void SetCcr(std::uint16_t value) { regs_.sr.FromCcr(value); }

    // --- addressing modes ---------------------------------------------------
    // `size` is 1/2/4 (bytes). Advances PC past any extension word(s) the
    // mode needs and performs (An)+/-(An) side effects exactly once.
    Operand ResolveEa(std::uint8_t mode, std::uint8_t reg, int size);
    std::uint32_t ReadOperand(const Operand& op, int size) const;
    void WriteOperand(const Operand& op, int size, std::uint32_t value);
    std::uint32_t EffectiveAddressForOperand(const Operand& op) const;  // Memory-kind only; used by LEA/PEA/JMP/JSR
    // Approximate extra bus cycles an addressing mode costs beyond an instruction's base
    // cost (standard published 68000 timings, not MAME's bus-contention-precise model).
    int EaExtraCycles(std::uint8_t mode, std::uint8_t reg, int size) const;

    // --- condition codes / exceptions ---------------------------------------
    bool TestCondition(int cc) const;  // 0..15: T,F,HI,LS,CC,CS,NE,EQ,VC,VS,PL,MI,GE,LT,GT,LE
    void TakeException(std::uint8_t vector, bool isInterruptAutovector = false, std::uint8_t autovectorLevel = 0);
    void CheckPendingInterrupt();
    void RaiseIllegalInstruction();

    // --- packed-BCD correction (ABCD/SBCD/NBCD) ---
    // Per-nibble decimal add/subtract with the carry/borrow propagated between the low and
    // high digits (the standard packed-BCD algorithm).
    std::uint8_t BcdAdd(std::uint8_t dst, std::uint8_t src, bool extend, bool& carryOut) const;
    std::uint8_t BcdSub(std::uint8_t dst, std::uint8_t src, bool extend, bool& carryOut) const;
    // Applies the shared ABCD/SBCD/NBCD flag rule to `result`: N from the sign bit, X=C=`carry`,
    // V cleared (undefined per Motorola; a fixed value keeps playback deterministic), Z
    // cleared only if the result is nonzero and otherwise unchanged (sticky across a
    // multi-byte chain, like ADDX/SUBX).
    void ApplyBcdFlags(std::uint8_t result, bool carry);

    // --- decode/execute -------------------------------------------------------
    // Each Group* handler returns the instruction's cycle cost (added to
    // cyclesConsumedThisCall_ by ExecuteOneInstruction/Execute).
    void ExecuteOneInstruction();
    int Group0(std::uint16_t op);   // 0x0xxx: immediate/bit ops, MOVEP
    int Group1(std::uint16_t op);   // 0x1xxx: MOVE.B
    int Group2(std::uint16_t op);   // 0x2xxx: MOVE.L / MOVEA.L
    int Group3(std::uint16_t op);   // 0x3xxx: MOVE.W / MOVEA.W
    int Group4(std::uint16_t op);   // 0x4xxx: miscellaneous
    int Group5(std::uint16_t op);   // 0x5xxx: ADDQ/SUBQ, Scc, DBcc
    int Group6(std::uint16_t op);   // 0x6xxx: Bcc/BRA/BSR
    int Group7(std::uint16_t op);   // 0x7xxx: MOVEQ
    int Group8(std::uint16_t op);   // 0x8xxx: OR, DIVU, DIVS
    int Group9(std::uint16_t op);   // 0x9xxx: SUB, SUBX, SUBA
    int GroupB(std::uint16_t op);   // 0xBxxx: CMP, CMPA, EOR, CMPM
    int GroupC(std::uint16_t op);   // 0xCxxx: AND, MULU, MULS, EXG
    int GroupD(std::uint16_t op);   // 0xDxxx: ADD, ADDX, ADDA
    int GroupE(std::uint16_t op);   // 0xExxx: shifts/rotates

    // Shared by Group1/2/3 (MOVE.B/.L/.W all share one encoding shape,
    // differing only in size).
    int ExecuteMove(std::uint16_t op, int size);
    // Shared by Group4's MOVEM ea,reglist / MOVEM reglist,ea forms.
    int ExecuteMovem(std::uint16_t op);
    // Shared by Group0/Group4's static (#imm bit number) and Group0's
    // dynamic (Dn bit number) BTST/BCHG/BCLR/BSET forms.
    int ExecuteBitOp(std::uint16_t op, int bitNumber, bool dynamicBitNumber);

    // --- ALU/flag helpers (N/Z/V/C/X computed explicitly per real-hardware rules) ---
    void SetNzForResult(std::uint32_t result, int size);  // sets N/Z only (logical ops)
    std::uint32_t OpAdd(std::uint32_t a, std::uint32_t b, int size, bool withExtend, bool useExtendCarryIn);
    std::uint32_t OpSub(std::uint32_t a, std::uint32_t b, int size, bool withExtend, bool useExtendCarryIn);
    void OpCmp(std::uint32_t a, std::uint32_t b, int size);
    std::uint32_t SignExtend(std::uint32_t value, int size) const;
    std::uint32_t MaskToSize(std::uint32_t value, int size) const;

    // One single-bit shift/rotate step. Looping single steps (rather than a closed-form
    // multi-bit shift) makes ASL's "V set if the sign bit changed on any step" rule come out
    // right. type: 0=AS,1=LS,2=ROX,3=RO. Sets C/X/N/Z/V as a side effect; returns the
    // shifted value.
    std::uint32_t ShiftStep(std::uint32_t value, int size, int type, bool left);
};
