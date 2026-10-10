// license:BSD-3-Clause
// copyright-holders:Juergen Buchmueller
// copyright-holders:Juergen Buchmueller, Andrei I. Holub
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/cpu/z80/z80.cpp  [copyright-holders:Juergen Buchmueller]
//   src/devices/cpu/z80/z80.h  [copyright-holders:Juergen Buchmueller]
//   src/devices/cpu/z80/z80.lst  [copyright-holders:Juergen Buchmueller, Andrei I. Holub]
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

// MameDerivedZ80Adapter - see mame_derived_z80_adapter.h and
// THIRD-PARTY-NOTICES.md.
//
// Ported from MAME's z80_device: the register/flag storage layout and the
// ALU/rotate/shift/bit primitives (Op* methods), including the exact flag
// arithmetic.
//
// Not ported: instruction dispatch and per-instruction cycle counts, which
// MAME generates at build time and which model bus contention that an arcade
// sound bus does not need. This adapter decodes opcodes with the standard
// x/y/z/p/q bitfield split and uses the documented Z80 cycle counts. Interrupt
// acknowledge sequencing (NMI/IM 0/1/2, the EI one-instruction delay) also
// follows the documented behavior.
//
// Limitations: CB opcodes issued through a DD/FD prefix only affect the
// (IX+d)/(IY+d) memory location, not the aliased register; undefined ED
// opcodes act as an 8-cycle NOP; IM 0 accepts only a single injected opcode
// (as in MAME). Repeat-block instructions (LDIR/CPIR/INIR/OTIR and the *D
// forms) run to completion in one call instead of being interruptible
// between iterations.

#include "mame_derived_z80_adapter.h"

#include <utility>

namespace {

// Standard Z80 8-bit register index (bitfield z/y position 0-7), used by
// several opcode groups: B, C, D, E, H, L, (HL), A.
constexpr int kRegB = 0;
constexpr int kRegC = 1;
constexpr int kRegD = 2;
constexpr int kRegE = 3;
constexpr int kRegH = 4;
constexpr int kRegL = 5;
constexpr int kRegIndirectHl = 6;
constexpr int kRegA = 7;

}  // namespace

MameDerivedZ80Adapter::MameDerivedZ80Adapter() {
    Reset();
}

void MameDerivedZ80Adapter::SetMemoryCallbacks(MemoryReadFn read, MemoryWriteFn write) {
    memoryRead_ = std::move(read);
    memoryWrite_ = std::move(write);
}

void MameDerivedZ80Adapter::SetIoCallbacks(IoReadFn read, IoWriteFn write) {
    ioRead_ = std::move(read);
    ioWrite_ = std::move(write);
}

void MameDerivedZ80Adapter::SetInterruptAcknowledgeCallback(InterruptAckFn callback) {
    interruptAck_ = std::move(callback);
}

void MameDerivedZ80Adapter::Reset() {
    af_ = bc_ = de_ = hl_ = ix_ = iy_ = sp_ = pc_ = wz_ = Z80RegisterPair(0);
    af2_ = bc2_ = de2_ = hl2_ = Z80RegisterPair(0);
    af2_.low = 0;
    f_ = Z80Flags{};
    i_ = 0;
    r_ = 0;
    r2_ = 0;
    iff1_ = false;
    iff2_ = false;
    im_ = 0;
    halted_ = false;
    nmiPending_ = false;
    irqAsserted_ = false;
    nmiLineState_ = Z80InputLineState::Clear;
    ix_ = 0xFFFF;
    iy_ = 0xFFFF;
}

MameDerivedZ80Adapter::RegisterSnapshot MameDerivedZ80Adapter::Snapshot() const {
    RegisterSnapshot snap;
    snap.af = static_cast<std::uint16_t>((af_.high << 8) | GetF());
    snap.bc = bc_.Word();
    snap.de = de_.Word();
    snap.hl = hl_.Word();
    snap.ix = ix_.Word();
    snap.iy = iy_.Word();
    snap.sp = sp_.Word();
    snap.pc = pc_.Word();
    snap.af2 = static_cast<std::uint16_t>((af2_.high << 8) | af2_.low);
    snap.bc2 = bc2_.Word();
    snap.de2 = de2_.Word();
    snap.hl2 = hl2_.Word();
    snap.i = i_;
    snap.r = static_cast<std::uint8_t>((r_ & 0x7F) | (r2_ & 0x80));
    snap.im = im_;
    snap.iff1 = iff1_;
    snap.iff2 = iff2_;
    snap.halted = halted_;
    snap.im0ServiceCount = im0ServiceCount_;
    snap.im0UnhandledServiceCount = im0UnhandledServiceCount_;
    snap.nmiServiceCount = nmiServiceCount_;
    snap.irqServiceCount = irqServiceCount_;
    return snap;
}

void MameDerivedZ80Adapter::SetInputLine(Z80InputLine line, Z80InputLineState state) {
    if (line == Z80InputLine::Nmi) {
        if (nmiLineState_ == Z80InputLineState::Clear && state == Z80InputLineState::Assert) {
            nmiPending_ = true;  // edge-triggered: latched on the Clear->Assert transition only
        }
        nmiLineState_ = state;
    } else {
        irqAsserted_ = (state == Z80InputLineState::Assert);  // level-triggered: mirrors the pin directly
    }
}

// ---------------------------------------------------------------------
// Memory/cycle helpers
// ---------------------------------------------------------------------

void MameDerivedZ80Adapter::AddCycles(int cycles) {
    cyclesConsumedThisCall_ += static_cast<std::uint64_t>(cycles);
}

std::uint8_t MameDerivedZ80Adapter::ReadMemory(std::uint16_t address) const {
    return memoryRead_ ? memoryRead_(address) : 0xFF;
}

void MameDerivedZ80Adapter::WriteMemory(std::uint16_t address, std::uint8_t value) const {
    if (memoryWriteObserver_) {
        memoryWriteObserver_(address, value);
    }
    if (memoryWrite_) {
        memoryWrite_(address, value);
    }
}

std::uint8_t MameDerivedZ80Adapter::FetchOpcode() {
    const std::uint16_t at = pc_.Word();
    std::uint8_t value = ReadMemory(at);
    ++pc_;
    r_ = static_cast<std::uint8_t>((r_ + 1) & 0x7F);
    if (opcodeFetchObserver_) {
        opcodeFetchObserver_(at, value);
    }
    return value;
}

std::uint8_t MameDerivedZ80Adapter::FetchByte() {
    std::uint8_t value = ReadMemory(pc_.Word());
    ++pc_;
    return value;
}

std::uint16_t MameDerivedZ80Adapter::FetchWord() {
    std::uint8_t lo = FetchByte();
    std::uint8_t hi = FetchByte();
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(hi) << 8) | lo);
}

void MameDerivedZ80Adapter::PushWord(std::uint16_t value) {
    --sp_;
    WriteMemory(sp_.Word(), static_cast<std::uint8_t>(value >> 8));
    --sp_;
    WriteMemory(sp_.Word(), static_cast<std::uint8_t>(value & 0xFF));
}

std::uint16_t MameDerivedZ80Adapter::PopWord() {
    std::uint8_t lo = ReadMemory(sp_.Word());
    ++sp_;
    std::uint8_t hi = ReadMemory(sp_.Word());
    ++sp_;
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(hi) << 8) | lo);
}

void MameDerivedZ80Adapter::JumpRelative(std::int8_t displacement) {
    wz_ = static_cast<std::uint16_t>(pc_.Word() + displacement);
    pc_ = wz_.Word();
}

bool MameDerivedZ80Adapter::TestCondition(int conditionCode) const {
    switch (conditionCode) {
        case 0: return f_.zValue != 0;               // NZ
        case 1: return f_.zValue == 0;                // Z
        case 2: return !f_.c;                          // NC
        case 3: return f_.c;                            // C
        case 4: return (f_.Pv() & kZ80FlagPV) == 0;     // PO (parity odd -> PV flag clear)
        case 5: return (f_.Pv() & kZ80FlagPV) != 0;     // PE (parity even -> PV flag set)
        case 6: return (f_.S() & kZ80FlagS) == 0;        // P (sign positive)
        case 7: return (f_.S() & kZ80FlagS) != 0;        // M (sign negative)
        default: return false;
    }
}

// ---------------------------------------------------------------------
// ALU / rotate / shift / bit primitives (ported from z80_device - see
// THIRD-PARTY-NOTICES.md)
// ---------------------------------------------------------------------

void MameDerivedZ80Adapter::OpInc(std::uint8_t& r) {
    ++r;
    f_.qTemp = 0;
    f_.sValue = f_.zValue = f_.yxValue = r;
    f_.pvValue = static_cast<std::uint8_t>(r != 0x80);
    f_.hValue = static_cast<std::uint8_t>((r & 0x0F) == 0x00 ? kZ80FlagH : 0);
    f_.n = false;
}

void MameDerivedZ80Adapter::OpDec(std::uint8_t& r) {
    --r;
    f_.qTemp = 0;
    f_.sValue = f_.zValue = f_.yxValue = r;
    f_.pvValue = static_cast<std::uint8_t>(r != 0x7F);
    f_.hValue = static_cast<std::uint8_t>((r & 0x0F) == 0x0F ? kZ80FlagH : 0);
    f_.n = true;
}

void MameDerivedZ80Adapter::OpRlca() {
    std::uint8_t a = af_.high;
    a = static_cast<std::uint8_t>((a << 1) | (a >> 7));
    f_.qTemp = 0;
    f_.yxValue = a;
    f_.hValue = 0;
    f_.n = false;
    f_.c = (af_.high & 0x01) != 0;
    af_.high = a;
}

void MameDerivedZ80Adapter::OpRrca() {
    std::uint8_t a0 = af_.high;
    af_.high = static_cast<std::uint8_t>((a0 >> 1) | (a0 << 7));
    f_.qTemp = 0;
    f_.yxValue = af_.high;
    f_.hValue = 0;
    f_.n = false;
    f_.c = (a0 & 0x01) != 0;
}

void MameDerivedZ80Adapter::OpRla() {
    std::uint8_t res = static_cast<std::uint8_t>((af_.high << 1) + (f_.c ? 1 : 0));
    f_.qTemp = 0;
    f_.yxValue = res;
    f_.hValue = 0;
    f_.n = false;
    f_.c = (af_.high & 0x80) != 0;
    af_.high = res;
}

void MameDerivedZ80Adapter::OpRra() {
    std::uint8_t res = static_cast<std::uint8_t>((f_.c ? 0x80 : 0) | (af_.high >> 1));
    f_.qTemp = 0;
    f_.yxValue = res;
    f_.hValue = 0;
    f_.n = false;
    f_.c = (af_.high & 0x01) != 0;
    af_.high = res;
}

void MameDerivedZ80Adapter::OpAddA(std::uint8_t value) {
    std::uint8_t a = af_.high;
    std::uint16_t res = static_cast<std::uint16_t>(a + value);
    f_.qTemp = 0;
    f_.sValue = f_.zValue = f_.yxValue = static_cast<std::uint8_t>(res);
    f_.c = (res & 0x100) != 0;
    f_.hValue = static_cast<std::uint8_t>((a & 0x0F) + (value & 0x0F));
    f_.pvValue = static_cast<std::uint8_t>(!(((a ^ res) & (value ^ res)) & 0x80));
    f_.n = false;
    af_.high = static_cast<std::uint8_t>(res);
}

void MameDerivedZ80Adapter::OpAdcA(std::uint8_t value) {
    std::uint8_t a = af_.high;
    int c = f_.c ? 1 : 0;
    std::uint16_t res = static_cast<std::uint16_t>(a + value + c);
    f_.qTemp = 0;
    f_.sValue = f_.zValue = f_.yxValue = static_cast<std::uint8_t>(res);
    f_.c = (res & 0x100) != 0;
    f_.hValue = static_cast<std::uint8_t>((a & 0x0F) + (value & 0x0F) + c);
    f_.pvValue = static_cast<std::uint8_t>(!(((a ^ res) & (value ^ res)) & 0x80));
    f_.n = false;
    af_.high = static_cast<std::uint8_t>(res);
}

void MameDerivedZ80Adapter::OpSubA(std::uint8_t value) {
    std::uint8_t a = af_.high;
    std::uint16_t res = static_cast<std::uint16_t>(a - value);
    f_.qTemp = 0;
    f_.sValue = f_.zValue = f_.yxValue = static_cast<std::uint8_t>(res);
    f_.c = (res & 0x100) != 0;
    f_.hValue = static_cast<std::uint8_t>((a & 0x0F) - (value & 0x0F));
    f_.pvValue = static_cast<std::uint8_t>(!(((a ^ value) & (a ^ res)) & 0x80));
    f_.n = true;
    af_.high = static_cast<std::uint8_t>(res);
}

void MameDerivedZ80Adapter::OpSbcA(std::uint8_t value) {
    std::uint8_t a = af_.high;
    int c = f_.c ? 1 : 0;
    std::uint16_t res = static_cast<std::uint16_t>(a - value - c);
    f_.qTemp = 0;
    f_.sValue = f_.zValue = f_.yxValue = static_cast<std::uint8_t>(res);
    f_.c = (res & 0x100) != 0;
    f_.hValue = static_cast<std::uint8_t>((a & 0x0F) - (value & 0x0F) - c);
    f_.pvValue = static_cast<std::uint8_t>(!(((a ^ value) & (a ^ res)) & 0x80));
    f_.n = true;
    af_.high = static_cast<std::uint8_t>(res);
}

void MameDerivedZ80Adapter::OpNeg() {
    std::uint8_t value = af_.high;
    af_.high = 0;
    OpSubA(value);
}

void MameDerivedZ80Adapter::OpDaa() {
    std::uint8_t a = af_.high;
    std::uint8_t adjusted = a;
    if (f_.n) {
        if (f_.H() || ((a & 0x0F) > 9)) adjusted = static_cast<std::uint8_t>(adjusted - 6);
        if (f_.c || (a > 0x99)) adjusted = static_cast<std::uint8_t>(adjusted - 0x60);
    } else {
        if (f_.H() || ((a & 0x0F) > 9)) adjusted = static_cast<std::uint8_t>(adjusted + 6);
        if (f_.c || (a > 0x99)) adjusted = static_cast<std::uint8_t>(adjusted + 0x60);
    }
    f_.qTemp = 0;
    f_.sValue = f_.zValue = f_.pvValue = f_.yxValue = adjusted;
    f_.hValue = static_cast<std::uint8_t>(a ^ adjusted);
    f_.c = f_.c || (a > 0x99);
    af_.high = adjusted;
}

void MameDerivedZ80Adapter::OpAndA(std::uint8_t value) {
    af_.high = static_cast<std::uint8_t>(af_.high & value);
    f_.qTemp = 0;
    f_.sValue = f_.zValue = f_.pvValue = f_.yxValue = af_.high;
    f_.n = false;
    f_.c = false;
    f_.hValue = kZ80FlagH;
}

void MameDerivedZ80Adapter::OpOrA(std::uint8_t value) {
    af_.high = static_cast<std::uint8_t>(af_.high | value);
    f_.qTemp = 0;
    f_.sValue = f_.zValue = f_.pvValue = f_.yxValue = af_.high;
    f_.hValue = 0;
    f_.n = false;
    f_.c = false;
}

void MameDerivedZ80Adapter::OpXorA(std::uint8_t value) {
    af_.high = static_cast<std::uint8_t>(af_.high ^ value);
    f_.qTemp = 0;
    f_.sValue = f_.zValue = f_.pvValue = f_.yxValue = af_.high;
    f_.hValue = 0;
    f_.n = false;
    f_.c = false;
}

void MameDerivedZ80Adapter::OpCp(std::uint8_t value) {
    std::uint8_t a = af_.high;
    std::uint16_t res = static_cast<std::uint16_t>(a - value);
    f_.qTemp = 0;
    f_.sValue = f_.zValue = static_cast<std::uint8_t>(res);
    f_.yxValue = value;
    f_.c = (res & 0x100) != 0;
    f_.hValue = static_cast<std::uint8_t>((a & 0x0F) - (value & 0x0F));
    f_.pvValue = static_cast<std::uint8_t>(!(((a ^ value) & (a ^ res)) & 0x80));
    f_.n = true;
}

void MameDerivedZ80Adapter::OpExx() {
    std::swap(bc_, bc2_);
    std::swap(de_, de2_);
    std::swap(hl_, hl2_);
}

std::uint8_t MameDerivedZ80Adapter::OpRlc(std::uint8_t value) {
    std::uint8_t res = static_cast<std::uint8_t>((value << 1) | (value >> 7));
    f_.qTemp = 0;
    f_.sValue = f_.zValue = f_.pvValue = f_.yxValue = res;
    f_.hValue = 0;
    f_.n = false;
    f_.c = (value & 0x80) != 0;
    return res;
}

std::uint8_t MameDerivedZ80Adapter::OpRrc(std::uint8_t value) {
    std::uint8_t res = static_cast<std::uint8_t>((value >> 1) | (value << 7));
    f_.qTemp = 0;
    f_.sValue = f_.zValue = f_.pvValue = f_.yxValue = res;
    f_.hValue = 0;
    f_.n = false;
    f_.c = (value & 0x01) != 0;
    return res;
}

std::uint8_t MameDerivedZ80Adapter::OpRl(std::uint8_t value) {
    std::uint8_t res = static_cast<std::uint8_t>((value << 1) + (f_.c ? 1 : 0));
    f_.qTemp = 0;
    f_.sValue = f_.zValue = f_.pvValue = f_.yxValue = res;
    f_.hValue = 0;
    f_.n = false;
    f_.c = (value & 0x80) != 0;
    return res;
}

std::uint8_t MameDerivedZ80Adapter::OpRr(std::uint8_t value) {
    std::uint8_t res = static_cast<std::uint8_t>((value >> 1) | (f_.c ? 0x80 : 0));
    f_.qTemp = 0;
    f_.sValue = f_.zValue = f_.pvValue = f_.yxValue = res;
    f_.hValue = 0;
    f_.n = false;
    f_.c = (value & 0x01) != 0;
    return res;
}

std::uint8_t MameDerivedZ80Adapter::OpSla(std::uint8_t value) {
    std::uint8_t res = static_cast<std::uint8_t>(value << 1);
    f_.qTemp = 0;
    f_.sValue = f_.zValue = f_.pvValue = f_.yxValue = res;
    f_.hValue = 0;
    f_.n = false;
    f_.c = (value & 0x80) != 0;
    return res;
}

std::uint8_t MameDerivedZ80Adapter::OpSra(std::uint8_t value) {
    std::uint8_t res = static_cast<std::uint8_t>((value >> 1) | (value & 0x80));
    f_.qTemp = 0;
    f_.sValue = f_.zValue = f_.pvValue = f_.yxValue = res;
    f_.hValue = 0;
    f_.n = false;
    f_.c = (value & 0x01) != 0;
    return res;
}

std::uint8_t MameDerivedZ80Adapter::OpSll(std::uint8_t value) {
    std::uint8_t res = static_cast<std::uint8_t>((value << 1) | 0x01);
    f_.qTemp = 0;
    f_.sValue = f_.zValue = f_.pvValue = f_.yxValue = res;
    f_.hValue = 0;
    f_.n = false;
    f_.c = (value & 0x80) != 0;
    return res;
}

std::uint8_t MameDerivedZ80Adapter::OpSrl(std::uint8_t value) {
    std::uint8_t res = static_cast<std::uint8_t>(value >> 1);
    f_.qTemp = 0;
    f_.sValue = f_.zValue = f_.pvValue = f_.yxValue = res;
    f_.hValue = 0;
    f_.n = false;
    f_.c = (value & 0x01) != 0;
    return res;
}

void MameDerivedZ80Adapter::OpBit(int bit, std::uint8_t value) {
    f_.qTemp = 0;
    f_.sValue = f_.zValue = f_.pvValue = static_cast<std::uint8_t>(value & (1 << bit));
    f_.hValue = kZ80FlagH;
    f_.n = false;
    f_.yxValue = value;  // simplified vs MAME's bit_hl/bit_xy (the undocumented Y/X source differs)
}

void MameDerivedZ80Adapter::OpAddHl(Z80RegisterPair& hlReg, std::uint16_t value) {
    std::uint16_t hl = hlReg.Word();
    std::uint32_t res = static_cast<std::uint32_t>(hl) + value;
    f_.qTemp = kZ80FlagYX;
    f_.yxValue = static_cast<std::uint8_t>(res >> 8);
    f_.hValue = static_cast<std::uint8_t>(((hl & 0x0FFF) + (value & 0x0FFF)) >> 8);
    f_.c = (res & 0x10000) != 0;
    f_.n = false;
    hlReg = static_cast<std::uint16_t>(res);
}

void MameDerivedZ80Adapter::OpAdcHl(std::uint16_t value) {
    std::uint16_t hl = hl_.Word();
    int c = f_.c ? 1 : 0;
    std::uint32_t res = static_cast<std::uint32_t>(hl) + value + static_cast<std::uint32_t>(c);
    f_.qTemp = kZ80FlagYX;
    f_.sValue = f_.yxValue = static_cast<std::uint8_t>(res >> 8);
    // Z is set iff the whole 16-bit result is zero; collapse to 0/1 because
    // a plain uint8 cast would drop the high byte.
    f_.zValue = static_cast<std::uint8_t>((res & 0xFFFF) != 0);
    f_.hValue = static_cast<std::uint8_t>(((hl & 0x0FFF) + (value & 0x0FFF) + static_cast<unsigned>(c)) >> 8);
    f_.c = (res & 0x10000) != 0;
    f_.pvValue = static_cast<std::uint8_t>(
        !(((hl ^ static_cast<std::uint16_t>(res)) & (value ^ static_cast<std::uint16_t>(res))) & 0x8000));
    f_.n = false;
    hl_ = static_cast<std::uint16_t>(res);
}

void MameDerivedZ80Adapter::OpSbcHl(std::uint16_t value) {
    std::uint16_t hl = hl_.Word();
    int c = f_.c ? 1 : 0;
    std::uint32_t res = static_cast<std::uint32_t>(hl) - value - static_cast<std::uint32_t>(c);
    f_.qTemp = kZ80FlagYX;
    f_.sValue = f_.yxValue = static_cast<std::uint8_t>(res >> 8);
    // Z is set iff the whole 16-bit result is zero; collapse to 0/1 because
    // a plain uint8 cast would drop the high byte (0x0100 would set Z).
    f_.zValue = static_cast<std::uint8_t>((res & 0xFFFF) != 0);
    f_.hValue = static_cast<std::uint8_t>(((hl & 0x0FFF) - (value & 0x0FFF) - static_cast<unsigned>(c)) >> 8);
    f_.c = (res & 0x10000) != 0;
    f_.pvValue = static_cast<std::uint8_t>(
        !(((hl ^ value) & (hl ^ static_cast<std::uint16_t>(res))) & 0x8000));
    f_.n = true;
    hl_ = static_cast<std::uint16_t>(res);
}

// ---------------------------------------------------------------------
// Interrupts
// ---------------------------------------------------------------------

bool MameDerivedZ80Adapter::CheckPendingInterrupts() {
    if (nmiPending_) {
        ServiceNmi();
        return true;
    }
    if (irqAsserted_ && iff1_) {
        ServiceIrq();
        return true;
    }
    return false;
}

void MameDerivedZ80Adapter::ServiceNmi() {
    ++nmiServiceCount_;
    nmiPending_ = false;
    halted_ = false;
    iff2_ = iff1_;
    iff1_ = false;
    PushWord(pc_.Word());
    pc_ = 0x0066;
    AddCycles(11);
}

void MameDerivedZ80Adapter::ServiceIrq() {
    ++irqServiceCount_;
    halted_ = false;
    iff1_ = false;
    iff2_ = false;
    switch (im_) {
        case 0: {
            // Matches MAME's take_interrupt: only CALL nnnn/JP nnnn/RST xx/EI
            // are executed; any other injected byte does nothing.
            //
            // T-state totals include 2 base interrupt-latency cycles:
            // CALL=19 (2+11+6 push), JP=12 (2+10), RST=13 (2+5+6 push),
            // EI=6 (2+4), unhandled=2.
            std::uint32_t vector = interruptAck_ ? interruptAck_() : 0xFFu;
            ++im0ServiceCount_;
            if ((vector & 0xFF0000u) == 0xCD0000u) {
                // CALL nnnn - the address bytes come from the acknowledge
                // callback (the data bus), not from program memory.
                PushWord(pc_.Word());
                pc_ = static_cast<std::uint16_t>(vector & 0xFFFFu);
                AddCycles(19);
            } else if ((vector & 0xFF0000u) == 0xC30000u) {
                // JP nnnn - operand supplied the same way as CALL above.
                pc_ = static_cast<std::uint16_t>(vector & 0xFFFFu);
                AddCycles(12);
            } else if ((vector & 0xC7u) == 0xC7u) {
                // RST xx (opcode pattern 11xxx111; only the low byte is
                // examined, as in MAME).
                PushWord(pc_.Word());
                pc_ = static_cast<std::uint16_t>(vector & 0x0038u);
                AddCycles(13);
            } else if (vector == 0xFBu) {
                // EI - same effect (including the one-instruction delay) as
                // the 0xFB opcode.
                iff1_ = iff2_ = true;
                eiDelayActive_ = true;
                AddCycles(6);
            } else {
                // As in MAME, anything else on the bus is ignored: no PC/SP
                // change, just the base acknowledge latency.
                ++im0UnhandledServiceCount_;
                AddCycles(2);
            }
            break;
        }
        case 1: {
            PushWord(pc_.Word());
            pc_ = 0x0038;
            AddCycles(13);
            break;
        }
        default: {  // IM 2
            std::uint32_t vector = interruptAck_ ? interruptAck_() : 0xFFu;
            std::uint8_t vectorLow = static_cast<std::uint8_t>(vector & 0xFFu);
            auto vectorAddress = static_cast<std::uint16_t>((static_cast<std::uint16_t>(i_) << 8) | vectorLow);
            std::uint8_t targetLow = ReadMemory(vectorAddress);
            std::uint8_t targetHigh = ReadMemory(static_cast<std::uint16_t>(vectorAddress + 1));
            PushWord(pc_.Word());
            pc_ = static_cast<std::uint16_t>((static_cast<std::uint16_t>(targetHigh) << 8) | targetLow);
            AddCycles(19);
            break;
        }
    }
}

// ---------------------------------------------------------------------
// Execute()
// ---------------------------------------------------------------------

std::uint64_t MameDerivedZ80Adapter::Execute(std::uint64_t cycleBudget) {
    cyclesConsumedThisCall_ = 0;

    while (cyclesConsumedThisCall_ < cycleBudget) {
        bool suppressInterruptCheck = eiDelayActive_;
        eiDelayActive_ = false;
        if (!suppressInterruptCheck && CheckPendingInterrupts()) {
            // A serviced interrupt replaces the next instruction fetch.
            continue;
        }

        if (halted_) {
            AddCycles(4);
            r_ = static_cast<std::uint8_t>((r_ + 1) & 0x7F);
            continue;
        }

        std::uint8_t opcode = FetchOpcode();

        switch (opcode) {
            case 0xCB:
                ExecuteCb(hl_, false);
                break;
            case 0xED: {
                std::uint8_t sub = FetchOpcode();
                ExecuteEd(sub);
                break;
            }
            case 0xDD:
            case 0xFD: {
                Z80RegisterPair* indexReg = (opcode == 0xDD) ? &ix_ : &iy_;
                AddCycles(4);
                std::uint8_t next = FetchOpcode();
                while (next == 0xDD || next == 0xFD) {
                    indexReg = (next == 0xDD) ? &ix_ : &iy_;
                    AddCycles(4);
                    next = FetchOpcode();
                }
                if (next == 0xCB) {
                    ExecuteCb(*indexReg, true);
                } else if (next == 0xED) {
                    std::uint8_t sub = FetchOpcode();
                    ExecuteEd(sub);
                } else {
                    ExecuteUnprefixed(next, *indexReg, true);
                }
                break;
            }
            default:
                ExecuteUnprefixed(opcode, hl_, false);
                break;
        }
    }

    return cyclesConsumedThisCall_;
}

// ---------------------------------------------------------------------
// Unprefixed opcode dispatch (also serves the DD/FD-prefixed opcode set,
// with `hlReg`/`useIndexedHl` substituting IX/IY for HL).
//
// Decoded via the standard Z80 opcode bitfield layout (opcode = xxyyyzzz,
// with p = y>>1 and q = y&1 for the 16-bit-register groups).
// ---------------------------------------------------------------------

void MameDerivedZ80Adapter::ExecuteUnprefixed(std::uint8_t opcode, Z80RegisterPair& hlReg, bool useIndexedHl) {
    const int x = opcode >> 6;
    const int y = (opcode >> 3) & 0x07;
    const int z = opcode & 0x07;
    const int p = y >> 1;
    const int q = y & 0x01;

    auto resolveIndirectHlAddress = [&]() -> std::uint16_t {
        std::uint16_t addr = hlReg.Word();
        if (useIndexedHl) {
            auto d = static_cast<std::int8_t>(FetchByte());
            addr = static_cast<std::uint16_t>(addr + d);
            wz_ = addr;
        }
        return addr;
    };

    auto getR = [&](int idx) -> std::uint8_t {
        switch (idx) {
            case kRegB: return bc_.high;
            case kRegC: return bc_.low;
            case kRegD: return de_.high;
            case kRegE: return de_.low;
            case kRegH: return hlReg.high;
            case kRegL: return hlReg.low;
            case kRegIndirectHl: return ReadMemory(resolveIndirectHlAddress());
            default: return af_.high;  // kRegA
        }
    };
    auto setR = [&](int idx, std::uint8_t value) {
        switch (idx) {
            case kRegB: bc_.high = value; break;
            case kRegC: bc_.low = value; break;
            case kRegD: de_.high = value; break;
            case kRegE: de_.low = value; break;
            case kRegH: hlReg.high = value; break;
            case kRegL: hlReg.low = value; break;
            case kRegIndirectHl: WriteMemory(resolveIndirectHlAddress(), value); break;
            default: af_.high = value; break;  // kRegA
        }
    };
    auto getRp = [&](int idx) -> Z80RegisterPair& {
        switch (idx) {
            case 0: return bc_;
            case 1: return de_;
            case 2: return hlReg;
            default: return sp_;
        }
    };
    auto getRp2 = [&](int idx) -> Z80RegisterPair& {  // PUSH/POP table: BC,DE,HL,AF
        switch (idx) {
            case 0: return bc_;
            case 1: return de_;
            case 2: return hlReg;
            default: return af_;
        }
    };
    auto applyAlu = [&](int op, std::uint8_t value) {
        switch (op) {
            case 0: OpAddA(value); break;
            case 1: OpAdcA(value); break;
            case 2: OpSubA(value); break;
            case 3: OpSbcA(value); break;
            case 4: OpAndA(value); break;
            case 5: OpXorA(value); break;
            case 6: OpOrA(value); break;
            default: OpCp(value); break;
        }
    };

    switch (x) {
        case 0: {
            switch (z) {
                case 0: {
                    if (y == 0) {
                        AddCycles(4);  // NOP
                    } else if (y == 1) {
                        std::uint8_t currentF = GetF();
                        std::swap(af_.high, af2_.high);
                        std::uint8_t shadowF = af2_.low;
                        af2_.low = currentF;
                        SetF(shadowF);
                        AddCycles(4);  // EX AF,AF'
                    } else if (y == 2) {
                        // DJNZ decrements B without touching any flag (unlike DEC B).
                        bc_.high = static_cast<std::uint8_t>(bc_.high - 1);
                        auto d = static_cast<std::int8_t>(FetchByte());
                        if (bc_.high != 0) {
                            JumpRelative(d);
                            AddCycles(13);
                        } else {
                            AddCycles(8);
                        }
                    } else if (y == 3) {
                        auto d = static_cast<std::int8_t>(FetchByte());
                        JumpRelative(d);
                        AddCycles(12);
                    } else {
                        auto d = static_cast<std::int8_t>(FetchByte());
                        if (TestCondition(y - 4)) {
                            JumpRelative(d);
                            AddCycles(12);
                        } else {
                            AddCycles(7);
                        }
                    }
                    break;
                }
                case 1: {
                    if (q == 0) {
                        getRp(p) = FetchWord();
                        AddCycles(10);
                    } else {
                        OpAddHl(hlReg, getRp(p).Word());
                        AddCycles(11);
                    }
                    break;
                }
                case 2: {
                    switch (y) {
                        case 0: WriteMemory(bc_.Word(), af_.high); AddCycles(7); break;
                        case 1: af_.high = ReadMemory(bc_.Word()); AddCycles(7); break;
                        case 2: WriteMemory(de_.Word(), af_.high); AddCycles(7); break;
                        case 3: af_.high = ReadMemory(de_.Word()); AddCycles(7); break;
                        case 4: {
                            std::uint16_t addr = FetchWord();
                            WriteMemory(addr, hlReg.low);
                            WriteMemory(static_cast<std::uint16_t>(addr + 1), hlReg.high);
                            AddCycles(16);
                            break;
                        }
                        case 5: {
                            std::uint16_t addr = FetchWord();
                            hlReg.low = ReadMemory(addr);
                            hlReg.high = ReadMemory(static_cast<std::uint16_t>(addr + 1));
                            AddCycles(16);
                            break;
                        }
                        case 6: {
                            std::uint16_t addr = FetchWord();
                            WriteMemory(addr, af_.high);
                            AddCycles(13);
                            break;
                        }
                        default: {
                            std::uint16_t addr = FetchWord();
                            af_.high = ReadMemory(addr);
                            AddCycles(13);
                            break;
                        }
                    }
                    break;
                }
                case 3: {
                    if (q == 0) {
                        ++getRp(p);
                    } else {
                        --getRp(p);
                    }
                    AddCycles(6);
                    break;
                }
                case 4: {
                    if (y == kRegIndirectHl) {
                        std::uint16_t addr = resolveIndirectHlAddress();
                        std::uint8_t v = ReadMemory(addr);
                        OpInc(v);
                        WriteMemory(addr, v);
                        AddCycles(useIndexedHl ? 19 : 11);
                    } else {
                        std::uint8_t v = getR(y);
                        OpInc(v);
                        setR(y, v);
                        AddCycles(4);
                    }
                    break;
                }
                case 5: {
                    if (y == kRegIndirectHl) {
                        std::uint16_t addr = resolveIndirectHlAddress();
                        std::uint8_t v = ReadMemory(addr);
                        OpDec(v);
                        WriteMemory(addr, v);
                        AddCycles(useIndexedHl ? 19 : 11);
                    } else {
                        std::uint8_t v = getR(y);
                        OpDec(v);
                        setR(y, v);
                        AddCycles(4);
                    }
                    break;
                }
                case 6: {
                    if (y == kRegIndirectHl) {
                        std::uint16_t addr = resolveIndirectHlAddress();
                        WriteMemory(addr, FetchByte());
                        AddCycles(useIndexedHl ? 15 : 10);
                    } else {
                        setR(y, FetchByte());
                        AddCycles(7);
                    }
                    break;
                }
                default: {  // z == 7
                    switch (y) {
                        case 0: OpRlca(); break;
                        case 1: OpRrca(); break;
                        case 2: OpRla(); break;
                        case 3: OpRra(); break;
                        case 4: OpDaa(); break;
                        case 5:
                            af_.high = static_cast<std::uint8_t>(~af_.high);
                            f_.hValue = kZ80FlagH;
                            f_.n = true;
                            f_.yxValue = af_.high;
                            break;
                        case 6:
                            f_.c = true;
                            f_.hValue = 0;
                            f_.n = false;
                            f_.yxValue = af_.high;
                            break;
                        default:
                            f_.c = !f_.c;
                            f_.hValue = 0;
                            f_.n = false;
                            f_.yxValue = af_.high;
                            break;
                    }
                    AddCycles(4);
                    break;
                }
            }
            break;
        }
        case 1: {  // LD r,r' (or HALT at y=6,z=6)
            if (y == kRegIndirectHl && z == kRegIndirectHl) {
                halted_ = true;
                AddCycles(4);
            } else if (y == kRegIndirectHl || z == kRegIndirectHl) {
                // LD r,(IX+d) / LD (IX+d),r: under a DD/FD prefix only the
                // (HL) slot becomes (IX+d); the register operand r stays the
                // real B/C/D/E/H/L/A (H/L are never IXH/IXL here). The
                // reg-reg forms (LD IXH,IXL etc.) keep the substitution and
                // are handled by the else branch.
                if (z == kRegIndirectHl) {
                    std::uint8_t v = getR(z);  // (IX+d) read; consumes the displacement
                    if (y == kRegH) {
                        hl_.high = v;
                    } else if (y == kRegL) {
                        hl_.low = v;
                    } else {
                        setR(y, v);
                    }
                } else {
                    std::uint8_t v = (z == kRegH) ? hl_.high : (z == kRegL) ? hl_.low : getR(z);
                    setR(y, v);  // (IX+d) write; consumes the displacement
                }
                AddCycles(useIndexedHl ? 15 : 7);
            } else {
                setR(y, getR(z));
                AddCycles(4);
            }
            break;
        }
        case 2: {  // ALU[y] r[z]
            std::uint8_t value = getR(z);
            applyAlu(y, value);
            AddCycles(z == kRegIndirectHl ? (useIndexedHl ? 15 : 7) : 4);
            break;
        }
        default: {  // x == 3
            switch (z) {
                case 0: {
                    if (TestCondition(y)) {
                        pc_ = PopWord();
                        wz_ = pc_.Word();
                        AddCycles(11);
                    } else {
                        AddCycles(5);
                    }
                    break;
                }
                case 1: {
                    if (q == 0) {
                        getRp2(p) = PopWord();
                        if (p == 3) {
                            SetF(static_cast<std::uint8_t>(af_.Word() & 0xFF));
                        }
                        AddCycles(10);
                    } else {
                        switch (p) {
                            case 0: pc_ = PopWord(); AddCycles(10); break;
                            case 1: OpExx(); AddCycles(4); break;
                            case 2: pc_ = hlReg.Word(); AddCycles(4); break;
                            default: sp_ = hlReg.Word(); AddCycles(6); break;
                        }
                    }
                    break;
                }
                case 2: {
                    std::uint16_t addr = FetchWord();
                    wz_ = addr;
                    if (TestCondition(y)) {
                        pc_ = addr;
                    }
                    AddCycles(10);
                    break;
                }
                case 3: {
                    switch (y) {
                        case 0: {
                            std::uint16_t addr = FetchWord();
                            wz_ = addr;
                            pc_ = addr;
                            AddCycles(10);
                            break;
                        }
                        case 1:
                            // 0xCB is handled by the caller before reaching here.
                            break;
                        case 2: {
                            std::uint8_t port = FetchByte();
                            wz_ = static_cast<std::uint16_t>(((af_.high << 8) | port) + 1);
                            if (ioWrite_) ioWrite_(port, af_.high);
                            AddCycles(11);
                            break;
                        }
                        case 3: {
                            std::uint8_t port = FetchByte();
                            wz_ = static_cast<std::uint16_t>(((af_.high << 8) | port) + 1);
                            af_.high = ioRead_ ? ioRead_(port) : 0xFF;
                            AddCycles(11);
                            break;
                        }
                        case 4: {
                            std::uint16_t spWord = sp_.Word();
                            std::uint8_t lo = ReadMemory(spWord);
                            std::uint8_t hi = ReadMemory(static_cast<std::uint16_t>(spWord + 1));
                            WriteMemory(spWord, hlReg.low);
                            WriteMemory(static_cast<std::uint16_t>(spWord + 1), hlReg.high);
                            hlReg.low = lo;
                            hlReg.high = hi;
                            wz_ = hlReg.Word();
                            AddCycles(useIndexedHl ? 23 : 19);
                            break;
                        }
                        case 5: std::swap(de_, hlReg); AddCycles(4); break;
                        case 6: iff1_ = iff2_ = false; AddCycles(4); break;
                        default: iff1_ = iff2_ = true; eiDelayActive_ = true; AddCycles(4); break;
                    }
                    break;
                }
                case 4: {
                    std::uint16_t addr = FetchWord();
                    wz_ = addr;
                    if (TestCondition(y)) {
                        PushWord(pc_.Word());
                        pc_ = addr;
                        AddCycles(17);
                    } else {
                        AddCycles(10);
                    }
                    break;
                }
                case 5: {
                    if (q == 0) {
                        // PUSH AF must first sync af_.low from the lazily
                        // evaluated flags; ordinary flag-setting instructions
                        // only write into f_, so af_.low would be stale.
                        if (p == 3) af_.low = GetF();
                        PushWord(getRp2(p).Word());
                        AddCycles(11);
                    } else {
                        switch (p) {
                            case 0: {
                                std::uint16_t addr = FetchWord();
                                wz_ = addr;
                                PushWord(pc_.Word());
                                pc_ = addr;
                                AddCycles(17);
                                break;
                            }
                            default:
                                // 0xDD/0xED/0xFD are handled by the caller before
                                // reaching here.
                                break;
                        }
                    }
                    break;
                }
                case 6: {
                    std::uint8_t value = FetchByte();
                    applyAlu(y, value);
                    AddCycles(7);
                    break;
                }
                default: {  // z == 7, RST y*8
                    PushWord(pc_.Word());
                    pc_ = static_cast<std::uint16_t>(y * 8);
                    wz_ = pc_.Word();
                    AddCycles(11);
                    break;
                }
            }
            break;
        }
    }
}

// ---------------------------------------------------------------------
// CB-prefixed dispatch (rotate/shift/BIT/RES/SET). Also serves DD CB
// d/FD CB d (indexed bit ops), which operate only on the (index+d) memory
// location, never on the register aliased by the opcode's low 3 bits.
// ---------------------------------------------------------------------

void MameDerivedZ80Adapter::ExecuteCb(Z80RegisterPair& hlReg, bool useIndexedHl) {
    std::uint16_t indexedAddress = 0;
    if (useIndexedHl) {
        auto d = static_cast<std::int8_t>(FetchByte());
        indexedAddress = static_cast<std::uint16_t>(hlReg.Word() + d);
        wz_ = indexedAddress;
    }
    std::uint8_t opcode = FetchOpcode();

    const int x = opcode >> 6;
    const int y = (opcode >> 3) & 0x07;
    const int z = opcode & 0x07;
    const bool isMemoryOperand = useIndexedHl || (z == kRegIndirectHl);
    const std::uint16_t address = useIndexedHl ? indexedAddress : hlReg.Word();

    auto readOperand = [&]() -> std::uint8_t {
        if (isMemoryOperand) return ReadMemory(address);
        switch (z) {
            case kRegB: return bc_.high;
            case kRegC: return bc_.low;
            case kRegD: return de_.high;
            case kRegE: return de_.low;
            case kRegH: return hlReg.high;
            case kRegL: return hlReg.low;
            default: return af_.high;  // kRegA
        }
    };
    auto writeOperand = [&](std::uint8_t value) {
        if (useIndexedHl) {
            WriteMemory(address, value);
            return;
        }
        switch (z) {
            case kRegB: bc_.high = value; break;
            case kRegC: bc_.low = value; break;
            case kRegD: de_.high = value; break;
            case kRegE: de_.low = value; break;
            case kRegH: hlReg.high = value; break;
            case kRegL: hlReg.low = value; break;
            case kRegIndirectHl: WriteMemory(address, value); break;
            default: af_.high = value; break;  // kRegA
        }
    };

    std::uint8_t value = readOperand();

    switch (x) {
        case 0: {
            std::uint8_t result = 0;
            switch (y) {
                case 0: result = OpRlc(value); break;
                case 1: result = OpRrc(value); break;
                case 2: result = OpRl(value); break;
                case 3: result = OpRr(value); break;
                case 4: result = OpSla(value); break;
                case 5: result = OpSra(value); break;
                case 6: result = OpSll(value); break;
                default: result = OpSrl(value); break;
            }
            writeOperand(result);
            AddCycles(isMemoryOperand ? (useIndexedHl ? 19 : 15) : 8);
            break;
        }
        case 1: {
            OpBit(y, value);
            AddCycles(useIndexedHl ? 16 : (z == kRegIndirectHl ? 12 : 8));
            break;
        }
        case 2: {
            writeOperand(static_cast<std::uint8_t>(value & ~(1 << y)));
            AddCycles(isMemoryOperand ? (useIndexedHl ? 19 : 15) : 8);
            break;
        }
        default: {
            writeOperand(static_cast<std::uint8_t>(value | (1 << y)));
            AddCycles(isMemoryOperand ? (useIndexedHl ? 19 : 15) : 8);
            break;
        }
    }
}

// ---------------------------------------------------------------------
// ED-prefixed dispatch: documented instructions live in the 0x40-0x7F
// range plus the sixteen block instructions at 0xA0-0xA3/0xA8-0xAB/
// 0xB0-0xB3/0xB8-0xBB. Everything else under an ED prefix is undocumented
// on real hardware and acts as an 8-cycle no-op here.
// ---------------------------------------------------------------------

void MameDerivedZ80Adapter::ExecuteEd(std::uint8_t opcode) {
    const int x = opcode >> 6;
    const int y = (opcode >> 3) & 0x07;
    const int z = opcode & 0x07;
    const int p = y >> 1;
    const int q = y & 0x01;

    auto getRp = [&](int idx) -> Z80RegisterPair& {
        switch (idx) {
            case 0: return bc_;
            case 1: return de_;
            case 2: return hl_;
            default: return sp_;
        }
    };

    if (x == 1) {
        switch (z) {
            case 0: {  // IN r[y],(C) - y==6 discards the value (still sets flags)
                std::uint8_t value = ioRead_ ? ioRead_(bc_.low) : 0xFF;
                if (y != kRegIndirectHl) {
                    switch (y) {
                        case kRegB: bc_.high = value; break;
                        case kRegC: bc_.low = value; break;
                        case kRegD: de_.high = value; break;
                        case kRegE: de_.low = value; break;
                        case kRegH: hl_.high = value; break;
                        case kRegL: hl_.low = value; break;
                        default: af_.high = value; break;
                    }
                }
                f_.qTemp = 0;
                f_.sValue = f_.zValue = f_.pvValue = f_.yxValue = value;
                f_.hValue = 0;
                f_.n = false;
                AddCycles(12);
                break;
            }
            case 1: {  // OUT (C),r[y] - y==6 outputs 0
                std::uint8_t value = 0;
                switch (y) {
                    case kRegB: value = bc_.high; break;
                    case kRegC: value = bc_.low; break;
                    case kRegD: value = de_.high; break;
                    case kRegE: value = de_.low; break;
                    case kRegH: value = hl_.high; break;
                    case kRegL: value = hl_.low; break;
                    case kRegIndirectHl: value = 0; break;
                    default: value = af_.high; break;
                }
                if (ioWrite_) ioWrite_(bc_.low, value);
                AddCycles(12);
                break;
            }
            case 2: {
                if (q == 0) {
                    OpSbcHl(getRp(p).Word());
                } else {
                    OpAdcHl(getRp(p).Word());
                }
                AddCycles(15);
                break;
            }
            case 3: {
                std::uint16_t addr = FetchWord();
                if (q == 0) {
                    WriteMemory(addr, getRp(p).low);
                    WriteMemory(static_cast<std::uint16_t>(addr + 1), getRp(p).high);
                } else {
                    getRp(p).low = ReadMemory(addr);
                    getRp(p).high = ReadMemory(static_cast<std::uint16_t>(addr + 1));
                }
                AddCycles(20);
                break;
            }
            case 4:
                OpNeg();
                AddCycles(8);
                break;
            case 5:
                pc_ = PopWord();
                if (y == 1) {
                    // RETI - no daisy-chain notification is modeled.
                } else {
                    iff1_ = iff2_;  // RETN
                }
                AddCycles(14);
                break;
            case 6: {
                if (y == 0 || y == 1 || y == 4 || y == 5) im_ = 0;
                else if (y == 2 || y == 6) im_ = 1;
                else im_ = 2;
                AddCycles(8);
                break;
            }
            default: {  // z == 7
                switch (y) {
                    case 0: i_ = af_.high; AddCycles(9); break;  // LD I,A (0xED 0x47)
                    case 1: r_ = af_.high & 0x7F; r2_ = af_.high & 0x80; AddCycles(9); break;  // LD R,A (0xED 0x4F)
                    case 2:
                        // LD A,I (0xED 0x57): y=2 reads the I register.
                        af_.high = i_;
                        f_.qTemp = 0;
                        f_.sValue = f_.zValue = f_.yxValue = af_.high;
                        f_.hValue = 0;
                        f_.n = false;
                        f_.pvValue = static_cast<std::uint8_t>(!iff2_);
                        AddCycles(9);
                        break;
                    case 3:
                        // LD A,R (0xED 0x5F): y=3 reads the R register.
                        af_.high = static_cast<std::uint8_t>((r_ & 0x7F) | (r2_ & 0x80));
                        f_.qTemp = 0;
                        f_.sValue = f_.zValue = f_.yxValue = af_.high;
                        f_.hValue = 0;
                        f_.n = false;
                        f_.pvValue = static_cast<std::uint8_t>(!iff2_);
                        AddCycles(9);
                        break;
                    case 4: {  // RRD
                        std::uint8_t memValue = ReadMemory(hl_.Word());
                        std::uint8_t newMem = static_cast<std::uint8_t>((af_.high << 4) | (memValue >> 4));
                        af_.high = static_cast<std::uint8_t>((af_.high & 0xF0) | (memValue & 0x0F));
                        WriteMemory(hl_.Word(), newMem);
                        f_.qTemp = 0;
                        f_.sValue = f_.zValue = f_.pvValue = f_.yxValue = af_.high;
                        f_.hValue = 0;
                        f_.n = false;
                        AddCycles(18);
                        break;
                    }
                    case 5: {  // RLD
                        std::uint8_t memValue = ReadMemory(hl_.Word());
                        std::uint8_t newMem = static_cast<std::uint8_t>((memValue << 4) | (af_.high & 0x0F));
                        af_.high = static_cast<std::uint8_t>((af_.high & 0xF0) | (memValue >> 4));
                        WriteMemory(hl_.Word(), newMem);
                        f_.qTemp = 0;
                        f_.sValue = f_.zValue = f_.pvValue = f_.yxValue = af_.high;
                        f_.hValue = 0;
                        f_.n = false;
                        AddCycles(18);
                        break;
                    }
                    default:
                        AddCycles(8);  // undocumented NOP (y==6,7)
                        break;
                }
                break;
            }
        }
        return;
    }

    if (x == 2 && y >= 4 && z <= 3) {
        const bool isDecrement = (y == 5 || y == 7);
        const bool isRepeat = (y == 6 || y == 7);
        bool repeatAgain = false;

        switch (z) {
            case 0: {  // LDI/LDD/LDIR/LDDR
                std::uint8_t value = ReadMemory(hl_.Word());
                WriteMemory(de_.Word(), value);
                if (isDecrement) { --hl_; --de_; } else { ++hl_; ++de_; }
                --bc_;
                f_.hValue = 0;
                f_.n = false;
                f_.pvValue = static_cast<std::uint8_t>(bc_.Word() == 0);
                f_.yxValue = value;
                repeatAgain = isRepeat && bc_.Word() != 0;
                break;
            }
            case 1: {  // CPI/CPD/CPIR/CPDR
                std::uint8_t value = ReadMemory(hl_.Word());
                std::uint8_t a = af_.high;
                auto result = static_cast<std::uint8_t>(a - value);
                if (isDecrement) --hl_; else ++hl_;
                --bc_;
                f_.sValue = f_.zValue = result;
                f_.hValue = static_cast<std::uint8_t>((a & 0x0F) - (value & 0x0F));
                f_.n = true;
                f_.pvValue = static_cast<std::uint8_t>(bc_.Word() == 0);
                f_.yxValue = result;
                repeatAgain = isRepeat && bc_.Word() != 0 && result != 0;
                break;
            }
            case 2: {  // INI/IND/INIR/INDR
                std::uint8_t value = ioRead_ ? ioRead_(bc_.low) : 0xFF;
                WriteMemory(hl_.Word(), value);
                if (isDecrement) --hl_; else ++hl_;
                OpDec(bc_.high);
                repeatAgain = isRepeat && bc_.high != 0;
                break;
            }
            default: {  // OUTI/OUTD/OTIR/OTDR
                std::uint8_t value = ReadMemory(hl_.Word());
                if (ioWrite_) ioWrite_(bc_.low, value);
                if (isDecrement) --hl_; else ++hl_;
                OpDec(bc_.high);
                repeatAgain = isRepeat && bc_.high != 0;
                break;
            }
        }

        if (repeatAgain) {
            pc_ -= 2;
            AddCycles(21);
        } else {
            AddCycles(16);
        }
        return;
    }

    AddCycles(8);  // undocumented ED opcode - no-op
}
