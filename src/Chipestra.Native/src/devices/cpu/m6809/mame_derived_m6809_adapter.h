// license:BSD-3-Clause
// copyright-holders:Nathan Woods
// copyright-holders:Olivier Galibert
// The original files also state: "Copyright John Butler".
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/cpu/m6809/m6809.cpp  [copyright-holders:Nathan Woods]
//   src/devices/cpu/m6809/m6809.h  [copyright-holders:Nathan Woods]
//   src/devices/cpu/m6809/m6809inl.h  [copyright-holders:Nathan Woods]
//   src/devices/cpu/m6809/m6809.lst  [copyright-holders:Nathan Woods]
//   src/devices/cpu/m6809/m6809make.py  [copyright-holders:Olivier Galibert]
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#pragma once

#include "im6809_core.h"

#include "device_manager.h"

#include <cstdint>
#include <functional>
#include <utility>

// IM6809Core implementation ported from MAME's m6809_base_device (m6809.cpp/.h/m6809inl.h,
// mame0289, BSD-3-Clause; see THIRD-PARTY-NOTICES.md). MC6809E only (clock divider 1, LIC
// pin unused); the plain MC6809 and the HD6309 native-mode extensions are out of scope.
//
// MAME's execute_one() is a goto/label state machine generated at MAME's build time from
// m6809.lst by m6809make.py (the generated m6809.hxx is not checked in). This port ran the
// script against m6809.lst to obtain the generated dispatcher and transcribed its logic
// into ordinary C++ methods.
//
// The `push_state(N); goto MODE;` pairs in the generated code exist only so MAME's
// scheduler can suspend mid-instruction; here every instruction runs to completion in one
// call, so e.g. `push_state(19); goto DIRECT;  // SUB8` becomes `ModeDirect(); OpSub8();`.
// This is a mechanical transform: register/flag logic and per-access cycle counts are kept
// as generated, in the same order.
//
// Removed on purpose: the dead NEG16/COM16/... label group (only reachable from HD6309
// native-mode 16-bit RMW opcodes, never targeted by the base-6809 dispatch tables); the
// debugger/save-state hooks; and the unused vector-read/sync-ack device callbacks.
class MameDerivedM6809Adapter : public IM6809Core, public IDevice {
public:
    MameDerivedM6809Adapter();

    void SetMemoryCallbacks(MemoryReadFn read, MemoryWriteFn write) override;
    void Reset() override;
    std::uint64_t Execute(std::uint64_t cycleBudget) override;
    void SetInputLine(M6809InputLine line, M6809InputLineState state) override;

    // Cycles consumed so far by the Execute() call in progress (0 outside one); lets a
    // Machine sync co-processors to the CPU's exact progress from inside a bus callback.
    std::uint64_t CyclesConsumedThisCall() const { return executing_ ? cyclesConsumedThisCall_ : 0; }

    // Register snapshot for tests and diagnostics only (not part of IM6809Core).
    struct RegisterSnapshot {
        std::uint16_t pc = 0;
        std::uint16_t s = 0, u = 0, x = 0, y = 0;
        std::uint8_t a = 0, b = 0, dp = 0, cc = 0;
        bool nmiLine = false, firqLine = false, irqLine = false;
        bool ldsEncountered = false;
        bool syncWaiting = false, cwaiWaiting = false;
        std::uint64_t nmiServiceCount = 0;
        std::uint64_t firqServiceCount = 0;
        std::uint64_t irqServiceCount = 0;
    };
    RegisterSnapshot Snapshot() const;

    // Diagnostic-only observers (zero cost when unset), same precedent as
    // MameDerivedZ80Adapter/MameDerivedH6280Adapter.
    using OpcodeFetchObserver = std::function<void(std::uint16_t pc, std::uint8_t opcode)>;
    void SetOpcodeFetchObserverForTest(OpcodeFetchObserver observer) { opcodeFetchObserver_ = std::move(observer); }

private:
    // CC (condition code) bits - MAME's own CC_* names/values (m6809.h).
    static constexpr std::uint8_t CC_C = 0x01;   // Carry
    static constexpr std::uint8_t CC_V = 0x02;   // Overflow
    static constexpr std::uint8_t CC_Z = 0x04;   // Zero
    static constexpr std::uint8_t CC_N = 0x08;   // Negative
    static constexpr std::uint8_t CC_I = 0x10;   // Inhibit IRQ
    static constexpr std::uint8_t CC_H = 0x20;   // Half carry
    static constexpr std::uint8_t CC_F = 0x40;   // Inhibit FIRQ
    static constexpr std::uint8_t CC_E = 0x80;   // Entire state pushed
    static constexpr std::uint8_t CC_NZ = CC_N | CC_Z;
    static constexpr std::uint8_t CC_NZV = CC_N | CC_Z | CC_V;
    static constexpr std::uint8_t CC_NZVC = CC_N | CC_Z | CC_V | CC_C;
    static constexpr std::uint8_t CC_HNZVC = CC_H | CC_N | CC_Z | CC_V | CC_C;

    // Interrupt/reset vectors (m6809.h).
    static constexpr std::uint16_t VECTOR_SWI3 = 0xFFF2;
    static constexpr std::uint16_t VECTOR_SWI2 = 0xFFF4;
    static constexpr std::uint16_t VECTOR_FIRQ = 0xFFF6;
    static constexpr std::uint16_t VECTOR_IRQ = 0xFFF8;
    static constexpr std::uint16_t VECTOR_SWI = 0xFFFA;
    static constexpr std::uint16_t VECTOR_NMI = 0xFFFC;
    static constexpr std::uint16_t VECTOR_RESET = 0xFFFE;

    enum class AddrMode { Immediate, Ea, RegA, RegB };
    enum class Reg8Id { None, A, B };
    enum class Reg16Id { None, X, Y, U, S, D };

    // ---- bus access (m6809inl.h eat()/read_memory()/write_memory()/... ) ----
    void Eat(int cycles) { cyclesConsumedThisCall_ += static_cast<std::uint64_t>(cycles); }
    std::uint8_t ReadMemory(std::uint16_t address);
    void WriteMemory(std::uint16_t address, std::uint8_t value);
    std::uint8_t ReadOpcode();            // fetch at pc_, bumps pc_
    std::uint8_t ReadOpcodeArg();         // fetch at pc_, bumps pc_ (MAME distinguishes this from
                                           // ReadOpcode() only for decrypted-opcode memory maps -
                                           // Chipestra has none, both route to the same callback)
    std::uint8_t ReadOpcodeArgAt(std::uint16_t address);  // explicit-address form, does NOT bump pc_
    void DummyReadOpcodeArg(std::uint16_t delta) { ReadOpcodeArgAt(static_cast<std::uint16_t>(pc_ + delta)); }
    void DummyVma(int count) {
        for (int i = 0; i != count; ++i) ReadOpcodeArgAt(0xFFFF);
    }
    std::uint8_t ReadVector(int ordinal) { return ReadMemory(static_cast<std::uint16_t>(ea_ + ordinal)); }

    // ---- operand access through the current addressing mode (m6809inl.h) ----
    std::uint8_t ReadOperand();
    std::uint8_t ReadOperand(int ordinal);
    void WriteOperand(std::uint8_t data);
    void WriteOperand(int ordinal, std::uint8_t data);
    void SetEa(std::uint16_t ea) { ea_ = ea; mode_ = AddrMode::Ea; }
    void SetEaHi(std::uint8_t hi) { ea_ = static_cast<std::uint16_t>((ea_ & 0x00FF) | (hi << 8)); }
    void SetEaLo(std::uint8_t lo) {
        ea_ = static_cast<std::uint16_t>((ea_ & 0xFF00) | lo);
        mode_ = AddrMode::Ea;
    }
    void SetImm() { mode_ = AddrMode::Immediate; }
    void SetA() { mode_ = AddrMode::RegA; }
    void SetB() { mode_ = AddrMode::RegB; }
    bool IsRegisterAddressingMode() const { return mode_ != AddrMode::Immediate && mode_ != AddrMode::Ea; }

    void SetRegOp8(Reg8Id r) { regOp8_ = r; }
    void SetRegOp16(Reg16Id r) { regOp16_ = r; }
    std::uint8_t RegOp8() const;
    void SetRegOp8Value(std::uint8_t v);
    std::uint16_t RegOp16() const;
    void SetRegOp16Value(std::uint16_t v);
    bool RegOp16IsS() const { return regOp16_ == Reg16Id::S; }

    // ---- flags (m6809inl.h set_flags<T>, rotate_left/right, daa/mul) ----
    std::uint8_t SetFlags8(std::uint8_t mask, std::uint8_t a, std::uint8_t b, std::uint32_t r);
    std::uint8_t SetFlags8(std::uint8_t mask, std::uint8_t r) { return SetFlags8(mask, 0, r, r); }
    std::uint16_t SetFlags16(std::uint8_t mask, std::uint16_t a, std::uint16_t b, std::uint32_t r);
    std::uint16_t SetFlags16(std::uint8_t mask, std::uint16_t r) { return SetFlags16(mask, 0, r, r); }
    std::uint8_t RotateRight8(std::uint8_t v);
    std::uint16_t RotateLeft8(std::uint8_t v);  // MAME's rotate_left<T> returns uint32_t; only the low
                                                 // 9 bits (8 data + carry-out) are ever meaningful for T=uint8_t
    void Daa();
    void Mul();

    // ---- branch conditions (m6809inl.h cond_*) ----
    bool CondHi() const { return !(cc_ & (CC_Z | CC_C)); }
    bool CondCc() const { return !(cc_ & CC_C); }
    bool CondNe() const { return !(cc_ & CC_Z); }
    bool CondVc() const { return !(cc_ & CC_V); }
    bool CondPl() const { return !(cc_ & CC_N); }
    bool CondGe() const { return static_cast<bool>(cc_ & CC_N) == static_cast<bool>(cc_ & CC_V); }
    bool CondGt() const { return CondGe() && !(cc_ & CC_Z); }

    // ---- interrupt registers (m6809inl.h) ----
    std::uint16_t GetPendingInterrupt() const;
    std::uint16_t ReadTfrExg816Register(std::uint8_t reg) const;
    std::uint16_t ReadExg168Register(std::uint8_t reg) const;
    void WriteExgTfrRegister(std::uint8_t reg, std::uint16_t value);
    std::uint16_t& Ireg();  // m_x/m_y/m_u/m_s selected by (opcode_ & 0x60), for indexed addressing

    // ---- one instruction (execute_one(), transcribed as calls - see class comment) ----
    void DispatchPage0(std::uint8_t opcode);
    void DispatchPage10(std::uint8_t opcode);
    void DispatchPage11(std::uint8_t opcode);

    // addressing modes (DIRECT/INDEXED/EXTENDED labels)
    void ModeDirect();
    void ModeExtended();
    void ModeIndexed();

    // 8-bit read-modify-write group (NEG8/COM8/LSR8/ROR8/ASR8/ASL8/ROL8/DEC8/INC8/TST8/CLR8, plus the
    // undocumented XNC8/XDEC8/XCLR8 variants)
    void OpNeg8();
    void OpCom8();
    void OpLsr8();
    void OpRor8();
    void OpAsr8();
    void OpAsl8();
    void OpRol8();
    void OpDec8();
    void OpInc8();
    void OpTst8();
    void OpClr8();
    void OpXnc8();   // undocumented: COM8 if C set, else NEG8
    void OpXdec8();  // undocumented DEC8 variant (also sets C from the pre-decrement value)
    void OpXclr8();  // undocumented CLR8 variant (does not force-clear V like OpClr8 does via CC_NZVC)
    void OpJmp() { pc_ = ea_; }

    // 8-bit register-op group (SUB8/CMP8/SBC8/AND8/BIT8/LD8/ST8/EOR8/ADC8/OR8/ADD8, undocumented XST8)
    void OpSub8();
    void OpCmp8();
    void OpSbc8();
    void OpAnd8();
    void OpBit8();
    void OpLd8();
    void OpSt8();
    void OpEor8();
    void OpAdc8();
    void OpOr8();
    void OpAdd8();
    void OpXst8();  // undocumented "store immediate": reads the (discarded) operand, sets flags from
                     // the register - TODO note carried over from MAME (see .cpp)

    // 16-bit register-op group (SUB16/CMP16/ADD16/LD16/ST16, undocumented XADD16)
    void OpSub16();
    void OpCmp16();
    void OpAdd16();
    void OpLd16();
    void OpSt16();
    void OpXadd16();
    void OpXst16();  // undocumented 16-bit "store immediate" (see .cpp)

    // branches / subroutines / stack
    void OpBranch();
    void OpLBranch();
    void OpBsr();
    void OpLbsr();
    void OpJsr();
    void GotoSubroutine();
    void OpRts();
    void OpAbx();
    void OpMul();
    void OpRti();
    void OpSwi();
    void OpSwi2();
    void OpSwi3();
    void SoftwareInterrupt();
    void OpCwai();
    void OpSync();
    void OpLeaXy();
    void OpLeaUs();
    void OpPshs();
    void OpPuls();
    void OpPshu();
    void OpPulu();
    void PushRegisters();
    void PullRegisters();

    // miscellaneous
    void OpNop();
    void OpDaa();
    void OpOrcc();
    void OpAndcc();
    void OpSex();
    void OpExg();
    void OpTfr();
    void OpFreerun();
    void OpX18();      // undocumented CC-rotate opcode 0x18
    void OpXandcc();   // undocumented ANDCC alias (opcode 0x38)
    void OpXres();     // undocumented "software reset" (0x3E page 0)
    void OpXswi2();    // undocumented SWI2 alias (0x3E page 0x10)
    void OpXfirq();    // undocumented "force FIRQ" (0x3E page 0x11)
    void OpIllegal();

    // interrupt entry
    void ServiceNmi();
    void ServiceFirq();
    void ServiceIrq();
    void InterruptVectorFetch();
    std::uint16_t PartialStateRegisters() const { return 0x81; }
    std::uint16_t EntireStateRegisters() const { return 0xFF; }

    // ---- CPU registers ----
    std::uint16_t pc_ = 0, ppc_ = 0;
    std::uint16_t s_ = 0, u_ = 0, x_ = 0, y_ = 0;
    std::uint8_t a_ = 0, b_ = 0;
    std::uint8_t dp_ = 0;
    std::uint8_t cc_ = 0;
    std::uint16_t ea_ = 0;
    std::uint16_t temp_ = 0;
    std::uint8_t opcode_ = 0;

    AddrMode mode_ = AddrMode::Immediate;
    Reg8Id regOp8_ = Reg8Id::None;
    Reg16Id regOp16_ = Reg16Id::None;

    bool nmiLine_ = false;
    bool nmiAsserted_ = false;
    bool firqLine_ = false;
    bool irqLine_ = false;
    bool ldsEncountered_ = false;
    bool cond_ = false;
    bool freeRun_ = false;
    bool syncWaiting_ = false;
    bool cwaiWaiting_ = false;

    MemoryReadFn memoryRead_;
    MemoryWriteFn memoryWrite_;
    OpcodeFetchObserver opcodeFetchObserver_;

    bool executing_ = false;
    std::uint64_t cyclesConsumedThisCall_ = 0;
    std::uint64_t nmiServiceCount_ = 0;
    std::uint64_t firqServiceCount_ = 0;
    std::uint64_t irqServiceCount_ = 0;
};
