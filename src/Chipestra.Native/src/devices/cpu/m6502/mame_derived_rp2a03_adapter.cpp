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
//
// Ported from MAME's m6502_device / rp2a03_core_device. See the class comment in
// mame_derived_rp2a03_adapter.h for the porting approach.

#include "mame_derived_rp2a03_adapter.h"

MameDerivedRp2a03Adapter::MameDerivedRp2a03Adapter() {
    // Reset() reads the reset vector through the memory callbacks, which are not set yet:
    // CALLERS MUST CALL Reset() AGAIN right after SetMemoryCallbacks().
    Reset();
}

void MameDerivedRp2a03Adapter::SetMemoryCallbacks(MemoryReadFn read, MemoryWriteFn write) {
    memoryRead_ = std::move(read);
    memoryWrite_ = std::move(write);
}

std::uint8_t MameDerivedRp2a03Adapter::Read(int address) {
    ++cyclesConsumedThisCall_;
    return memoryRead_ ? memoryRead_(static_cast<std::uint16_t>(address)) : std::uint8_t{0};
}

void MameDerivedRp2a03Adapter::Write(int address, int value) {
    ++cyclesConsumedThisCall_;
    if (memoryWrite_) memoryWrite_(static_cast<std::uint16_t>(address), static_cast<std::uint8_t>(value));
}

// prefetch_start() + the opcode read + prefetch_end() of the original (no SYNC output pin here).
void MameDerivedRp2a03Adapter::Prefetch() {
    npc_ = pc_;
    ir_ = Read(pc_);
    if (nmiPending_ || ((irqState_ || apuIrqState_) && !(p_ & kFlagI))) {
        irqTaken_ = true;
        ir_ = 0x00;  // the interrupt runs as a BRK; pc_ stays on the interrupted instruction
    } else {
        ++pc_;
    }
}

void MameDerivedRp2a03Adapter::SetNz(std::uint8_t v) {
    p_ &= static_cast<std::uint8_t>(~(kFlagZ | kFlagN));
    if (v & 0x80) p_ |= kFlagN;
    if (!v) p_ |= kFlagZ;
}

// ---------------------------------------------------------------------------
// ALU helpers (m6502.cpp). Only the decimal-disabled variants exist on the RP2A03.
// ---------------------------------------------------------------------------

void MameDerivedRp2a03Adapter::DoAdcNd(std::uint8_t val) {
    std::uint16_t sum;
    sum = static_cast<std::uint16_t>(a_ + val + (p_ & kFlagC ? 1 : 0));
    p_ &= static_cast<std::uint8_t>(~(kFlagN | kFlagV | kFlagZ | kFlagC));
    if (!std::uint8_t(sum)) p_ |= kFlagZ;
    else if (std::int8_t(sum) < 0) p_ |= kFlagN;
    if (~(a_ ^ val) & (a_ ^ sum) & 0x80) p_ |= kFlagV;
    if (sum & 0xff00) p_ |= kFlagC;
    a_ = static_cast<std::uint8_t>(sum);
}

void MameDerivedRp2a03Adapter::DoSbcNd(std::uint8_t val) {
    std::uint16_t diff = static_cast<std::uint16_t>(a_ - val - (p_ & kFlagC ? 0 : 1));
    p_ &= static_cast<std::uint8_t>(~(kFlagN | kFlagV | kFlagZ | kFlagC));
    if (!std::uint8_t(diff)) p_ |= kFlagZ;
    else if (std::int8_t(diff) < 0) p_ |= kFlagN;
    if ((a_ ^ val) & (a_ ^ diff) & 0x80) p_ |= kFlagV;
    if (!(diff & 0xff00)) p_ |= kFlagC;
    a_ = static_cast<std::uint8_t>(diff);
}

void MameDerivedRp2a03Adapter::DoArrNd() {
    bool c = (p_ & kFlagC) != 0;
    p_ &= static_cast<std::uint8_t>(~(kFlagN | kFlagZ | kFlagC | kFlagV));
    a_ >>= 1;
    if (c) a_ |= 0x80;
    if (!a_) p_ |= kFlagZ;
    else if (std::int8_t(a_) < 0) p_ |= kFlagN;
    if (a_ & 0x40) p_ |= kFlagV | kFlagC;
    if (a_ & 0x20) p_ ^= kFlagV;
}

void MameDerivedRp2a03Adapter::DoCmp(std::uint8_t val1, std::uint8_t val2) {
    p_ &= static_cast<std::uint8_t>(~(kFlagN | kFlagZ | kFlagC));
    std::uint16_t r = static_cast<std::uint16_t>(val1 - val2);
    if (!r) p_ |= kFlagZ;
    else if (std::int8_t(r) < 0) p_ |= kFlagN;
    if (!(r & 0xff00)) p_ |= kFlagC;
}

void MameDerivedRp2a03Adapter::DoBit(std::uint8_t val) {
    p_ &= static_cast<std::uint8_t>(~(kFlagN | kFlagZ | kFlagV));
    std::uint8_t r = a_ & val;
    if (!r) p_ |= kFlagZ;
    if (val & 0x80) p_ |= kFlagN;
    if (val & 0x40) p_ |= kFlagV;
}

std::uint8_t MameDerivedRp2a03Adapter::DoAsl(std::uint8_t v) {
    p_ &= static_cast<std::uint8_t>(~(kFlagN | kFlagZ | kFlagC));
    std::uint8_t r = static_cast<std::uint8_t>(v << 1);
    if (!r) p_ |= kFlagZ;
    else if (std::int8_t(r) < 0) p_ |= kFlagN;
    if (v & 0x80) p_ |= kFlagC;
    return r;
}

std::uint8_t MameDerivedRp2a03Adapter::DoLsr(std::uint8_t v) {
    p_ &= static_cast<std::uint8_t>(~(kFlagN | kFlagZ | kFlagC));
    if (v & 1) p_ |= kFlagC;
    v >>= 1;
    if (!v) p_ |= kFlagZ;
    return v;
}

std::uint8_t MameDerivedRp2a03Adapter::DoRor(std::uint8_t v) {
    bool c = (p_ & kFlagC) != 0;
    p_ &= static_cast<std::uint8_t>(~(kFlagN | kFlagZ | kFlagC));
    if (v & 1) p_ |= kFlagC;
    v >>= 1;
    if (c) v |= 0x80;
    if (!v) p_ |= kFlagZ;
    else if (std::int8_t(v) < 0) p_ |= kFlagN;
    return v;
}

std::uint8_t MameDerivedRp2a03Adapter::DoRol(std::uint8_t v) {
    bool c = (p_ & kFlagC) != 0;
    p_ &= static_cast<std::uint8_t>(~(kFlagN | kFlagZ | kFlagC));
    if (v & 0x80) p_ |= kFlagC;
    v = static_cast<std::uint8_t>(v << 1);
    if (c) v |= 0x01;
    if (!v) p_ |= kFlagZ;
    else if (std::int8_t(v) < 0) p_ |= kFlagN;
    return v;
}

// ---------------------------------------------------------------------------
// Reset / Execute / input lines
// ---------------------------------------------------------------------------

void MameDerivedRp2a03Adapter::Reset() {
    // MAME sets these once at device start and a machine reset keeps them; Chipestra restores
    // them on every Reset() so a reset machine replays exactly like a freshly built one.
    pc_ = 0;
    npc_ = 0;
    sp_ = 0x0100;
    tmp_ = 0;
    tmp2_ = 0;
    a_ = 0x00;
    x_ = 0x80;
    y_ = 0x00;
    p_ = 0x36;
    ir_ = 0x00;
    nmiState_ = irqState_ = apuIrqState_ = vState_ = false;
    nmiPending_ = false;
    irqTaken_ = false;
    jammed_ = false;
    totalCycles_ = 0;
    nmiServiceCount_ = 0;
    irqServiceCount_ = 0;

    cyclesConsumedThisCall_ = 0;
    Op_reset();
    pendingResetCycles_ = cyclesConsumedThisCall_;
    cyclesConsumedThisCall_ = 0;
}

std::uint64_t MameDerivedRp2a03Adapter::Execute(std::uint64_t cycleBudget) {
    cyclesConsumedThisCall_ = pendingResetCycles_;
    pendingResetCycles_ = 0;
    executing_ = true;
    while (cyclesConsumedThisCall_ < cycleBudget) {
        if (jammed_) {
            cyclesConsumedThisCall_ = cycleBudget;
            break;
        }
        if (opcodeFetchObserver_) opcodeFetchObserver_(npc_, ir_);
        (this->*kOpTable[ir_])();
    }
    executing_ = false;
    const std::uint64_t consumed = cyclesConsumedThisCall_;
    totalCycles_ += consumed;
    return consumed;
}

void MameDerivedRp2a03Adapter::SetInputLine(M6502InputLine line, M6502InputLineState state) {
    const bool asserted = state == M6502InputLineState::Assert;
    switch (line) {
    case M6502InputLine::Irq:
        irqState_ = asserted;
        break;
    case M6502InputLine::ApuIrq:
        apuIrqState_ = asserted;
        break;
    case M6502InputLine::Nmi:
        // An edge at the very moment RESET is released is not accepted.
        if (!nmiState_ && asserted && totalCycles_) nmiPending_ = true;
        nmiState_ = asserted;
        break;
    case M6502InputLine::SetOverflow:
        if (!vState_ && asserted && totalCycles_) p_ |= kFlagV;
        vState_ = asserted;
        break;
    }
}

MameDerivedRp2a03Adapter::RegisterSnapshot MameDerivedRp2a03Adapter::Snapshot() const {
    RegisterSnapshot s;
    s.pc = npc_;
    s.a = a_;
    s.x = x_;
    s.y = y_;
    s.p = p_;
    s.s = static_cast<std::uint8_t>(sp_ & 0xff);
    s.ir = ir_;
    s.nmiLine = nmiState_;
    s.irqLine = irqState_;
    s.apuIrqLine = apuIrqState_;
    s.nmiPending = nmiPending_;
    s.jammed = jammed_;
    s.nmiServiceCount = nmiServiceCount_;
    s.irqServiceCount = irqServiceCount_;
    return s;
}

#include "mame_derived_rp2a03_ops.inc"
