// license:BSD-3-Clause
// copyright-holders:Dan Boris, Mirko Buffoni, Aaron Giles, Couriersud
// The original files also state: "Copyright Mirko Buffoni" and "Based on the original work Copyright Dan Boris".
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/cpu/mcs48/mcs48.cpp
//   src/devices/cpu/mcs48/mcs48.h
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#pragma once

#include "i8039_core.h"

#include "device_manager.h"

#include <array>
#include <cstdint>
#include <functional>
#include <utility>

// I8039Core implementation whose instruction/interrupt/timer semantics are ported from
// MAME's mcs48_cpu_device (mcs48.cpp/.h, mame0289, BSD-3-Clause; see
// THIRD-PARTY-NOTICES.md), scoped to the i8039 configuration: external ROM only, 128 bytes
// internal RAM, memory-bank-select and external-bus instructions, no UPI-41 DBB/STS, no
// i8021/i8022 special opcodes (it uses the plain s_mcs48_opcodes[256] table).
//
// Method names/grouping mirror mcs48.h's per-opcode method list (in PascalCase), like the
// other MAME-derived adapters. No MAME type appears in this header or the .cpp; the MAME
// device-framework coupling is replaced by I8039Core's callbacks.
//
// Also implements IDevice so a Machine's DeviceManager owns it uniformly.
class MameDerivedI8039Adapter : public I8039Core, public IDevice {
public:
    MameDerivedI8039Adapter();

    void SetProgramMemoryCallback(ProgramReadFn read) override;
    void SetBusCallbacks(BusReadFn read, BusWriteFn write) override;
    void SetPort1Callbacks(PortReadFn read, PortWriteFn write) override;
    void SetPort2Callbacks(PortReadFn read, PortWriteFn write) override;
    void SetTestInputCallbacks(TestInputFn t0, TestInputFn t1) override;
    void Reset() override;
    std::uint64_t Execute(std::uint64_t clockBudget) override;
    void SetInputLine(I8039InputLine line, I8039InputLineState state) override;

    // Input clocks consumed so far within the Execute() call in progress (0 once it
    // returns). Lets a port-write callback fired from inside Execute() learn where within
    // the call (and so the Render() block) the write happened.
    std::uint64_t CyclesConsumedThisCall() const { return clockDebt_; }

    // Register snapshot for synthetic tests only - not part of I8039Core.
    struct RegisterSnapshot {
        std::uint16_t pc = 0;    // 12-bit; the bank bit is part of it once set by JMP/CALL
        std::uint8_t a = 0;
        std::uint8_t psw = 0;    // C(0x80)/AC(0x40)/F0(0x20)/BS(0x10)/SP(bits 0-2)
        bool f1 = false;
        std::uint8_t p1 = 0, p2 = 0;
        std::uint8_t timer = 0;
        bool irqInProgress = false;
        std::uint64_t irqServiceCount = 0;       // external IRQ (call to 0x003)
        std::uint64_t timerIrqServiceCount = 0;  // timer overflow IRQ (call to 0x007)
    };
    RegisterSnapshot Snapshot() const;

    // Test-only hooks.
    using OpcodeFetchObserver = std::function<void(std::uint16_t pc, std::uint8_t opcode)>;
    void SetOpcodeFetchObserverForTest(OpcodeFetchObserver observer) { opcodeFetchObserver_ = std::move(observer); }

    using BusWriteObserver = std::function<void(std::uint8_t address, std::uint8_t value)>;
    void SetBusWriteObserverForTest(BusWriteObserver observer) { busWriteObserver_ = std::move(observer); }

    // Direct RAM peek for tests (no side effects).
    std::uint8_t RamPeekForTest(std::uint8_t address) const { return ram_[address & 0x7F]; }

private:
    // Internal RAM (128 bytes), owned directly. MOVX @R0/@R1 target the separate external
    // bus via BusReadFn/BusWriteFn, not this RAM.
    std::array<std::uint8_t, 128> ram_{};

    std::uint16_t pc_ = 0;      // full 12-bit PC (0-0xFFF), matches mcs48's own m_pc
    std::uint16_t prevPc_ = 0;  // PC at the start of the current/last instruction (for the
                                 // "JNI forced-taken" IRQ hack in CheckIrqs())
    std::uint16_t a11_ = 0;  // SEL MB0/MB1 flip-flop (0x000 or 0x800); only used when a
                              // JMP/CALL computes its target, not re-applied on every fetch
    std::uint8_t a_ = 0;
    std::uint8_t psw_ = 0;
    bool f1_ = false;
    std::uint8_t p1_ = 0xFF, p2_ = 0xFF;
    std::uint8_t* regptr_ = nullptr;  // points into ram_ at offset 0 or 24 (bank select)

    std::uint8_t timer_ = 0;
    std::uint8_t prescaler_ = 0;  // 5-bit
    std::uint8_t t1History_ = 0;
    bool timerOverflow_ = false;
    bool timerFlag_ = false;
    bool tirqEnabled_ = false;
    bool xirqEnabled_ = false;
    std::uint8_t timeCountEnabled_ = 0;  // bit0=timer, bit1=counter

    bool irqState_ = false;   // external IRQ line level
    bool irqPolled_ = false;  // last instruction was JNI (not taken)
    bool irqInProgress_ = false;

    std::uint64_t irqServiceCount_ = 0;
    std::uint64_t timerIrqServiceCount_ = 0;

    ProgramReadFn programRead_;
    BusReadFn busRead_;
    BusWriteFn busWrite_;
    PortReadFn port1Read_, port2Read_;
    PortWriteFn port1Write_, port2Write_;
    TestInputFn t0Read_, t1Read_;

    OpcodeFetchObserver opcodeFetchObserver_;
    BusWriteObserver busWriteObserver_;

    std::uint64_t clockDebt_ = 0;  // input clocks already budgeted for the in-progress Execute() call

    void UpdateRegPtr();
    std::uint8_t OpcodeFetch();
    std::uint8_t ArgumentFetch();
    std::uint8_t RamRead(std::uint8_t address) const { return ram_[address & 0x7F]; }
    void RamWrite(std::uint8_t address, std::uint8_t value) { ram_[address & 0x7F] = value; }
    std::uint8_t BusRead(std::uint8_t address) const;
    void BusWrite(std::uint8_t address, std::uint8_t value);
    void Port1Write(std::uint8_t value);
    void Port2Write(std::uint8_t value);
    std::uint8_t Port1Read() const;
    std::uint8_t Port2Read() const;
    bool TestRead(int which) const;

    void PushPcPsw();
    void PullPcPsw();
    void PullPc();
    void ExecuteAdd(std::uint8_t data);
    void ExecuteAddc(std::uint8_t data);
    void ExecuteJmp(std::uint16_t address);
    void ExecuteCall(std::uint16_t address);
    void ExecuteJcc(bool result);
    void ExpanderOperation(int operation, std::uint8_t port);

    void CheckIrqs();
    void BurnMachineCycles(int count);  // 1 machine cycle = 15 input clocks

    void RunOneInstruction();

    using OpHandler = void (MameDerivedI8039Adapter::*)();
    static const OpHandler kOpcodeTable[256];

    // R0-R7 within the currently-selected register bank.
    std::uint8_t& Reg(int index) { return regptr_[index]; }

    void Illegal();
    void Nop();
    void OutlBusA();
    void AddAN();
    void Jmp0(); void Jmp1(); void Jmp2(); void Jmp3(); void Jmp4(); void Jmp5(); void Jmp6(); void Jmp7();
    void EnI();
    void DecA();
    void InsABus();
    void InAP1();
    void InAP2();
    void MovdAP4(); void MovdAP5(); void MovdAP6(); void MovdAP7();
    void IncXr0(); void IncXr1();
    void Jb0(); void Jb1(); void Jb2(); void Jb3(); void Jb4(); void Jb5(); void Jb6(); void Jb7();
    void AdcAN();
    void Call0(); void Call1(); void Call2(); void Call3(); void Call4(); void Call5(); void Call6(); void Call7();
    void DisI();
    void Jtf();
    void IncA();
    void IncR0(); void IncR1(); void IncR2(); void IncR3(); void IncR4(); void IncR5(); void IncR6(); void IncR7();
    void XchAXr0(); void XchAXr1();
    void MovAN();
    void EnTcnti();
    void Jnt0();
    void ClrA();
    void XchAR0(); void XchAR1(); void XchAR2(); void XchAR3(); void XchAR4(); void XchAR5(); void XchAR6(); void XchAR7();
    void XchdAXr0(); void XchdAXr1();
    void DisTcnti();
    void Jt0();
    void CplA();
    void OutlP1A();
    void OutlP2A();
    void MovdP4A(); void MovdP5A(); void MovdP6A(); void MovdP7A();
    void OrlAXr0(); void OrlAXr1();
    void MovAT();
    void OrlAN();
    void StrtCnt();
    void Jnt1();
    void SwapA();
    void OrlAR0(); void OrlAR1(); void OrlAR2(); void OrlAR3(); void OrlAR4(); void OrlAR5(); void OrlAR6(); void OrlAR7();
    void AnlAXr0(); void AnlAXr1();
    void AnlAN();
    void StrtT();
    void Jt1();
    void DaA();
    void AnlAR0(); void AnlAR1(); void AnlAR2(); void AnlAR3(); void AnlAR4(); void AnlAR5(); void AnlAR6(); void AnlAR7();
    void AddAXr0(); void AddAXr1();
    void MovTA();
    void StopTcnt();
    void RrcA();
    void AddAR0(); void AddAR1(); void AddAR2(); void AddAR3(); void AddAR4(); void AddAR5(); void AddAR6(); void AddAR7();
    void AdcAXr0(); void AdcAXr1();
    void Ent0Clk();
    void Jf1();
    void RrA();
    void AdcAR0(); void AdcAR1(); void AdcAR2(); void AdcAR3(); void AdcAR4(); void AdcAR5(); void AdcAR6(); void AdcAR7();
    void MovxAXr0(); void MovxAXr1();
    void Ret();
    void ClrF0();
    void Jni();
    void OrlBusN();
    void OrlP1N();
    void OrlP2N();
    void OrldP4A(); void OrldP5A(); void OrldP6A(); void OrldP7A();
    void MovxXr0A(); void MovxXr1A();
    void Retr();
    void CplF0();
    void Jnz();
    void ClrC();
    void AnlBusN();
    void AnlP1N();
    void AnlP2N();
    void AnldP4A(); void AnldP5A(); void AnldP6A(); void AnldP7A();
    void MovXr0A(); void MovXr1A();
    void MovpAXa();
    void ClrF1();
    void CplC();
    void MovR0A(); void MovR1A(); void MovR2A(); void MovR3A(); void MovR4A(); void MovR5A(); void MovR6A(); void MovR7A();
    void MovXr0N(); void MovXr1N();
    void JmppXa();
    void CplF1();
    void Jf0();
    void MovR0N(); void MovR1N(); void MovR2N(); void MovR3N(); void MovR4N(); void MovR5N(); void MovR6N(); void MovR7N();
    void SelRb0();
    void Jz();
    void MovAPsw();
    void DecR0(); void DecR1(); void DecR2(); void DecR3(); void DecR4(); void DecR5(); void DecR6(); void DecR7();
    void XrlAXr0(); void XrlAXr1();
    void XrlAN();
    void SelRb1();
    void MovPswA();
    void XrlAR0(); void XrlAR1(); void XrlAR2(); void XrlAR3(); void XrlAR4(); void XrlAR5(); void XrlAR6(); void XrlAR7();
    void Movp3AXa();
    void SelMb0();
    void Jnc();
    void RlA();
    void DjnzR0(); void DjnzR1(); void DjnzR2(); void DjnzR3(); void DjnzR4(); void DjnzR5(); void DjnzR6(); void DjnzR7();
    void MovAXr0(); void MovAXr1();
    void SelMb1();
    void Jc();
    void RlcA();
    void MovAR0(); void MovAR1(); void MovAR2(); void MovAR3(); void MovAR4(); void MovAR5(); void MovAR6(); void MovAR7();
};
