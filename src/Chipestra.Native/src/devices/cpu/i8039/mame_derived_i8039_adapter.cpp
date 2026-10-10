// license:BSD-3-Clause
// copyright-holders:Dan Boris, Mirko Buffoni, Aaron Giles, Couriersud
// The original files also state: "Copyright Mirko Buffoni" and "Based on the original work Copyright Dan Boris".
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/cpu/mcs48/mcs48.cpp
//   src/devices/cpu/mcs48/mcs48.h
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#include "devices/cpu/i8039/mame_derived_i8039_adapter.h"

#include <utility>

namespace {
constexpr std::uint8_t kCFlag = 0x80;
constexpr std::uint8_t kAFlag = 0x40;  // auxiliary carry
constexpr std::uint8_t kFFlag = 0x20;  // user flag F0
constexpr std::uint8_t kBFlag = 0x10;  // register bank select

constexpr std::uint8_t kTimerEnabled = 0x01;
constexpr std::uint8_t kCounterEnabled = 0x02;

enum ExpanderOp { kExpanderRead = 0, kExpanderWrite = 1, kExpanderOr = 2, kExpanderAnd = 3 };
}  // namespace

MameDerivedI8039Adapter::MameDerivedI8039Adapter() { UpdateRegPtr(); }

void MameDerivedI8039Adapter::SetProgramMemoryCallback(ProgramReadFn read) { programRead_ = std::move(read); }

void MameDerivedI8039Adapter::SetBusCallbacks(BusReadFn read, BusWriteFn write) {
    busRead_ = std::move(read);
    busWrite_ = std::move(write);
}

void MameDerivedI8039Adapter::SetPort1Callbacks(PortReadFn read, PortWriteFn write) {
    port1Read_ = std::move(read);
    port1Write_ = std::move(write);
}

void MameDerivedI8039Adapter::SetPort2Callbacks(PortReadFn read, PortWriteFn write) {
    port2Read_ = std::move(read);
    port2Write_ = std::move(write);
}

void MameDerivedI8039Adapter::SetTestInputCallbacks(TestInputFn t0, TestInputFn t1) {
    t0Read_ = std::move(t0);
    t1Read_ = std::move(t1);
}

void MameDerivedI8039Adapter::Reset() {
    // mcs48_cpu_device::device_reset() (mame0289, THIRD-PARTY-NOTICES.md).
    pc_ = 0;
    psw_ = psw_ & (kCFlag | kAFlag);
    UpdateRegPtr();
    f1_ = false;
    a11_ = 0;

    tirqEnabled_ = false;
    xirqEnabled_ = false;
    timeCountEnabled_ = 0;
    timerFlag_ = false;

    irqInProgress_ = false;
    timerOverflow_ = false;
    irqPolled_ = false;

    // Port 1/Port 2 are set to input mode (all-ones latch).
    p1_ = 0xFF;
    p2_ = 0xFF;
    Port1Write(p1_);
    Port2Write(p2_);
}

void MameDerivedI8039Adapter::SetInputLine(I8039InputLine line, I8039InputLineState state) {
    if (line == I8039InputLine::Irq) {
        irqState_ = (state == I8039InputLineState::Assert);
    }
}

void MameDerivedI8039Adapter::UpdateRegPtr() { regptr_ = &ram_[(psw_ & kBFlag) ? 24 : 0]; }

std::uint8_t MameDerivedI8039Adapter::OpcodeFetch() {
    const std::uint16_t address = pc_;
    // 11-bit auto-increment; the bank bit (0x800) of pc_ is preserved (it is not
    // re-derived from a11_, which only matters at the next JMP/CALL).
    pc_ = static_cast<std::uint16_t>(((pc_ + 1) & 0x7FF) | (pc_ & 0x800));
    const std::uint8_t opcode = programRead_ ? programRead_(address) : 0;
    if (opcodeFetchObserver_) opcodeFetchObserver_(address, opcode);
    return opcode;
}

std::uint8_t MameDerivedI8039Adapter::ArgumentFetch() {
    const std::uint16_t address = pc_;
    pc_ = static_cast<std::uint16_t>(((pc_ + 1) & 0x7FF) | (pc_ & 0x800));
    return programRead_ ? programRead_(address) : 0;
}

std::uint8_t MameDerivedI8039Adapter::BusRead(std::uint8_t address) const {
    return busRead_ ? busRead_(address) : 0xFF;
}

void MameDerivedI8039Adapter::BusWrite(std::uint8_t address, std::uint8_t value) {
    if (busWrite_) busWrite_(address, value);
    if (busWriteObserver_) busWriteObserver_(address, value);
}

void MameDerivedI8039Adapter::Port1Write(std::uint8_t value) {
    if (port1Write_) port1Write_(value);
}
void MameDerivedI8039Adapter::Port2Write(std::uint8_t value) {
    if (port2Write_) port2Write_(value);
}
std::uint8_t MameDerivedI8039Adapter::Port1Read() const { return port1Read_ ? port1Read_() : 0xFF; }
std::uint8_t MameDerivedI8039Adapter::Port2Read() const { return port2Read_ ? port2Read_() : 0xFF; }

bool MameDerivedI8039Adapter::TestRead(int which) const {
    const TestInputFn& fn = (which == 0) ? t0Read_ : t1Read_;
    return fn ? fn() : true;
}

// push_pc_psw/pull_pc_psw/pull_pc (mcs48.cpp) - the 8-level*2-byte software
// stack lives in internal RAM at 8+2*sp/9+2*sp, sp is PSW's low 3 bits.
void MameDerivedI8039Adapter::PushPcPsw() {
    const std::uint8_t sp = psw_ & 0x07;
    RamWrite(static_cast<std::uint8_t>(8 + 2 * sp), static_cast<std::uint8_t>(pc_ & 0xFF));
    RamWrite(static_cast<std::uint8_t>(9 + 2 * sp),
              static_cast<std::uint8_t>(((pc_ >> 8) & 0x0F) | (psw_ & 0xF0)));
    psw_ = static_cast<std::uint8_t>((psw_ & 0xF0) | ((sp + 1) & 0x07));
}

void MameDerivedI8039Adapter::PullPcPsw() {
    const std::uint8_t sp = static_cast<std::uint8_t>((psw_ - 1) & 0x07);
    std::uint16_t pc = RamRead(static_cast<std::uint8_t>(8 + 2 * sp));
    pc |= static_cast<std::uint16_t>(RamRead(static_cast<std::uint8_t>(9 + 2 * sp))) << 8;
    psw_ = static_cast<std::uint8_t>(((pc >> 8) & 0xF0) | sp);
    pc_ = static_cast<std::uint16_t>(pc & (irqInProgress_ ? 0x7FF : 0xFFF));
    UpdateRegPtr();
}

void MameDerivedI8039Adapter::PullPc() {
    const std::uint8_t sp = static_cast<std::uint8_t>((psw_ - 1) & 0x07);
    std::uint16_t pc = RamRead(static_cast<std::uint8_t>(8 + 2 * sp));
    pc |= static_cast<std::uint16_t>(RamRead(static_cast<std::uint8_t>(9 + 2 * sp))) << 8;
    pc_ = static_cast<std::uint16_t>(pc & (irqInProgress_ ? 0x7FF : 0xFFF));
    psw_ = static_cast<std::uint8_t>((psw_ & 0xF0) | sp);
}

void MameDerivedI8039Adapter::ExecuteAdd(std::uint8_t data) {
    const std::uint16_t temp = static_cast<std::uint16_t>(a_ + data);
    const std::uint16_t temp4 = static_cast<std::uint16_t>((a_ & 0x0F) + (data & 0x0F));
    psw_ &= static_cast<std::uint8_t>(~(kCFlag | kAFlag));
    psw_ |= static_cast<std::uint8_t>((temp4 << 2) & kAFlag);
    psw_ |= static_cast<std::uint8_t>((temp >> 1) & kCFlag);
    a_ = static_cast<std::uint8_t>(temp);
}

void MameDerivedI8039Adapter::ExecuteAddc(std::uint8_t data) {
    const std::uint8_t carryin = (psw_ & kCFlag) >> 7;
    const std::uint16_t temp = static_cast<std::uint16_t>(a_ + data + carryin);
    const std::uint16_t temp4 = static_cast<std::uint16_t>((a_ & 0x0F) + (data & 0x0F) + carryin);
    psw_ &= static_cast<std::uint8_t>(~(kCFlag | kAFlag));
    psw_ |= static_cast<std::uint8_t>((temp4 << 2) & kAFlag);
    psw_ |= static_cast<std::uint8_t>((temp >> 1) & kCFlag);
    a_ = static_cast<std::uint8_t>(temp);
}

void MameDerivedI8039Adapter::ExecuteJmp(std::uint16_t address) {
    const std::uint16_t a11 = irqInProgress_ ? 0 : a11_;
    pc_ = static_cast<std::uint16_t>(address | a11);
}

void MameDerivedI8039Adapter::ExecuteCall(std::uint16_t address) {
    PushPcPsw();
    ExecuteJmp(address);
}

void MameDerivedI8039Adapter::ExecuteJcc(bool result) {
    const std::uint16_t pch = pc_ & 0xF00;
    const std::uint8_t offset = ArgumentFetch();
    if (result) pc_ = static_cast<std::uint16_t>(pch | offset);
}

void MameDerivedI8039Adapter::ExpanderOperation(int operation, std::uint8_t port) {
    // 8243 expander protocol. No PROG line/8243 is wired on the target board, so there is
    // no prog_out callback; the data-transfer semantics are implemented for completeness.
    p2_ = static_cast<std::uint8_t>((p2_ & 0xF0) | (static_cast<std::uint8_t>(operation) << 2) | (port & 3));
    Port2Write(p2_);
    if (operation != kExpanderRead) {
        p2_ = static_cast<std::uint8_t>((p2_ & 0xF0) | (a_ & 0x0F));
        Port2Write(p2_);
    } else {
        p2_ |= 0x0F;
        Port2Write(p2_);
        a_ = Port2Read() & 0x0F;
    }
}

void MameDerivedI8039Adapter::BurnMachineCycles(int count) {
    if (timeCountEnabled_) {
        bool timerOver = false;
        if (timeCountEnabled_ & kTimerEnabled) {
            const std::uint8_t oldTimer = timer_;
            prescaler_ = static_cast<std::uint8_t>(prescaler_ + count);
            timer_ = static_cast<std::uint8_t>(timer_ + (prescaler_ >> 5));
            prescaler_ &= 0x1F;
            timerOver = timer_ < oldTimer;
        } else if (timeCountEnabled_ & kCounterEnabled) {
            for (; count > 0; --count) {
                t1History_ = static_cast<std::uint8_t>((t1History_ << 1) | (TestRead(1) ? 1 : 0));
                if ((t1History_ & 3) == 2) {
                    if (++timer_ == 0) timerOver = true;
                }
            }
        }
        if (timerOver) {
            timerFlag_ = true;
            if (tirqEnabled_) timerOverflow_ = true;
        }
    }
    clockDebt_ += static_cast<std::uint64_t>(count) * 15;  // 1 machine cycle = 15 input clocks
}

void MameDerivedI8039Adapter::CheckIrqs() {
    if (irqInProgress_) return;

    if (irqState_ && xirqEnabled_) {
        BurnMachineCycles(2);
        irqInProgress_ = true;
        ++irqServiceCount_;

        // Force JNI to be taken (mcs48.cpp's hack): if the instruction just executed was a
        // JNI that did not branch, rewind to just past its opcode and re-run the jump
        // with the branch forced.
        if (irqPolled_) {
            pc_ = static_cast<std::uint16_t>(((prevPc_ + 1) & 0x7FF) | (prevPc_ & 0x800));
            ExecuteJcc(true);
        }

        ExecuteCall(0x003);
    } else if (timerOverflow_ && tirqEnabled_) {
        BurnMachineCycles(2);
        irqInProgress_ = true;
        ++timerIrqServiceCount_;

        ExecuteCall(0x007);
        timerOverflow_ = false;
    }
}

void MameDerivedI8039Adapter::RunOneInstruction() {
    CheckIrqs();
    irqPolled_ = false;

    prevPc_ = pc_;
    const std::uint8_t opcode = OpcodeFetch();
    (this->*kOpcodeTable[opcode])();
}

std::uint64_t MameDerivedI8039Adapter::Execute(std::uint64_t clockBudget) {
    clockDebt_ = 0;
    do {
        RunOneInstruction();
    } while (clockDebt_ < clockBudget);
    return clockDebt_;
}

MameDerivedI8039Adapter::RegisterSnapshot MameDerivedI8039Adapter::Snapshot() const {
    RegisterSnapshot snap;
    snap.pc = pc_;
    snap.a = a_;
    snap.psw = psw_;
    snap.f1 = f1_;
    snap.p1 = p1_;
    snap.p2 = p2_;
    snap.timer = timer_;
    snap.irqInProgress = irqInProgress_;
    snap.irqServiceCount = irqServiceCount_;
    snap.timerIrqServiceCount = timerIrqServiceCount_;
    return snap;
}

// ============================================================================
// OPCODE HANDLERS (mcs48.cpp's OPHANDLER bodies). Only the handlers reachable from
// s_mcs48_opcodes are implemented; the UPI-41-only opcodes do not exist on the i8039.
// ============================================================================

void MameDerivedI8039Adapter::Illegal() { BurnMachineCycles(1); }
void MameDerivedI8039Adapter::Nop() { BurnMachineCycles(1); }
void MameDerivedI8039Adapter::OutlBusA() { BurnMachineCycles(2); BusWrite(0, a_); }
void MameDerivedI8039Adapter::AddAN() { BurnMachineCycles(2); ExecuteAdd(ArgumentFetch()); }

void MameDerivedI8039Adapter::Jmp0() { BurnMachineCycles(2); ExecuteJmp(ArgumentFetch() | 0x000); }
void MameDerivedI8039Adapter::Jmp1() { BurnMachineCycles(2); ExecuteJmp(ArgumentFetch() | 0x100); }
void MameDerivedI8039Adapter::Jmp2() { BurnMachineCycles(2); ExecuteJmp(ArgumentFetch() | 0x200); }
void MameDerivedI8039Adapter::Jmp3() { BurnMachineCycles(2); ExecuteJmp(ArgumentFetch() | 0x300); }
void MameDerivedI8039Adapter::Jmp4() { BurnMachineCycles(2); ExecuteJmp(ArgumentFetch() | 0x400); }
void MameDerivedI8039Adapter::Jmp5() { BurnMachineCycles(2); ExecuteJmp(ArgumentFetch() | 0x500); }
void MameDerivedI8039Adapter::Jmp6() { BurnMachineCycles(2); ExecuteJmp(ArgumentFetch() | 0x600); }
void MameDerivedI8039Adapter::Jmp7() { BurnMachineCycles(2); ExecuteJmp(ArgumentFetch() | 0x700); }

void MameDerivedI8039Adapter::EnI() { BurnMachineCycles(1); xirqEnabled_ = true; }
void MameDerivedI8039Adapter::DecA() { BurnMachineCycles(1); --a_; }
void MameDerivedI8039Adapter::InsABus() { BurnMachineCycles(2); a_ = BusRead(0); }
void MameDerivedI8039Adapter::InAP1() { BurnMachineCycles(2); a_ = static_cast<std::uint8_t>(Port1Read() & p1_); }
void MameDerivedI8039Adapter::InAP2() { BurnMachineCycles(2); a_ = static_cast<std::uint8_t>(Port2Read() & p2_); }

void MameDerivedI8039Adapter::MovdAP4() { BurnMachineCycles(2); ExpanderOperation(kExpanderRead, 4); }
void MameDerivedI8039Adapter::MovdAP5() { BurnMachineCycles(2); ExpanderOperation(kExpanderRead, 5); }
void MameDerivedI8039Adapter::MovdAP6() { BurnMachineCycles(2); ExpanderOperation(kExpanderRead, 6); }
void MameDerivedI8039Adapter::MovdAP7() { BurnMachineCycles(2); ExpanderOperation(kExpanderRead, 7); }

void MameDerivedI8039Adapter::IncXr0() { BurnMachineCycles(1); RamWrite(Reg(0), static_cast<std::uint8_t>(RamRead(Reg(0)) + 1)); }
void MameDerivedI8039Adapter::IncXr1() { BurnMachineCycles(1); RamWrite(Reg(1), static_cast<std::uint8_t>(RamRead(Reg(1)) + 1)); }

void MameDerivedI8039Adapter::Jb0() { BurnMachineCycles(2); ExecuteJcc((a_ & 0x01) != 0); }
void MameDerivedI8039Adapter::Jb1() { BurnMachineCycles(2); ExecuteJcc((a_ & 0x02) != 0); }
void MameDerivedI8039Adapter::Jb2() { BurnMachineCycles(2); ExecuteJcc((a_ & 0x04) != 0); }
void MameDerivedI8039Adapter::Jb3() { BurnMachineCycles(2); ExecuteJcc((a_ & 0x08) != 0); }
void MameDerivedI8039Adapter::Jb4() { BurnMachineCycles(2); ExecuteJcc((a_ & 0x10) != 0); }
void MameDerivedI8039Adapter::Jb5() { BurnMachineCycles(2); ExecuteJcc((a_ & 0x20) != 0); }
void MameDerivedI8039Adapter::Jb6() { BurnMachineCycles(2); ExecuteJcc((a_ & 0x40) != 0); }
void MameDerivedI8039Adapter::Jb7() { BurnMachineCycles(2); ExecuteJcc((a_ & 0x80) != 0); }

void MameDerivedI8039Adapter::AdcAN() { BurnMachineCycles(2); ExecuteAddc(ArgumentFetch()); }

void MameDerivedI8039Adapter::Call0() { BurnMachineCycles(2); ExecuteCall(ArgumentFetch() | 0x000); }
void MameDerivedI8039Adapter::Call1() { BurnMachineCycles(2); ExecuteCall(ArgumentFetch() | 0x100); }
void MameDerivedI8039Adapter::Call2() { BurnMachineCycles(2); ExecuteCall(ArgumentFetch() | 0x200); }
void MameDerivedI8039Adapter::Call3() { BurnMachineCycles(2); ExecuteCall(ArgumentFetch() | 0x300); }
void MameDerivedI8039Adapter::Call4() { BurnMachineCycles(2); ExecuteCall(ArgumentFetch() | 0x400); }
void MameDerivedI8039Adapter::Call5() { BurnMachineCycles(2); ExecuteCall(ArgumentFetch() | 0x500); }
void MameDerivedI8039Adapter::Call6() { BurnMachineCycles(2); ExecuteCall(ArgumentFetch() | 0x600); }
void MameDerivedI8039Adapter::Call7() { BurnMachineCycles(2); ExecuteCall(ArgumentFetch() | 0x700); }

void MameDerivedI8039Adapter::DisI() { BurnMachineCycles(1); xirqEnabled_ = false; }
void MameDerivedI8039Adapter::Jtf() { BurnMachineCycles(2); ExecuteJcc(timerFlag_); timerFlag_ = false; }
void MameDerivedI8039Adapter::IncA() { BurnMachineCycles(1); ++a_; }

void MameDerivedI8039Adapter::IncR0() { BurnMachineCycles(1); ++Reg(0); }
void MameDerivedI8039Adapter::IncR1() { BurnMachineCycles(1); ++Reg(1); }
void MameDerivedI8039Adapter::IncR2() { BurnMachineCycles(1); ++Reg(2); }
void MameDerivedI8039Adapter::IncR3() { BurnMachineCycles(1); ++Reg(3); }
void MameDerivedI8039Adapter::IncR4() { BurnMachineCycles(1); ++Reg(4); }
void MameDerivedI8039Adapter::IncR5() { BurnMachineCycles(1); ++Reg(5); }
void MameDerivedI8039Adapter::IncR6() { BurnMachineCycles(1); ++Reg(6); }
void MameDerivedI8039Adapter::IncR7() { BurnMachineCycles(1); ++Reg(7); }

void MameDerivedI8039Adapter::XchAXr0() { BurnMachineCycles(1); std::uint8_t t = a_; a_ = RamRead(Reg(0)); RamWrite(Reg(0), t); }
void MameDerivedI8039Adapter::XchAXr1() { BurnMachineCycles(1); std::uint8_t t = a_; a_ = RamRead(Reg(1)); RamWrite(Reg(1), t); }

void MameDerivedI8039Adapter::MovAN() { BurnMachineCycles(2); a_ = ArgumentFetch(); }
void MameDerivedI8039Adapter::EnTcnti() { BurnMachineCycles(1); tirqEnabled_ = true; }
void MameDerivedI8039Adapter::Jnt0() { BurnMachineCycles(2); ExecuteJcc(!TestRead(0)); }
void MameDerivedI8039Adapter::ClrA() { BurnMachineCycles(1); a_ = 0; }

void MameDerivedI8039Adapter::XchAR0() { BurnMachineCycles(1); std::uint8_t t = a_; a_ = Reg(0); Reg(0) = t; }
void MameDerivedI8039Adapter::XchAR1() { BurnMachineCycles(1); std::uint8_t t = a_; a_ = Reg(1); Reg(1) = t; }
void MameDerivedI8039Adapter::XchAR2() { BurnMachineCycles(1); std::uint8_t t = a_; a_ = Reg(2); Reg(2) = t; }
void MameDerivedI8039Adapter::XchAR3() { BurnMachineCycles(1); std::uint8_t t = a_; a_ = Reg(3); Reg(3) = t; }
void MameDerivedI8039Adapter::XchAR4() { BurnMachineCycles(1); std::uint8_t t = a_; a_ = Reg(4); Reg(4) = t; }
void MameDerivedI8039Adapter::XchAR5() { BurnMachineCycles(1); std::uint8_t t = a_; a_ = Reg(5); Reg(5) = t; }
void MameDerivedI8039Adapter::XchAR6() { BurnMachineCycles(1); std::uint8_t t = a_; a_ = Reg(6); Reg(6) = t; }
void MameDerivedI8039Adapter::XchAR7() { BurnMachineCycles(1); std::uint8_t t = a_; a_ = Reg(7); Reg(7) = t; }

void MameDerivedI8039Adapter::XchdAXr0() {
    BurnMachineCycles(1);
    const std::uint8_t oldRam = RamRead(Reg(0));
    RamWrite(Reg(0), static_cast<std::uint8_t>((oldRam & 0xF0) | (a_ & 0x0F)));
    a_ = static_cast<std::uint8_t>((a_ & 0xF0) | (oldRam & 0x0F));
}
void MameDerivedI8039Adapter::XchdAXr1() {
    BurnMachineCycles(1);
    const std::uint8_t oldRam = RamRead(Reg(1));
    RamWrite(Reg(1), static_cast<std::uint8_t>((oldRam & 0xF0) | (a_ & 0x0F)));
    a_ = static_cast<std::uint8_t>((a_ & 0xF0) | (oldRam & 0x0F));
}

void MameDerivedI8039Adapter::DisTcnti() { BurnMachineCycles(1); tirqEnabled_ = false; timerOverflow_ = false; }
void MameDerivedI8039Adapter::Jt0() { BurnMachineCycles(2); ExecuteJcc(TestRead(0)); }
void MameDerivedI8039Adapter::CplA() { BurnMachineCycles(1); a_ ^= 0xFF; }
void MameDerivedI8039Adapter::OutlP1A() { BurnMachineCycles(2); p1_ = a_; Port1Write(p1_); }
void MameDerivedI8039Adapter::OutlP2A() { BurnMachineCycles(2); p2_ = a_; Port2Write(p2_); }

void MameDerivedI8039Adapter::MovdP4A() { BurnMachineCycles(2); ExpanderOperation(kExpanderWrite, 4); }
void MameDerivedI8039Adapter::MovdP5A() { BurnMachineCycles(2); ExpanderOperation(kExpanderWrite, 5); }
void MameDerivedI8039Adapter::MovdP6A() { BurnMachineCycles(2); ExpanderOperation(kExpanderWrite, 6); }
void MameDerivedI8039Adapter::MovdP7A() { BurnMachineCycles(2); ExpanderOperation(kExpanderWrite, 7); }

void MameDerivedI8039Adapter::OrlAXr0() { BurnMachineCycles(1); a_ |= RamRead(Reg(0)); }
void MameDerivedI8039Adapter::OrlAXr1() { BurnMachineCycles(1); a_ |= RamRead(Reg(1)); }
void MameDerivedI8039Adapter::MovAT() { BurnMachineCycles(1); a_ = timer_; }
void MameDerivedI8039Adapter::OrlAN() { BurnMachineCycles(2); a_ |= ArgumentFetch(); }
void MameDerivedI8039Adapter::StrtCnt() {
    BurnMachineCycles(1);
    if (!(timeCountEnabled_ & kCounterEnabled)) t1History_ = TestRead(1) ? 1 : 0;
    timeCountEnabled_ = kCounterEnabled;
}
void MameDerivedI8039Adapter::Jnt1() { BurnMachineCycles(2); ExecuteJcc(!TestRead(1)); }
void MameDerivedI8039Adapter::SwapA() { BurnMachineCycles(1); a_ = static_cast<std::uint8_t>((a_ << 4) | (a_ >> 4)); }

void MameDerivedI8039Adapter::OrlAR0() { BurnMachineCycles(1); a_ |= Reg(0); }
void MameDerivedI8039Adapter::OrlAR1() { BurnMachineCycles(1); a_ |= Reg(1); }
void MameDerivedI8039Adapter::OrlAR2() { BurnMachineCycles(1); a_ |= Reg(2); }
void MameDerivedI8039Adapter::OrlAR3() { BurnMachineCycles(1); a_ |= Reg(3); }
void MameDerivedI8039Adapter::OrlAR4() { BurnMachineCycles(1); a_ |= Reg(4); }
void MameDerivedI8039Adapter::OrlAR5() { BurnMachineCycles(1); a_ |= Reg(5); }
void MameDerivedI8039Adapter::OrlAR6() { BurnMachineCycles(1); a_ |= Reg(6); }
void MameDerivedI8039Adapter::OrlAR7() { BurnMachineCycles(1); a_ |= Reg(7); }

void MameDerivedI8039Adapter::AnlAXr0() { BurnMachineCycles(1); a_ &= RamRead(Reg(0)); }
void MameDerivedI8039Adapter::AnlAXr1() { BurnMachineCycles(1); a_ &= RamRead(Reg(1)); }
void MameDerivedI8039Adapter::AnlAN() { BurnMachineCycles(2); a_ &= ArgumentFetch(); }
void MameDerivedI8039Adapter::StrtT() { BurnMachineCycles(1); timeCountEnabled_ = kTimerEnabled; prescaler_ = 0; }
void MameDerivedI8039Adapter::Jt1() { BurnMachineCycles(2); ExecuteJcc(TestRead(1)); }
void MameDerivedI8039Adapter::DaA() {
    BurnMachineCycles(1);
    if ((a_ & 0x0F) > 0x09 || (psw_ & kAFlag)) {
        if (a_ > 0xF9) psw_ |= kCFlag;
        a_ = static_cast<std::uint8_t>(a_ + 0x06);
    }
    if ((a_ & 0xF0) > 0x90 || (psw_ & kCFlag)) {
        a_ = static_cast<std::uint8_t>(a_ + 0x60);
        psw_ |= kCFlag;
    }
}

void MameDerivedI8039Adapter::AnlAR0() { BurnMachineCycles(1); a_ &= Reg(0); }
void MameDerivedI8039Adapter::AnlAR1() { BurnMachineCycles(1); a_ &= Reg(1); }
void MameDerivedI8039Adapter::AnlAR2() { BurnMachineCycles(1); a_ &= Reg(2); }
void MameDerivedI8039Adapter::AnlAR3() { BurnMachineCycles(1); a_ &= Reg(3); }
void MameDerivedI8039Adapter::AnlAR4() { BurnMachineCycles(1); a_ &= Reg(4); }
void MameDerivedI8039Adapter::AnlAR5() { BurnMachineCycles(1); a_ &= Reg(5); }
void MameDerivedI8039Adapter::AnlAR6() { BurnMachineCycles(1); a_ &= Reg(6); }
void MameDerivedI8039Adapter::AnlAR7() { BurnMachineCycles(1); a_ &= Reg(7); }

void MameDerivedI8039Adapter::AddAXr0() { BurnMachineCycles(1); ExecuteAdd(RamRead(Reg(0))); }
void MameDerivedI8039Adapter::AddAXr1() { BurnMachineCycles(1); ExecuteAdd(RamRead(Reg(1))); }
void MameDerivedI8039Adapter::MovTA() { BurnMachineCycles(1); timer_ = a_; }
void MameDerivedI8039Adapter::StopTcnt() { BurnMachineCycles(1); timeCountEnabled_ = 0; }
void MameDerivedI8039Adapter::RrcA() {
    BurnMachineCycles(1);
    const std::uint8_t newC = static_cast<std::uint8_t>((a_ << 7) & kCFlag);
    a_ = static_cast<std::uint8_t>((a_ >> 1) | (psw_ & kCFlag));
    psw_ = static_cast<std::uint8_t>((psw_ & ~kCFlag) | newC);
}

void MameDerivedI8039Adapter::AddAR0() { BurnMachineCycles(1); ExecuteAdd(Reg(0)); }
void MameDerivedI8039Adapter::AddAR1() { BurnMachineCycles(1); ExecuteAdd(Reg(1)); }
void MameDerivedI8039Adapter::AddAR2() { BurnMachineCycles(1); ExecuteAdd(Reg(2)); }
void MameDerivedI8039Adapter::AddAR3() { BurnMachineCycles(1); ExecuteAdd(Reg(3)); }
void MameDerivedI8039Adapter::AddAR4() { BurnMachineCycles(1); ExecuteAdd(Reg(4)); }
void MameDerivedI8039Adapter::AddAR5() { BurnMachineCycles(1); ExecuteAdd(Reg(5)); }
void MameDerivedI8039Adapter::AddAR6() { BurnMachineCycles(1); ExecuteAdd(Reg(6)); }
void MameDerivedI8039Adapter::AddAR7() { BurnMachineCycles(1); ExecuteAdd(Reg(7)); }

void MameDerivedI8039Adapter::AdcAXr0() { BurnMachineCycles(1); ExecuteAddc(RamRead(Reg(0))); }
void MameDerivedI8039Adapter::AdcAXr1() { BurnMachineCycles(1); ExecuteAddc(RamRead(Reg(1))); }
void MameDerivedI8039Adapter::Ent0Clk() { BurnMachineCycles(1); /* T0 clock-out mode: not modeled, no consumer in this project's target boards */ }
void MameDerivedI8039Adapter::Jf1() { BurnMachineCycles(2); ExecuteJcc(f1_); }
void MameDerivedI8039Adapter::RrA() { BurnMachineCycles(1); a_ = static_cast<std::uint8_t>((a_ >> 1) | (a_ << 7)); }

void MameDerivedI8039Adapter::AdcAR0() { BurnMachineCycles(1); ExecuteAddc(Reg(0)); }
void MameDerivedI8039Adapter::AdcAR1() { BurnMachineCycles(1); ExecuteAddc(Reg(1)); }
void MameDerivedI8039Adapter::AdcAR2() { BurnMachineCycles(1); ExecuteAddc(Reg(2)); }
void MameDerivedI8039Adapter::AdcAR3() { BurnMachineCycles(1); ExecuteAddc(Reg(3)); }
void MameDerivedI8039Adapter::AdcAR4() { BurnMachineCycles(1); ExecuteAddc(Reg(4)); }
void MameDerivedI8039Adapter::AdcAR5() { BurnMachineCycles(1); ExecuteAddc(Reg(5)); }
void MameDerivedI8039Adapter::AdcAR6() { BurnMachineCycles(1); ExecuteAddc(Reg(6)); }
void MameDerivedI8039Adapter::AdcAR7() { BurnMachineCycles(1); ExecuteAddc(Reg(7)); }

void MameDerivedI8039Adapter::MovxAXr0() { BurnMachineCycles(2); a_ = BusRead(Reg(0)); }
void MameDerivedI8039Adapter::MovxAXr1() { BurnMachineCycles(2); a_ = BusRead(Reg(1)); }
void MameDerivedI8039Adapter::Ret() { BurnMachineCycles(2); PullPc(); }
void MameDerivedI8039Adapter::ClrF0() { BurnMachineCycles(1); psw_ &= static_cast<std::uint8_t>(~kFFlag); }
void MameDerivedI8039Adapter::Jni() {
    BurnMachineCycles(2);
    irqPolled_ = !irqState_;
    ExecuteJcc(irqState_);
}

void MameDerivedI8039Adapter::OrlBusN() { BurnMachineCycles(2); BusWrite(0, static_cast<std::uint8_t>(BusRead(0) | ArgumentFetch())); }
void MameDerivedI8039Adapter::OrlP1N() { BurnMachineCycles(2); p1_ |= ArgumentFetch(); Port1Write(p1_); }
void MameDerivedI8039Adapter::OrlP2N() { BurnMachineCycles(2); p2_ |= ArgumentFetch(); Port2Write(p2_); }

void MameDerivedI8039Adapter::OrldP4A() { BurnMachineCycles(2); ExpanderOperation(kExpanderOr, 4); }
void MameDerivedI8039Adapter::OrldP5A() { BurnMachineCycles(2); ExpanderOperation(kExpanderOr, 5); }
void MameDerivedI8039Adapter::OrldP6A() { BurnMachineCycles(2); ExpanderOperation(kExpanderOr, 6); }
void MameDerivedI8039Adapter::OrldP7A() { BurnMachineCycles(2); ExpanderOperation(kExpanderOr, 7); }

void MameDerivedI8039Adapter::MovxXr0A() { BurnMachineCycles(2); BusWrite(Reg(0), a_); }
void MameDerivedI8039Adapter::MovxXr1A() { BurnMachineCycles(2); BusWrite(Reg(1), a_); }
void MameDerivedI8039Adapter::Retr() {
    BurnMachineCycles(2);
    irqInProgress_ = false;
    PullPcPsw();
}
void MameDerivedI8039Adapter::CplF0() { BurnMachineCycles(1); psw_ ^= kFFlag; }
void MameDerivedI8039Adapter::Jnz() { BurnMachineCycles(2); ExecuteJcc(a_ != 0); }
void MameDerivedI8039Adapter::ClrC() { BurnMachineCycles(1); psw_ &= static_cast<std::uint8_t>(~kCFlag); }

void MameDerivedI8039Adapter::AnlBusN() { BurnMachineCycles(2); BusWrite(0, static_cast<std::uint8_t>(BusRead(0) & ArgumentFetch())); }
void MameDerivedI8039Adapter::AnlP1N() { BurnMachineCycles(2); p1_ &= ArgumentFetch(); Port1Write(p1_); }
void MameDerivedI8039Adapter::AnlP2N() { BurnMachineCycles(2); p2_ &= ArgumentFetch(); Port2Write(p2_); }

void MameDerivedI8039Adapter::AnldP4A() { BurnMachineCycles(2); ExpanderOperation(kExpanderAnd, 4); }
void MameDerivedI8039Adapter::AnldP5A() { BurnMachineCycles(2); ExpanderOperation(kExpanderAnd, 5); }
void MameDerivedI8039Adapter::AnldP6A() { BurnMachineCycles(2); ExpanderOperation(kExpanderAnd, 6); }
void MameDerivedI8039Adapter::AnldP7A() { BurnMachineCycles(2); ExpanderOperation(kExpanderAnd, 7); }

void MameDerivedI8039Adapter::MovXr0A() { BurnMachineCycles(1); RamWrite(Reg(0), a_); }
void MameDerivedI8039Adapter::MovXr1A() { BurnMachineCycles(1); RamWrite(Reg(1), a_); }
void MameDerivedI8039Adapter::MovpAXa() { BurnMachineCycles(2); a_ = programRead_ ? programRead_(static_cast<std::uint16_t>((pc_ & 0xF00) | a_)) : 0; }
void MameDerivedI8039Adapter::ClrF1() { BurnMachineCycles(1); f1_ = false; }
void MameDerivedI8039Adapter::CplC() { BurnMachineCycles(1); psw_ ^= kCFlag; }

void MameDerivedI8039Adapter::MovR0A() { BurnMachineCycles(1); Reg(0) = a_; }
void MameDerivedI8039Adapter::MovR1A() { BurnMachineCycles(1); Reg(1) = a_; }
void MameDerivedI8039Adapter::MovR2A() { BurnMachineCycles(1); Reg(2) = a_; }
void MameDerivedI8039Adapter::MovR3A() { BurnMachineCycles(1); Reg(3) = a_; }
void MameDerivedI8039Adapter::MovR4A() { BurnMachineCycles(1); Reg(4) = a_; }
void MameDerivedI8039Adapter::MovR5A() { BurnMachineCycles(1); Reg(5) = a_; }
void MameDerivedI8039Adapter::MovR6A() { BurnMachineCycles(1); Reg(6) = a_; }
void MameDerivedI8039Adapter::MovR7A() { BurnMachineCycles(1); Reg(7) = a_; }

void MameDerivedI8039Adapter::MovXr0N() { BurnMachineCycles(2); RamWrite(Reg(0), ArgumentFetch()); }
void MameDerivedI8039Adapter::MovXr1N() { BurnMachineCycles(2); RamWrite(Reg(1), ArgumentFetch()); }
void MameDerivedI8039Adapter::JmppXa() {
    BurnMachineCycles(2);
    pc_ = static_cast<std::uint16_t>((pc_ & 0xF00) | (programRead_ ? programRead_(static_cast<std::uint16_t>((pc_ & 0xF00) | a_)) : 0));
}
void MameDerivedI8039Adapter::CplF1() { BurnMachineCycles(1); f1_ = !f1_; }
void MameDerivedI8039Adapter::Jf0() { BurnMachineCycles(2); ExecuteJcc((psw_ & kFFlag) != 0); }

void MameDerivedI8039Adapter::MovR0N() { BurnMachineCycles(2); Reg(0) = ArgumentFetch(); }
void MameDerivedI8039Adapter::MovR1N() { BurnMachineCycles(2); Reg(1) = ArgumentFetch(); }
void MameDerivedI8039Adapter::MovR2N() { BurnMachineCycles(2); Reg(2) = ArgumentFetch(); }
void MameDerivedI8039Adapter::MovR3N() { BurnMachineCycles(2); Reg(3) = ArgumentFetch(); }
void MameDerivedI8039Adapter::MovR4N() { BurnMachineCycles(2); Reg(4) = ArgumentFetch(); }
void MameDerivedI8039Adapter::MovR5N() { BurnMachineCycles(2); Reg(5) = ArgumentFetch(); }
void MameDerivedI8039Adapter::MovR6N() { BurnMachineCycles(2); Reg(6) = ArgumentFetch(); }
void MameDerivedI8039Adapter::MovR7N() { BurnMachineCycles(2); Reg(7) = ArgumentFetch(); }

void MameDerivedI8039Adapter::SelRb0() { BurnMachineCycles(1); psw_ &= static_cast<std::uint8_t>(~kBFlag); UpdateRegPtr(); }
void MameDerivedI8039Adapter::Jz() { BurnMachineCycles(2); ExecuteJcc(a_ == 0); }
void MameDerivedI8039Adapter::MovAPsw() { BurnMachineCycles(1); a_ = static_cast<std::uint8_t>(psw_ | 0x08); }

void MameDerivedI8039Adapter::DecR0() { BurnMachineCycles(1); --Reg(0); }
void MameDerivedI8039Adapter::DecR1() { BurnMachineCycles(1); --Reg(1); }
void MameDerivedI8039Adapter::DecR2() { BurnMachineCycles(1); --Reg(2); }
void MameDerivedI8039Adapter::DecR3() { BurnMachineCycles(1); --Reg(3); }
void MameDerivedI8039Adapter::DecR4() { BurnMachineCycles(1); --Reg(4); }
void MameDerivedI8039Adapter::DecR5() { BurnMachineCycles(1); --Reg(5); }
void MameDerivedI8039Adapter::DecR6() { BurnMachineCycles(1); --Reg(6); }
void MameDerivedI8039Adapter::DecR7() { BurnMachineCycles(1); --Reg(7); }

void MameDerivedI8039Adapter::XrlAXr0() { BurnMachineCycles(1); a_ ^= RamRead(Reg(0)); }
void MameDerivedI8039Adapter::XrlAXr1() { BurnMachineCycles(1); a_ ^= RamRead(Reg(1)); }
void MameDerivedI8039Adapter::XrlAN() { BurnMachineCycles(2); a_ ^= ArgumentFetch(); }
void MameDerivedI8039Adapter::SelRb1() { BurnMachineCycles(1); psw_ |= kBFlag; UpdateRegPtr(); }
void MameDerivedI8039Adapter::MovPswA() { BurnMachineCycles(1); psw_ = static_cast<std::uint8_t>(a_ & ~0x08); UpdateRegPtr(); }

void MameDerivedI8039Adapter::XrlAR0() { BurnMachineCycles(1); a_ ^= Reg(0); }
void MameDerivedI8039Adapter::XrlAR1() { BurnMachineCycles(1); a_ ^= Reg(1); }
void MameDerivedI8039Adapter::XrlAR2() { BurnMachineCycles(1); a_ ^= Reg(2); }
void MameDerivedI8039Adapter::XrlAR3() { BurnMachineCycles(1); a_ ^= Reg(3); }
void MameDerivedI8039Adapter::XrlAR4() { BurnMachineCycles(1); a_ ^= Reg(4); }
void MameDerivedI8039Adapter::XrlAR5() { BurnMachineCycles(1); a_ ^= Reg(5); }
void MameDerivedI8039Adapter::XrlAR6() { BurnMachineCycles(1); a_ ^= Reg(6); }
void MameDerivedI8039Adapter::XrlAR7() { BurnMachineCycles(1); a_ ^= Reg(7); }

void MameDerivedI8039Adapter::Movp3AXa() { BurnMachineCycles(2); a_ = programRead_ ? programRead_(static_cast<std::uint16_t>(0x300 | a_)) : 0; }
void MameDerivedI8039Adapter::SelMb0() { BurnMachineCycles(1); a11_ = 0x000; }
void MameDerivedI8039Adapter::Jnc() { BurnMachineCycles(2); ExecuteJcc((psw_ & kCFlag) == 0); }
void MameDerivedI8039Adapter::RlA() { BurnMachineCycles(1); a_ = static_cast<std::uint8_t>((a_ << 1) | (a_ >> 7)); }

void MameDerivedI8039Adapter::DjnzR0() { BurnMachineCycles(2); ExecuteJcc(--Reg(0) != 0); }
void MameDerivedI8039Adapter::DjnzR1() { BurnMachineCycles(2); ExecuteJcc(--Reg(1) != 0); }
void MameDerivedI8039Adapter::DjnzR2() { BurnMachineCycles(2); ExecuteJcc(--Reg(2) != 0); }
void MameDerivedI8039Adapter::DjnzR3() { BurnMachineCycles(2); ExecuteJcc(--Reg(3) != 0); }
void MameDerivedI8039Adapter::DjnzR4() { BurnMachineCycles(2); ExecuteJcc(--Reg(4) != 0); }
void MameDerivedI8039Adapter::DjnzR5() { BurnMachineCycles(2); ExecuteJcc(--Reg(5) != 0); }
void MameDerivedI8039Adapter::DjnzR6() { BurnMachineCycles(2); ExecuteJcc(--Reg(6) != 0); }
void MameDerivedI8039Adapter::DjnzR7() { BurnMachineCycles(2); ExecuteJcc(--Reg(7) != 0); }

void MameDerivedI8039Adapter::MovAXr0() { BurnMachineCycles(1); a_ = RamRead(Reg(0)); }
void MameDerivedI8039Adapter::MovAXr1() { BurnMachineCycles(1); a_ = RamRead(Reg(1)); }
void MameDerivedI8039Adapter::SelMb1() { BurnMachineCycles(1); a11_ = 0x800; }
void MameDerivedI8039Adapter::Jc() { BurnMachineCycles(2); ExecuteJcc((psw_ & kCFlag) != 0); }
void MameDerivedI8039Adapter::RlcA() {
    BurnMachineCycles(1);
    const std::uint8_t newC = a_ & kCFlag;
    a_ = static_cast<std::uint8_t>((a_ << 1) | (psw_ >> 7));
    psw_ = static_cast<std::uint8_t>((psw_ & ~kCFlag) | newC);
}

void MameDerivedI8039Adapter::MovAR0() { BurnMachineCycles(1); a_ = Reg(0); }
void MameDerivedI8039Adapter::MovAR1() { BurnMachineCycles(1); a_ = Reg(1); }
void MameDerivedI8039Adapter::MovAR2() { BurnMachineCycles(1); a_ = Reg(2); }
void MameDerivedI8039Adapter::MovAR3() { BurnMachineCycles(1); a_ = Reg(3); }
void MameDerivedI8039Adapter::MovAR4() { BurnMachineCycles(1); a_ = Reg(4); }
void MameDerivedI8039Adapter::MovAR5() { BurnMachineCycles(1); a_ = Reg(5); }
void MameDerivedI8039Adapter::MovAR6() { BurnMachineCycles(1); a_ = Reg(6); }
void MameDerivedI8039Adapter::MovAR7() { BurnMachineCycles(1); a_ = Reg(7); }

// ============================================================================
// DISPATCH TABLE - transcription of mcs48.cpp's s_mcs48_opcodes[256]
// ============================================================================
// ============================================================================
#define OP(name) &MameDerivedI8039Adapter::name
const MameDerivedI8039Adapter::OpHandler MameDerivedI8039Adapter::kOpcodeTable[256] = {
    OP(Nop),      OP(Illegal),  OP(OutlBusA), OP(AddAN),    OP(Jmp0),     OP(EnI),      OP(Illegal),  OP(DecA),      // 00
    OP(InsABus),  OP(InAP1),    OP(InAP2),    OP(Illegal),  OP(MovdAP4),  OP(MovdAP5),  OP(MovdAP6),  OP(MovdAP7),
    OP(IncXr0),   OP(IncXr1),   OP(Jb0),      OP(AdcAN),    OP(Call0),    OP(DisI),     OP(Jtf),      OP(IncA),      // 10
    OP(IncR0),    OP(IncR1),    OP(IncR2),    OP(IncR3),    OP(IncR4),    OP(IncR5),    OP(IncR6),    OP(IncR7),
    OP(XchAXr0),  OP(XchAXr1),  OP(Illegal),  OP(MovAN),    OP(Jmp1),     OP(EnTcnti),  OP(Jnt0),     OP(ClrA),      // 20
    OP(XchAR0),   OP(XchAR1),   OP(XchAR2),   OP(XchAR3),   OP(XchAR4),   OP(XchAR5),   OP(XchAR6),   OP(XchAR7),
    OP(XchdAXr0), OP(XchdAXr1), OP(Jb1),      OP(Illegal),  OP(Call1),    OP(DisTcnti), OP(Jt0),      OP(CplA),      // 30
    OP(Illegal),  OP(OutlP1A),  OP(OutlP2A),  OP(Illegal),  OP(MovdP4A),  OP(MovdP5A),  OP(MovdP6A),  OP(MovdP7A),
    OP(OrlAXr0),  OP(OrlAXr1),  OP(MovAT),    OP(OrlAN),    OP(Jmp2),     OP(StrtCnt),  OP(Jnt1),     OP(SwapA),     // 40
    OP(OrlAR0),   OP(OrlAR1),   OP(OrlAR2),   OP(OrlAR3),   OP(OrlAR4),   OP(OrlAR5),   OP(OrlAR6),   OP(OrlAR7),
    OP(AnlAXr0),  OP(AnlAXr1),  OP(Jb2),      OP(AnlAN),    OP(Call2),    OP(StrtT),    OP(Jt1),      OP(DaA),       // 50
    OP(AnlAR0),   OP(AnlAR1),   OP(AnlAR2),   OP(AnlAR3),   OP(AnlAR4),   OP(AnlAR5),   OP(AnlAR6),   OP(AnlAR7),
    OP(AddAXr0),  OP(AddAXr1),  OP(MovTA),    OP(Illegal),  OP(Jmp3),     OP(StopTcnt), OP(Illegal),  OP(RrcA),      // 60
    OP(AddAR0),   OP(AddAR1),   OP(AddAR2),   OP(AddAR3),   OP(AddAR4),   OP(AddAR5),   OP(AddAR6),   OP(AddAR7),
    OP(AdcAXr0),  OP(AdcAXr1),  OP(Jb3),      OP(Illegal),  OP(Call3),    OP(Ent0Clk),  OP(Jf1),      OP(RrA),       // 70
    OP(AdcAR0),   OP(AdcAR1),   OP(AdcAR2),   OP(AdcAR3),   OP(AdcAR4),   OP(AdcAR5),   OP(AdcAR6),   OP(AdcAR7),
    OP(MovxAXr0), OP(MovxAXr1), OP(Illegal),  OP(Ret),      OP(Jmp4),     OP(ClrF0),    OP(Jni),      OP(Illegal),   // 80
    OP(OrlBusN),  OP(OrlP1N),   OP(OrlP2N),   OP(Illegal),  OP(OrldP4A),  OP(OrldP5A),  OP(OrldP6A),  OP(OrldP7A),
    OP(MovxXr0A), OP(MovxXr1A), OP(Jb4),      OP(Retr),     OP(Call4),    OP(CplF0),    OP(Jnz),      OP(ClrC),      // 90
    OP(AnlBusN),  OP(AnlP1N),   OP(AnlP2N),   OP(Illegal),  OP(AnldP4A),  OP(AnldP5A),  OP(AnldP6A),  OP(AnldP7A),
    OP(MovXr0A),  OP(MovXr1A),  OP(Illegal),  OP(MovpAXa),  OP(Jmp5),     OP(ClrF1),    OP(Illegal),  OP(CplC),      // A0
    OP(MovR0A),   OP(MovR1A),   OP(MovR2A),   OP(MovR3A),   OP(MovR4A),   OP(MovR5A),   OP(MovR6A),   OP(MovR7A),
    OP(MovXr0N),  OP(MovXr1N),  OP(Jb5),      OP(JmppXa),   OP(Call5),    OP(CplF1),    OP(Jf0),      OP(Illegal),   // B0
    OP(MovR0N),   OP(MovR1N),   OP(MovR2N),   OP(MovR3N),   OP(MovR4N),   OP(MovR5N),   OP(MovR6N),   OP(MovR7N),
    OP(Illegal),  OP(Illegal),  OP(Illegal),  OP(Illegal),  OP(Jmp6),     OP(SelRb0),   OP(Jz),       OP(MovAPsw),   // C0
    OP(DecR0),    OP(DecR1),    OP(DecR2),    OP(DecR3),    OP(DecR4),    OP(DecR5),    OP(DecR6),    OP(DecR7),
    OP(XrlAXr0),  OP(XrlAXr1),  OP(Jb6),      OP(XrlAN),    OP(Call6),    OP(SelRb1),   OP(Illegal),  OP(MovPswA),   // D0
    OP(XrlAR0),   OP(XrlAR1),   OP(XrlAR2),   OP(XrlAR3),   OP(XrlAR4),   OP(XrlAR5),   OP(XrlAR6),   OP(XrlAR7),
    OP(Illegal),  OP(Illegal),  OP(Illegal),  OP(Movp3AXa), OP(Jmp7),     OP(SelMb0),   OP(Jnc),      OP(RlA),       // E0
    OP(DjnzR0),   OP(DjnzR1),   OP(DjnzR2),   OP(DjnzR3),   OP(DjnzR4),   OP(DjnzR5),   OP(DjnzR6),   OP(DjnzR7),
    OP(MovAXr0),  OP(MovAXr1),  OP(Jb7),      OP(Illegal),  OP(Call7),    OP(SelMb1),   OP(Jc),       OP(RlcA),      // F0
    OP(MovAR0),   OP(MovAR1),   OP(MovAR2),   OP(MovAR3),   OP(MovAR4),   OP(MovAR5),   OP(MovAR6),   OP(MovAR7),
};
#undef OP
