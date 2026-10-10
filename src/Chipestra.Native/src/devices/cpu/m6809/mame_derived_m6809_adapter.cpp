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
//
// Ported from MAME's m6809_base_device (m6809.cpp, m6809.h, m6809inl.h and the
// m6809make.py-generated m6809.hxx dispatcher - mame0289). See the class comment in
// mame_derived_m6809_adapter.h for the porting approach.

#include "mame_derived_m6809_adapter.h"

#include <utility>

MameDerivedM6809Adapter::MameDerivedM6809Adapter() {
    // Reset() runs here although SetMemoryCallbacks() has not been called yet, so the
    // reset-vector read below sees memoryRead_ unset (null-safe fallback 0) and loads 0x0000
    // into pc_. CALLERS MUST CALL Reset() AGAIN immediately after SetMemoryCallbacks(), since
    // the reset vector of this core is read from memory.
    Reset();
}

void MameDerivedM6809Adapter::SetMemoryCallbacks(MemoryReadFn read, MemoryWriteFn write) {
    memoryRead_ = std::move(read);
    memoryWrite_ = std::move(write);
}

// ---------------------------------------------------------------------------
// Bus access (m6809inl.h: eat()/read_memory()/write_memory()/read_opcode()/read_opcode_arg())
// ---------------------------------------------------------------------------

std::uint8_t MameDerivedM6809Adapter::ReadMemory(std::uint16_t address) {
    Eat(1);
    return memoryRead_ ? memoryRead_(address) : 0;
}

void MameDerivedM6809Adapter::WriteMemory(std::uint16_t address, std::uint8_t value) {
    Eat(1);
    if (memoryWrite_) memoryWrite_(address, value);
}

std::uint8_t MameDerivedM6809Adapter::ReadOpcode() {
    Eat(1);
    std::uint8_t value = memoryRead_ ? memoryRead_(pc_) : 0;
    ++pc_;
    return value;
}

std::uint8_t MameDerivedM6809Adapter::ReadOpcodeArg() {
    // MAME's read_opcode_arg() (no explicit address) reads through the "opcode argument" cache and
    // bumps PC - functionally identical to ReadOpcode() here since Chipestra has no decrypted-opcode map.
    Eat(1);
    std::uint8_t value = memoryRead_ ? memoryRead_(pc_) : 0;
    ++pc_;
    return value;
}

std::uint8_t MameDerivedM6809Adapter::ReadOpcodeArgAt(std::uint16_t address) {
    Eat(1);
    return memoryRead_ ? memoryRead_(address) : 0;
}

// ---------------------------------------------------------------------------
// Operand access through the current addressing mode (m6809inl.h read_operand()/write_operand())
// ---------------------------------------------------------------------------

std::uint8_t MameDerivedM6809Adapter::ReadOperand() {
    switch (mode_) {
        case AddrMode::Ea:
            return ReadMemory(ea_);
        case AddrMode::Immediate:
            return ReadOpcodeArg();
        case AddrMode::RegA:
            return a_;
        case AddrMode::RegB:
            return b_;
    }
    return 0;
}

std::uint8_t MameDerivedM6809Adapter::ReadOperand(int ordinal) {
    switch (mode_) {
        case AddrMode::Ea:
            return ReadMemory(static_cast<std::uint16_t>(ea_ + ordinal));
        case AddrMode::Immediate:
            return ReadOpcodeArg();
        default:
            return 0;  // unreachable for the base-6809 opcode table (see class comment)
    }
}

void MameDerivedM6809Adapter::WriteOperand(std::uint8_t data) {
    switch (mode_) {
        case AddrMode::Immediate:
            break;  // do nothing
        case AddrMode::Ea:
            WriteMemory(ea_, data);
            break;
        case AddrMode::RegA:
            a_ = data;
            break;
        case AddrMode::RegB:
            b_ = data;
            break;
    }
}

void MameDerivedM6809Adapter::WriteOperand(int ordinal, std::uint8_t data) {
    switch (mode_) {
        case AddrMode::Immediate:
            break;  // do nothing
        case AddrMode::Ea:
            WriteMemory(static_cast<std::uint16_t>(ea_ + ordinal), data);
            break;
        default:
            break;  // unreachable for the base-6809 opcode table
    }
}

std::uint8_t MameDerivedM6809Adapter::RegOp8() const {
    switch (regOp8_) {
        case Reg8Id::A:
            return a_;
        case Reg8Id::B:
            return b_;
        case Reg8Id::None:
            return 0;
    }
    return 0;
}

void MameDerivedM6809Adapter::SetRegOp8Value(std::uint8_t v) {
    switch (regOp8_) {
        case Reg8Id::A:
            a_ = v;
            break;
        case Reg8Id::B:
            b_ = v;
            break;
        case Reg8Id::None:
            break;
    }
}

std::uint16_t MameDerivedM6809Adapter::RegOp16() const {
    switch (regOp16_) {
        case Reg16Id::X:
            return x_;
        case Reg16Id::Y:
            return y_;
        case Reg16Id::U:
            return u_;
        case Reg16Id::S:
            return s_;
        case Reg16Id::D:
            return static_cast<std::uint16_t>((static_cast<std::uint16_t>(a_) << 8) | b_);
        case Reg16Id::None:
            return 0;
    }
    return 0;
}

void MameDerivedM6809Adapter::SetRegOp16Value(std::uint16_t v) {
    switch (regOp16_) {
        case Reg16Id::X:
            x_ = v;
            break;
        case Reg16Id::Y:
            y_ = v;
            break;
        case Reg16Id::U:
            u_ = v;
            break;
        case Reg16Id::S:
            s_ = v;
            break;
        case Reg16Id::D:
            a_ = static_cast<std::uint8_t>(v >> 8);
            b_ = static_cast<std::uint8_t>(v & 0xFF);
            break;
        case Reg16Id::None:
            break;
    }
}

// ---------------------------------------------------------------------------
// Flags (m6809inl.h set_flags<T>/rotate_right/rotate_left/daa/mul)
// ---------------------------------------------------------------------------

std::uint8_t MameDerivedM6809Adapter::SetFlags8(std::uint8_t mask, std::uint8_t a, std::uint8_t b, std::uint32_t r) {
    // MAME's set_flags<T> does NOT truncate `r` to T before computing H/N/V (only Z casts
    // explicitly). This matters for V: its `r >> 1` term folds r's bit 8 (the carry/borrow out
    // of bit 7) into bit 7 before the XOR, so truncating r to 8 bits first gives a wrong V
    // (e.g. SUBA/CMPA computing 0x00-0x01 must give V=0). `r` is kept at full 32-bit width.
    constexpr std::uint32_t hiBit = 0x80;
    cc_ &= static_cast<std::uint8_t>(~mask);
    if (mask & CC_H) cc_ |= ((static_cast<std::uint32_t>(a) ^ b ^ r) & 0x10) ? CC_H : 0;
    if (mask & CC_N) cc_ |= (r & hiBit) ? CC_N : 0;
    if (mask & CC_Z) cc_ |= (static_cast<std::uint8_t>(r) == 0) ? CC_Z : 0;
    if (mask & CC_V) cc_ |= ((static_cast<std::uint32_t>(a) ^ b ^ r ^ (r >> 1)) & hiBit) ? CC_V : 0;
    if (mask & CC_C) cc_ |= (r & (hiBit << 1)) ? CC_C : 0;
    return static_cast<std::uint8_t>(r);
}

std::uint16_t MameDerivedM6809Adapter::SetFlags16(std::uint8_t mask, std::uint16_t a, std::uint16_t b, std::uint32_t r) {
    // Same full-width-r requirement as SetFlags8() (CC_H is not requested for 16-bit results
    // by the base-6809 opcode table; the body mirrors MAME's generic template).
    constexpr std::uint32_t hiBit = 0x8000;
    cc_ &= static_cast<std::uint8_t>(~mask);
    if (mask & CC_H) cc_ |= ((static_cast<std::uint32_t>(a) ^ b ^ r) & 0x10) ? CC_H : 0;
    if (mask & CC_N) cc_ |= (r & hiBit) ? CC_N : 0;
    if (mask & CC_Z) cc_ |= (static_cast<std::uint16_t>(r) == 0) ? CC_Z : 0;
    if (mask & CC_V) cc_ |= ((static_cast<std::uint32_t>(a) ^ b ^ r ^ (r >> 1)) & hiBit) ? CC_V : 0;
    if (mask & CC_C) cc_ |= (r & (hiBit << 1)) ? CC_C : 0;
    return static_cast<std::uint16_t>(r);
}

std::uint8_t MameDerivedM6809Adapter::RotateRight8(std::uint8_t value) {
    bool newCarry = (value & 1) != 0;
    value = static_cast<std::uint8_t>(value >> 1);
    if (cc_ & CC_C)
        value |= 0x80;
    else
        value &= 0x7F;
    if (newCarry)
        cc_ |= CC_C;
    else
        cc_ &= static_cast<std::uint8_t>(~CC_C);
    return value;
}

std::uint16_t MameDerivedM6809Adapter::RotateLeft8(std::uint8_t value) {
    bool newCarry = (value & 0x80) != 0;
    std::uint32_t newValue = static_cast<std::uint32_t>(value) << 1;
    if (cc_ & CC_C)
        newValue |= 1;
    else
        newValue &= ~1u;
    if (newCarry)
        cc_ |= CC_C;
    else
        cc_ &= static_cast<std::uint8_t>(~CC_C);
    return static_cast<std::uint16_t>(newValue);
}

void MameDerivedM6809Adapter::Daa() {
    std::uint16_t cf = 0;
    std::uint8_t msn = a_ & 0xF0;
    std::uint8_t lsn = a_ & 0x0F;

    if (lsn > 0x09 || (cc_ & CC_H)) cf |= 0x06;
    if (msn > 0x80 && lsn > 0x09) cf |= 0x60;
    if (msn > 0x90 || (cc_ & CC_C)) cf |= 0x60;

    std::uint16_t t = static_cast<std::uint16_t>(a_ + cf);

    cc_ &= static_cast<std::uint8_t>(~CC_V);
    if (t & 0x0100) cc_ |= CC_C;  // keep carry from the previous operation

    a_ = SetFlags8(CC_NZ, static_cast<std::uint8_t>(t));
}

void MameDerivedM6809Adapter::Mul() {
    std::uint16_t result = static_cast<std::uint16_t>(static_cast<std::uint16_t>(a_) * static_cast<std::uint16_t>(b_));
    std::uint16_t d = SetFlags16(CC_Z, result);
    a_ = static_cast<std::uint8_t>(d >> 8);
    b_ = static_cast<std::uint8_t>(d & 0xFF);
    if (d & 0x0080)
        cc_ |= CC_C;
    else
        cc_ &= static_cast<std::uint8_t>(~CC_C);
}

// ---------------------------------------------------------------------------
// Interrupt registers (m6809inl.h)
// ---------------------------------------------------------------------------

std::uint16_t MameDerivedM6809Adapter::GetPendingInterrupt() const {
    if (nmiAsserted_) return VECTOR_NMI;
    if (!(cc_ & CC_F) && firqLine_) return VECTOR_FIRQ;
    if (!(cc_ & CC_I) && irqLine_) return VECTOR_IRQ;
    return 0;
}

std::uint16_t MameDerivedM6809Adapter::ReadTfrExg816Register(std::uint8_t reg) const {
    switch (reg & 0x0F) {
        case 0:
            return static_cast<std::uint16_t>((static_cast<std::uint16_t>(a_) << 8) | b_);  // D
        case 1:
            return x_;
        case 2:
            return y_;
        case 3:
            return u_;
        case 4:
            return s_;
        case 5:
            return pc_;
        case 8:
            return static_cast<std::uint16_t>(0xFF00 | a_);
        case 9:
            return static_cast<std::uint16_t>(0xFF00 | b_);
        case 10:
            return static_cast<std::uint16_t>((static_cast<std::uint16_t>(cc_) << 8) | cc_);
        case 11:
            return static_cast<std::uint16_t>((static_cast<std::uint16_t>(dp_) << 8) | dp_);
        default:
            return 0xFFFF;
    }
}

std::uint16_t MameDerivedM6809Adapter::ReadExg168Register(std::uint8_t reg) const {
    switch (reg & 0x0F) {
        case 0:
            return static_cast<std::uint16_t>((static_cast<std::uint16_t>(a_) << 8) | b_);  // D
        case 1:
            return x_;
        case 2:
            return y_;
        case 3:
            return u_;
        case 4:
            return s_;
        case 5:
            return pc_;
        case 8:
            return static_cast<std::uint16_t>(0xFF00 | a_);
        case 9:
            return static_cast<std::uint16_t>(0xFF00 | b_);
        case 10:
            return static_cast<std::uint16_t>(0xFF00 | cc_);
        case 11:
            return static_cast<std::uint16_t>(0xFF00 | dp_);
        default:
            return 0xFFFF;
    }
}

void MameDerivedM6809Adapter::WriteExgTfrRegister(std::uint8_t reg, std::uint16_t value) {
    switch (reg & 0x0F) {
        case 0:
            a_ = static_cast<std::uint8_t>(value >> 8);
            b_ = static_cast<std::uint8_t>(value & 0xFF);
            break;
        case 1:
            x_ = value;
            break;
        case 2:
            y_ = value;
            break;
        case 3:
            u_ = value;
            break;
        case 4:
            s_ = value;
            break;
        case 5:
            pc_ = value;
            break;
        case 8:
            a_ = static_cast<std::uint8_t>(value);
            break;
        case 9:
            b_ = static_cast<std::uint8_t>(value);
            break;
        case 10:
            cc_ = static_cast<std::uint8_t>(value);
            break;
        case 11:
            dp_ = static_cast<std::uint8_t>(value);
            break;
        default:
            break;
    }
}

std::uint16_t& MameDerivedM6809Adapter::Ireg() {
    switch (opcode_ & 0x60) {
        case 0x00:
            return x_;
        case 0x20:
            return y_;
        case 0x40:
            return u_;
        default:  // 0x60
            return s_;
    }
}

// ---------------------------------------------------------------------------
// Reset / Execute
// ---------------------------------------------------------------------------

void MameDerivedM6809Adapter::Reset() {
    // m6809_base_device::device_reset() - note CC is NOT cleared here (only I/F are OR'd in), and DP
    // IS cleared. Matches real 6809 reset behavior: other CC bits and every register besides PC/DP
    // survive a reset unless this is the very first Reset() after construction (when every member's
    // own in-class initializer already zeroed it).
    nmiAsserted_ = false;
    ldsEncountered_ = false;
    freeRun_ = false;
    syncWaiting_ = false;
    cwaiWaiting_ = false;

    dp_ = 0x00;
    cc_ |= CC_I;
    cc_ |= CC_F;

    // Read the reset vector directly rather than deferring to the first Execute() call as
    // MAME's state machine does (it queues INTERRUPT_VECTOR). This read does not count
    // against cyclesConsumedThisCall_ (Reset() runs outside any Execute() call).
    std::uint8_t hi = memoryRead_ ? memoryRead_(VECTOR_RESET) : 0;
    std::uint8_t lo = memoryRead_ ? memoryRead_(static_cast<std::uint16_t>(VECTOR_RESET + 1)) : 0;
    pc_ = static_cast<std::uint16_t>((static_cast<std::uint16_t>(hi) << 8) | lo);
    ppc_ = pc_;
}

void MameDerivedM6809Adapter::SetInputLine(M6809InputLine line, M6809InputLineState state) {
    bool asserted = state == M6809InputLineState::Assert;
    switch (line) {
        case M6809InputLine::Nmi:
            // Edge-triggered, AND masked until the first write to S (m_lds_encountered) - exactly
            // MAME's execute_set_input(): "m_nmi_asserted = m_nmi_asserted || ((state != CLEAR_LINE)
            // && !m_nmi_line && m_lds_encountered);"
            nmiAsserted_ = nmiAsserted_ || (asserted && !nmiLine_ && ldsEncountered_);
            nmiLine_ = asserted;
            break;
        case M6809InputLine::Firq:
            firqLine_ = asserted;
            break;
        case M6809InputLine::Irq:
            irqLine_ = asserted;
            break;
    }
}

std::uint64_t MameDerivedM6809Adapter::Execute(std::uint64_t cycleBudget) {
    executing_ = true;
    cyclesConsumedThisCall_ = 0;

    while (cyclesConsumedThisCall_ < cycleBudget) {
        if (freeRun_) {
            // FREERUN: a CPU test mode that increments the address bus until reset - not a real
            // instruction, no Chipestra board is expected to reach it (no game code executes 0x14/0x15/
            // 0xCD deliberately). Modeled as a 1-cycle-per-iteration spin so Execute() still returns
            // control at the requested budget instead of hanging.
            Eat(1);
            continue;
        }

        if (syncWaiting_) {
            // See IM6809Core::Execute()'s own doc comment: SYNC's wait is driven by this outer loop
            // rather than MAME's icount-yield/resume machinery.
            if (GetPendingInterrupt() != 0) {
                syncWaiting_ = false;
                continue;  // re-check via the normal interrupt path below, same call
            }
            Eat(1);
            continue;
        }

        if (cwaiWaiting_) {
            std::uint16_t pending = GetPendingInterrupt();
            if (pending != 0) {
                cwaiWaiting_ = false;
                if (nmiAsserted_) nmiAsserted_ = false;
                SetEa(pending);  // also sets mode_ = Ea, matching MAME's own comment ("need to do this
                                  // to set the addressing mode")
                cc_ |= static_cast<std::uint8_t>(CC_I | (pending != VECTOR_IRQ ? CC_F : 0));
                // CWAI already pushed the entire state before waiting, so this does not go
                // through Service{Nmi,Firq,Irq}() (which would push again): just the vector
                // fetch. The per-vector counters are still updated, so they reflect every
                // actual vector entry.
                if (pending == VECTOR_NMI)
                    ++nmiServiceCount_;
                else if (pending == VECTOR_FIRQ)
                    ++firqServiceCount_;
                else
                    ++irqServiceCount_;
                InterruptVectorFetch();
                continue;
            }
            Eat(1);
            continue;
        }

        std::uint16_t pending = GetPendingInterrupt();
        if (pending == VECTOR_NMI) {
            ServiceNmi();
            continue;
        }
        if (pending == VECTOR_FIRQ) {
            ServiceFirq();
            continue;
        }
        if (pending == VECTOR_IRQ) {
            ServiceIrq();
            continue;
        }

        ppc_ = pc_;
        opcode_ = ReadOpcode();
        if (opcodeFetchObserver_) opcodeFetchObserver_(ppc_, opcode_);
        DispatchPage0(opcode_);
    }

    executing_ = false;
    return cyclesConsumedThisCall_;
}

// ---------------------------------------------------------------------------
// Interrupt entry (NMI/FIRQ/IRQ/INTERRUPT_VECTOR)
// ---------------------------------------------------------------------------

void MameDerivedM6809Adapter::ServiceNmi() {
    nmiAsserted_ = false;
    DummyReadOpcodeArg(0);
    DummyReadOpcodeArg(0);
    DummyVma(1);
    cc_ |= CC_E;
    SetRegOp16(Reg16Id::S);
    temp_ = EntireStateRegisters();
    PushRegisters();
    cc_ |= static_cast<std::uint8_t>(CC_I | CC_F);
    SetEa(VECTOR_NMI);
    ++nmiServiceCount_;
    InterruptVectorFetch();
}

void MameDerivedM6809Adapter::ServiceFirq() {
    DummyReadOpcodeArg(0);
    DummyReadOpcodeArg(0);
    DummyVma(1);
    // firq_saves_entire_state() is always false on base 6809 (only overridden by Konami's custom
    // core in MAME, out of scope here) - FIRQ always pushes the partial state.
    cc_ &= static_cast<std::uint8_t>(~CC_E);
    temp_ = PartialStateRegisters();
    SetRegOp16(Reg16Id::S);
    PushRegisters();
    cc_ |= static_cast<std::uint8_t>(CC_I | CC_F);
    SetEa(VECTOR_FIRQ);
    ++firqServiceCount_;
    InterruptVectorFetch();
}

void MameDerivedM6809Adapter::ServiceIrq() {
    DummyReadOpcodeArg(0);
    DummyReadOpcodeArg(0);
    DummyVma(1);
    cc_ |= CC_E;
    SetRegOp16(Reg16Id::S);
    temp_ = EntireStateRegisters();
    PushRegisters();
    cc_ |= CC_I;
    SetEa(VECTOR_IRQ);
    ++irqServiceCount_;
    InterruptVectorFetch();
}

void MameDerivedM6809Adapter::InterruptVectorFetch() {
    DummyVma(1);
    std::uint8_t hi = ReadVector(0);
    std::uint8_t lo = ReadVector(1);
    pc_ = static_cast<std::uint16_t>((static_cast<std::uint16_t>(hi) << 8) | lo);
    DummyVma(1);
}

// ---------------------------------------------------------------------------
// PUSH_REGISTERS / PULL_REGISTERS
// ---------------------------------------------------------------------------

void MameDerivedM6809Adapter::PushRegisters() {
    std::uint16_t reg = RegOp16();
    auto push16 = [&](std::uint8_t hi, std::uint8_t lo) {
        WriteMemory(--reg, lo);
        WriteMemory(--reg, hi);
    };
    if (temp_ & 0x80) push16(static_cast<std::uint8_t>(pc_ >> 8), static_cast<std::uint8_t>(pc_ & 0xFF));
    if (temp_ & 0x40) {
        // The "other" U/S register (push S's own contents when pushing onto U, and vice versa).
        std::uint16_t other = RegOp16IsS() ? u_ : s_;
        push16(static_cast<std::uint8_t>(other >> 8), static_cast<std::uint8_t>(other & 0xFF));
    }
    if (temp_ & 0x20) push16(static_cast<std::uint8_t>(y_ >> 8), static_cast<std::uint8_t>(y_ & 0xFF));
    if (temp_ & 0x10) push16(static_cast<std::uint8_t>(x_ >> 8), static_cast<std::uint8_t>(x_ & 0xFF));
    if (temp_ & 0x08) WriteMemory(--reg, dp_);
    if (temp_ & 0x04) WriteMemory(--reg, b_);
    if (temp_ & 0x02) WriteMemory(--reg, a_);
    if (temp_ & 0x01) WriteMemory(--reg, cc_);
    SetRegOp16Value(reg);
}

void MameDerivedM6809Adapter::PullRegisters() {
    std::uint16_t reg = RegOp16();
    if (temp_ & 0x01) cc_ = ReadMemory(reg++);
    if (temp_ & 0x02) a_ = ReadMemory(reg++);
    if (temp_ & 0x04) b_ = ReadMemory(reg++);
    if (temp_ & 0x08) dp_ = ReadMemory(reg++);
    if (temp_ & 0x10) {
        std::uint8_t hi = ReadMemory(reg++);
        std::uint8_t lo = ReadMemory(reg++);
        x_ = static_cast<std::uint16_t>((static_cast<std::uint16_t>(hi) << 8) | lo);
    }
    if (temp_ & 0x20) {
        std::uint8_t hi = ReadMemory(reg++);
        std::uint8_t lo = ReadMemory(reg++);
        y_ = static_cast<std::uint16_t>((static_cast<std::uint16_t>(hi) << 8) | lo);
    }
    if (temp_ & 0x40) {
        std::uint8_t hi = ReadMemory(reg++);
        std::uint8_t lo = ReadMemory(reg++);
        std::uint16_t value = static_cast<std::uint16_t>((static_cast<std::uint16_t>(hi) << 8) | lo);
        if (RegOp16IsS())
            u_ = value;
        else
            s_ = value;
    }
    if (temp_ & 0x80) {
        std::uint8_t hi = ReadMemory(reg++);
        std::uint8_t lo = ReadMemory(reg++);
        pc_ = static_cast<std::uint16_t>((static_cast<std::uint16_t>(hi) << 8) | lo);
    }
    (void)ReadMemory(reg);  // MAME's own trailing dummy read (bus settle after the last pull)
    SetRegOp16Value(reg);
}

// ---------------------------------------------------------------------------
// Addressing modes (DIRECT/EXTENDED/INDEXED)
// ---------------------------------------------------------------------------

void MameDerivedM6809Adapter::ModeDirect() {
    SetEa(static_cast<std::uint16_t>((static_cast<std::uint16_t>(dp_) << 8) | ReadOpcodeArg()));
    DummyVma(1);
}

void MameDerivedM6809Adapter::ModeExtended() {
    SetEaHi(ReadOpcodeArg());
    SetEaLo(ReadOpcodeArg());
    DummyVma(1);
}

void MameDerivedM6809Adapter::ModeIndexed() {
    opcode_ = ReadOpcodeArg();
    if (opcode_ & 0x80) {
        switch (opcode_ & 0x7F) {
            case 0x00:
            case 0x20:
            case 0x40:
            case 0x60:
            case 0x10:
            case 0x30:
            case 0x50:
            case 0x70:  // ,R+
                temp_ = Ireg();
                Ireg()++;
                DummyReadOpcodeArg(0);
                DummyVma(2);
                break;

            case 0x01:
            case 0x21:
            case 0x41:
            case 0x61:
            case 0x11:
            case 0x31:
            case 0x51:
            case 0x71:  // ,R++
                temp_ = Ireg();
                Ireg() += 2;
                DummyReadOpcodeArg(0);
                DummyVma(3);
                break;

            case 0x02:
            case 0x22:
            case 0x42:
            case 0x62:
            case 0x12:
            case 0x32:
            case 0x52:
            case 0x72:  // ,-R
                Ireg()--;
                temp_ = Ireg();
                DummyReadOpcodeArg(0);
                DummyVma(2);
                break;

            case 0x03:
            case 0x23:
            case 0x43:
            case 0x63:
            case 0x13:
            case 0x33:
            case 0x53:
            case 0x73:  // ,--R
                Ireg() -= 2;
                temp_ = Ireg();
                DummyReadOpcodeArg(0);
                DummyVma(3);
                break;

            case 0x04:
            case 0x24:
            case 0x44:
            case 0x64:
            case 0x14:
            case 0x34:
            case 0x54:
            case 0x74:  // ,R
                temp_ = Ireg();
                DummyReadOpcodeArg(0);
                break;

            case 0x05:
            case 0x25:
            case 0x45:
            case 0x65:
            case 0x15:
            case 0x35:
            case 0x55:
            case 0x75:  // B,R
                temp_ = static_cast<std::uint16_t>(Ireg() + static_cast<std::int8_t>(b_));
                DummyReadOpcodeArg(0);
                DummyVma(1);
                break;

            case 0x06:
            case 0x26:
            case 0x46:
            case 0x66:
            case 0x16:
            case 0x36:
            case 0x56:
            case 0x76:  // A,R
                temp_ = static_cast<std::uint16_t>(Ireg() + static_cast<std::int8_t>(a_));
                DummyReadOpcodeArg(0);
                DummyVma(1);
                break;

            case 0x08:
            case 0x28:
            case 0x48:
            case 0x68:
            case 0x18:
            case 0x38:
            case 0x58:
            case 0x78:  // n8,R
                temp_ = static_cast<std::uint16_t>(Ireg() + static_cast<std::int8_t>(ReadOpcodeArg()));
                DummyReadOpcodeArg(0);
                break;

            case 0x09:
            case 0x29:
            case 0x49:
            case 0x69:
            case 0x19:
            case 0x39:
            case 0x59:
            case 0x79: {  // n16,R
                std::uint8_t hi = ReadOpcodeArg();
                std::uint8_t lo = ReadOpcodeArg();
                temp_ = static_cast<std::uint16_t>(Ireg() +
                                                    static_cast<std::uint16_t>((static_cast<std::uint16_t>(hi) << 8) | lo));
                DummyVma(3);
                break;
            }

            case 0x0B:
            case 0x2B:
            case 0x4B:
            case 0x6B:
            case 0x1B:
            case 0x3B:
            case 0x5B:
            case 0x7B: {  // D,R
                std::uint16_t d = static_cast<std::uint16_t>((static_cast<std::uint16_t>(a_) << 8) | b_);
                temp_ = static_cast<std::uint16_t>(Ireg() + d);
                DummyReadOpcodeArg(0);
                DummyReadOpcodeArg(1);
                DummyVma(3);
                break;
            }

            case 0x0C:
            case 0x2C:
            case 0x4C:
            case 0x6C:
            case 0x1C:
            case 0x3C:
            case 0x5C:
            case 0x7C: {  // n8,PCR
                std::uint8_t offset = ReadOpcodeArg();
                temp_ = static_cast<std::uint16_t>(pc_ + static_cast<std::int8_t>(offset));
                DummyVma(1);
                break;
            }

            case 0x0D:
            case 0x2D:
            case 0x4D:
            case 0x6D:
            case 0x1D:
            case 0x3D:
            case 0x5D:
            case 0x7D: {  // n16,PCR
                std::uint8_t hi = ReadOpcodeArg();
                std::uint8_t lo = ReadOpcodeArg();
                std::uint16_t offset = static_cast<std::uint16_t>((static_cast<std::uint16_t>(hi) << 8) | lo);
                temp_ = static_cast<std::uint16_t>(pc_ + static_cast<std::int16_t>(offset));
                DummyVma(4);
                break;
            }

            case 0x0F:
            case 0x2F:
            case 0x4F:
            case 0x6F:
            case 0x1F:
            case 0x3F:
            case 0x5F:
            case 0x7F: {  // [n16] (extended indirect)
                std::uint8_t hi = ReadOpcodeArg();
                std::uint8_t lo = ReadOpcodeArg();
                temp_ = static_cast<std::uint16_t>((static_cast<std::uint16_t>(hi) << 8) | lo);
                DummyVma(1);
                break;
            }

            default:
                temp_ = 0x0000;
                break;
        }

        // indirect mode
        if (opcode_ & 0x10) {
            SetEa(temp_);
            std::uint8_t hi = ReadOperand(0);
            std::uint8_t lo = ReadOperand(1);
            temp_ = static_cast<std::uint16_t>((static_cast<std::uint16_t>(hi) << 8) | lo);
            DummyVma(1);
        }
    } else {
        // 5-bit offset
        std::uint8_t raw = static_cast<std::uint8_t>((opcode_ & 0x0F) | (opcode_ & 0x10 ? 0xF0 : 0x00));
        temp_ = static_cast<std::uint16_t>(Ireg() + static_cast<std::int8_t>(raw));
        DummyReadOpcodeArg(0);
        DummyVma(1);
    }
    SetEa(temp_);
}

// ---------------------------------------------------------------------------
// 8-bit read-modify-write group (NEG8/COM8/LSR8/ROR8/ASR8/ASL8/ROL8/DEC8/INC8/TST8/CLR8)
// ---------------------------------------------------------------------------

void MameDerivedM6809Adapter::OpNeg8() {
    std::uint8_t v = ReadOperand();
    v = SetFlags8(CC_NZVC, 0, v, static_cast<std::uint32_t>(-static_cast<std::int32_t>(v)));
    DummyReadOpcodeArg(0);
    WriteOperand(v);
}

void MameDerivedM6809Adapter::OpCom8() {
    std::uint8_t v = ReadOperand();
    cc_ &= static_cast<std::uint8_t>(~CC_V);
    cc_ |= CC_C;
    v = SetFlags8(CC_NZ, static_cast<std::uint8_t>(~v));
    DummyReadOpcodeArg(0);
    WriteOperand(v);
}

void MameDerivedM6809Adapter::OpLsr8() {
    std::uint8_t v = ReadOperand();
    cc_ &= static_cast<std::uint8_t>(~CC_C);
    cc_ |= (v & 1) ? CC_C : 0;
    v = SetFlags8(CC_NZ, static_cast<std::uint8_t>(v >> 1));
    DummyReadOpcodeArg(0);
    WriteOperand(v);
}

void MameDerivedM6809Adapter::OpRor8() {
    std::uint8_t v = ReadOperand();
    v = SetFlags8(CC_NZ, RotateRight8(v));
    DummyReadOpcodeArg(0);
    WriteOperand(v);
}

void MameDerivedM6809Adapter::OpAsr8() {
    std::uint8_t v = ReadOperand();
    cc_ &= static_cast<std::uint8_t>(~CC_C);
    cc_ |= (v & 1) ? CC_C : 0;
    v = SetFlags8(CC_NZ, static_cast<std::uint8_t>(static_cast<std::int8_t>(v) >> 1));
    DummyReadOpcodeArg(0);
    WriteOperand(v);
}

void MameDerivedM6809Adapter::OpAsl8() {
    std::uint8_t v = ReadOperand();
    v = SetFlags8(CC_NZVC, v, v, static_cast<std::uint32_t>(v) << 1);
    DummyReadOpcodeArg(0);
    WriteOperand(v);
}

void MameDerivedM6809Adapter::OpRol8() {
    std::uint8_t v = ReadOperand();
    v = SetFlags8(CC_NZV, v, v, RotateLeft8(v));
    DummyReadOpcodeArg(0);
    WriteOperand(v);
}

void MameDerivedM6809Adapter::OpDec8() {
    std::uint8_t v = ReadOperand();
    v = SetFlags8(CC_NZV, v, 1, static_cast<std::uint32_t>(v - 1));
    DummyReadOpcodeArg(0);
    WriteOperand(v);
}

void MameDerivedM6809Adapter::OpInc8() {
    std::uint8_t v = ReadOperand();
    v = SetFlags8(CC_NZV, v, 1, static_cast<std::uint32_t>(v + 1));
    DummyReadOpcodeArg(0);
    WriteOperand(v);
}

void MameDerivedM6809Adapter::OpTst8() {
    std::uint8_t v = ReadOperand();
    SetFlags8(CC_NZV, v);
    DummyVma(1);
    if (!IsRegisterAddressingMode()) DummyVma(1);
}

void MameDerivedM6809Adapter::OpClr8() {
    ReadOperand();
    cc_ &= static_cast<std::uint8_t>(~CC_NZVC);
    cc_ |= CC_Z;
    DummyReadOpcodeArg(0);
    WriteOperand(0);
}

void MameDerivedM6809Adapter::OpXnc8() {
    if (cc_ & CC_C)
        OpCom8();
    else
        OpNeg8();
}

void MameDerivedM6809Adapter::OpXdec8() {
    std::uint8_t v = ReadOperand();
    if (v)
        cc_ |= CC_C;
    else
        cc_ &= static_cast<std::uint8_t>(~CC_C);
    v = SetFlags8(CC_NZV, v, 1, static_cast<std::uint32_t>(v - 1));
    DummyReadOpcodeArg(0);
    WriteOperand(v);
}

void MameDerivedM6809Adapter::OpXclr8() {
    ReadOperand();
    cc_ &= static_cast<std::uint8_t>(~CC_NZV);
    cc_ |= CC_Z;
    DummyReadOpcodeArg(0);
    WriteOperand(0);
}

// ---------------------------------------------------------------------------
// 8-bit register-op group (SUB8/CMP8/SBC8/AND8/BIT8/LD8/ST8/EOR8/ADC8/OR8/ADD8)
// ---------------------------------------------------------------------------

void MameDerivedM6809Adapter::OpSub8() {
    std::uint8_t v = ReadOperand();
    SetRegOp8Value(SetFlags8(CC_NZVC, RegOp8(), v, static_cast<std::uint32_t>(RegOp8() - v)));
}

void MameDerivedM6809Adapter::OpCmp8() {
    std::uint8_t v = ReadOperand();
    SetFlags8(CC_NZVC, RegOp8(), v, static_cast<std::uint32_t>(RegOp8() - v));
}

void MameDerivedM6809Adapter::OpSbc8() {
    std::uint8_t v = ReadOperand();
    std::uint8_t borrow = (cc_ & CC_C) ? 1 : 0;
    SetRegOp8Value(SetFlags8(CC_NZVC, RegOp8(), v, static_cast<std::uint32_t>(RegOp8() - v - borrow)));
}

void MameDerivedM6809Adapter::OpAnd8() {
    cc_ &= static_cast<std::uint8_t>(~CC_V);
    SetRegOp8Value(SetFlags8(CC_NZ, 0, RegOp8(), static_cast<std::uint32_t>(RegOp8() & ReadOperand())));
}

void MameDerivedM6809Adapter::OpBit8() {
    cc_ &= static_cast<std::uint8_t>(~CC_V);
    SetFlags8(CC_NZ, 0, RegOp8(), static_cast<std::uint32_t>(RegOp8() & ReadOperand()));
}

void MameDerivedM6809Adapter::OpLd8() {
    SetRegOp8Value(ReadOperand());
    SetFlags8(CC_NZV, RegOp8());
}

void MameDerivedM6809Adapter::OpSt8() { WriteMemory(ea_, SetFlags8(CC_NZV, RegOp8())); }

void MameDerivedM6809Adapter::OpEor8() {
    cc_ &= static_cast<std::uint8_t>(~CC_V);
    SetRegOp8Value(SetFlags8(CC_NZ, 0, RegOp8(), static_cast<std::uint32_t>(RegOp8() ^ ReadOperand())));
}

void MameDerivedM6809Adapter::OpAdc8() {
    std::uint8_t v = ReadOperand();
    std::uint8_t carry = (cc_ & CC_C) ? 1 : 0;
    SetRegOp8Value(SetFlags8(CC_HNZVC, RegOp8(), v, static_cast<std::uint32_t>(RegOp8() + v + carry)));
}

void MameDerivedM6809Adapter::OpOr8() {
    cc_ &= static_cast<std::uint8_t>(~CC_V);
    SetRegOp8Value(SetFlags8(CC_NZ, 0, RegOp8(), static_cast<std::uint32_t>(RegOp8() | ReadOperand())));
}

void MameDerivedM6809Adapter::OpAdd8() {
    std::uint8_t v = ReadOperand();
    SetRegOp8Value(SetFlags8(CC_HNZVC, RegOp8(), v, static_cast<std::uint32_t>(RegOp8() + v)));
}

void MameDerivedM6809Adapter::OpXst8() {
    // TODO (carried over from MAME, m6809.hxx's own comment): flag setting on this undocumented
    // "store immediate" opcode is more complex on real hardware, see
    // https://github.com/hoglet67/6809Decoder/wiki/Undocumented-6809-Behaviours#store-immediate
    ReadOperand();
    SetFlags8(CC_NZV, RegOp8());
}

// ---------------------------------------------------------------------------
// 16-bit register-op group (SUB16/CMP16/ADD16/LD16/ST16, undocumented XADD16)
// ---------------------------------------------------------------------------

void MameDerivedM6809Adapter::OpSub16() {
    std::uint8_t hi = ReadOperand(0);
    std::uint8_t lo = ReadOperand(1);
    std::uint16_t v = static_cast<std::uint16_t>((static_cast<std::uint16_t>(hi) << 8) | lo);
    SetRegOp16Value(SetFlags16(CC_NZVC, RegOp16(), v, static_cast<std::uint32_t>(RegOp16() - v)));
    DummyReadOpcodeArg(0);
}

void MameDerivedM6809Adapter::OpCmp16() {
    std::uint8_t hi = ReadOperand(0);
    std::uint8_t lo = ReadOperand(1);
    std::uint16_t v = static_cast<std::uint16_t>((static_cast<std::uint16_t>(hi) << 8) | lo);
    SetFlags16(CC_NZVC, RegOp16(), v, static_cast<std::uint32_t>(RegOp16() - v));
    DummyReadOpcodeArg(0);
}

void MameDerivedM6809Adapter::OpAdd16() {
    std::uint8_t hi = ReadOperand(0);
    std::uint8_t lo = ReadOperand(1);
    std::uint16_t v = static_cast<std::uint16_t>((static_cast<std::uint16_t>(hi) << 8) | lo);
    SetRegOp16Value(SetFlags16(CC_NZVC, RegOp16(), v, static_cast<std::uint32_t>(RegOp16() + v)));
    DummyReadOpcodeArg(0);
}

void MameDerivedM6809Adapter::OpXadd16() {
    std::uint8_t hi = ReadOperand(0);
    std::uint8_t lo = ReadOperand(1);
    std::uint16_t v = static_cast<std::uint16_t>((static_cast<std::uint16_t>(hi) << 8) | lo);
    SetFlags16(CC_NZVC, RegOp16(), v, static_cast<std::uint32_t>(RegOp16() + v));
    // MAME's XADD16 does NOT write the sum back to the register (unlike ADD16): it computes
    // set_flags() and discards the result. An undocumented quirk, kept faithfully.
    DummyReadOpcodeArg(0);
}

void MameDerivedM6809Adapter::OpLd16() {
    std::uint8_t hi = ReadOperand(0);
    std::uint8_t lo = ReadOperand(1);
    SetRegOp16Value(static_cast<std::uint16_t>((static_cast<std::uint16_t>(hi) << 8) | lo));
    SetFlags16(CC_NZV, RegOp16());
    if (RegOp16IsS()) ldsEncountered_ = true;
}

void MameDerivedM6809Adapter::OpSt16() {
    std::uint16_t v = RegOp16();
    WriteOperand(0, static_cast<std::uint8_t>(v >> 8));
    WriteOperand(1, static_cast<std::uint8_t>(v & 0xFF));
    SetFlags16(CC_NZV, RegOp16());
}

void MameDerivedM6809Adapter::OpXst16() {
    // TODO (carried over from MAME): same undocumented-opcode flag caveat as OpXst8() above.
    ReadOperand(0);
    WriteMemory(pc_++, static_cast<std::uint8_t>(RegOp16() & 0xFF));
    SetFlags16(CC_NZV, RegOp16());
}

// ---------------------------------------------------------------------------
// Branches / subroutines / stack
// ---------------------------------------------------------------------------

void MameDerivedM6809Adapter::OpBranch() {
    std::uint8_t offset = ReadOpcodeArg();
    DummyVma(1);
    if (cond_) pc_ = static_cast<std::uint16_t>(pc_ + static_cast<std::int8_t>(offset));
}

void MameDerivedM6809Adapter::OpLBranch() {
    std::uint8_t hi = ReadOpcodeArg();
    std::uint8_t lo = ReadOpcodeArg();
    DummyVma(1);
    if (cond_) {
        std::uint16_t offset = static_cast<std::uint16_t>((static_cast<std::uint16_t>(hi) << 8) | lo);
        pc_ = static_cast<std::uint16_t>(pc_ + static_cast<std::int16_t>(offset));
        DummyVma(1);
    }
}

void MameDerivedM6809Adapter::OpBsr() {
    std::uint8_t offset = ReadOpcodeArg();
    ea_ = static_cast<std::uint16_t>(pc_ + static_cast<std::int8_t>(offset));
    DummyVma(3);
    GotoSubroutine();
}

void MameDerivedM6809Adapter::OpLbsr() {
    std::uint8_t hi = ReadOpcodeArg();
    std::uint8_t lo = ReadOpcodeArg();
    std::uint16_t offset = static_cast<std::uint16_t>((static_cast<std::uint16_t>(hi) << 8) | lo);
    ea_ = static_cast<std::uint16_t>(pc_ + static_cast<std::int16_t>(offset));
    DummyVma(4);
    GotoSubroutine();
}

void MameDerivedM6809Adapter::OpJsr() {
    DummyReadOpcodeArg(0);
    DummyVma(1);
    GotoSubroutine();
}

void MameDerivedM6809Adapter::GotoSubroutine() {
    WriteMemory(--s_, static_cast<std::uint8_t>(pc_ & 0xFF));
    WriteMemory(--s_, static_cast<std::uint8_t>(pc_ >> 8));
    pc_ = ea_;
}

void MameDerivedM6809Adapter::OpRts() {
    temp_ = 0x80;  // RTS is equivalent to "PULS PC"
    SetRegOp16(Reg16Id::S);
    PullRegisters();
}

void MameDerivedM6809Adapter::OpAbx() {
    x_ = static_cast<std::uint16_t>(x_ + b_);
    DummyReadOpcodeArg(0);
    DummyVma(1);
}

void MameDerivedM6809Adapter::OpMul() {
    Mul();
    DummyReadOpcodeArg(0);
    DummyVma(9);
}

void MameDerivedM6809Adapter::OpRti() {
    DummyReadOpcodeArg(0);
    SetRegOp16(Reg16Id::S);
    cc_ = ReadMemory(s_++);
    temp_ = ((cc_ & CC_E) ? EntireStateRegisters() : PartialStateRegisters()) & ~0x01;
    PullRegisters();
}

void MameDerivedM6809Adapter::OpCwai() {
    cc_ &= ReadOpcodeArg();
    DummyReadOpcodeArg(0);
    DummyVma(1);

    cc_ |= CC_E;
    SetRegOp16(Reg16Id::S);
    temp_ = EntireStateRegisters();
    PushRegisters();

    // MAME's own wait loop (while ((m_ea.w = get_pending_interrupt()) == 0) eat_remaining()) is
    // driven here by Execute()'s own outer loop instead (see IM6809Core::Execute()'s doc comment
    // and the cwaiWaiting_ branch in Execute()) - this method only sets up the wait, it never spins.
    cwaiWaiting_ = true;
}

void MameDerivedM6809Adapter::OpSync() {
    // SYNC stops processing instructions until an interrupt request happens. This doesn't require
    // the corresponding interrupt to be enabled: if it is disabled, execution continues with the
    // next instruction (matches MAME's own comment - GetPendingInterrupt() alone would incorrectly
    // gate on CC_I/CC_F, so the wait condition here is the raw input lines, not GetPendingInterrupt()).
    DummyReadOpcodeArg(0);
    if (!nmiAsserted_ && !firqLine_ && !irqLine_) {
        syncWaiting_ = true;
    } else {
        Eat(1);
    }
}

void MameDerivedM6809Adapter::OpLeaXy() {
    SetRegOp16Value(SetFlags16(CC_Z, ea_));
    DummyVma(1);
}

void MameDerivedM6809Adapter::OpLeaUs() {
    if (RegOp16IsS()) ldsEncountered_ = true;
    SetRegOp16Value(ea_);
    DummyVma(1);
}

void MameDerivedM6809Adapter::OpPshs() {
    temp_ = ReadOpcodeArg();
    DummyVma(2);
    SetRegOp16(Reg16Id::S);
    (void)ReadMemory(s_);
    PushRegisters();
}

void MameDerivedM6809Adapter::OpPuls() {
    temp_ = ReadOpcodeArg();
    DummyVma(2);
    SetRegOp16(Reg16Id::S);
    PullRegisters();
}

void MameDerivedM6809Adapter::OpPshu() {
    temp_ = ReadOpcodeArg();
    DummyVma(2);
    SetRegOp16(Reg16Id::U);
    (void)ReadMemory(u_);
    PushRegisters();
}

void MameDerivedM6809Adapter::OpPulu() {
    temp_ = ReadOpcodeArg();
    DummyVma(2);
    SetRegOp16(Reg16Id::U);
    PullRegisters();
}

void MameDerivedM6809Adapter::OpSwi() {
    // Doesn't share SoftwareInterrupt() because SWI (unlike SWI2/SWI3) masks IRQ/FIRQ.
    SetEa(VECTOR_SWI);
    cc_ |= CC_E;
    SetRegOp16(Reg16Id::S);
    temp_ = EntireStateRegisters();
    DummyReadOpcodeArg(0);
    DummyVma(1);
    PushRegisters();
    cc_ |= static_cast<std::uint8_t>(CC_I | CC_F);
    InterruptVectorFetch();
}

void MameDerivedM6809Adapter::OpSwi2() {
    SetEa(VECTOR_SWI2);
    SoftwareInterrupt();
}

void MameDerivedM6809Adapter::OpSwi3() {
    SetEa(VECTOR_SWI3);
    SoftwareInterrupt();
}

void MameDerivedM6809Adapter::SoftwareInterrupt() {
    // Used for SWI2/SWI3 (and, on the 6309, illegal-opcode/div-by-zero traps - out of scope here).
    cc_ |= CC_E;
    SetRegOp16(Reg16Id::S);
    temp_ = EntireStateRegisters();
    DummyReadOpcodeArg(0);
    DummyVma(1);
    PushRegisters();
    InterruptVectorFetch();
}

// ---------------------------------------------------------------------------
// Miscellaneous
// ---------------------------------------------------------------------------

void MameDerivedM6809Adapter::OpNop() { DummyReadOpcodeArg(0); }

void MameDerivedM6809Adapter::OpDaa() {
    Daa();
    DummyReadOpcodeArg(0);
}

void MameDerivedM6809Adapter::OpOrcc() {
    cc_ |= ReadOperand();
    DummyReadOpcodeArg(0);
}

void MameDerivedM6809Adapter::OpAndcc() {
    cc_ &= ReadOperand();
    DummyReadOpcodeArg(0);
}

void MameDerivedM6809Adapter::OpSex() {
    std::uint16_t d = SetFlags16(CC_NZ, static_cast<std::int8_t>(b_));
    a_ = static_cast<std::uint8_t>(d >> 8);
    b_ = static_cast<std::uint8_t>(d & 0xFF);
    DummyReadOpcodeArg(0);
}

void MameDerivedM6809Adapter::OpExg() {
    std::uint8_t param = ReadOpcodeArg();
    std::uint16_t reg1, reg2;
    if (param & 0x80) {
        reg1 = ReadTfrExg816Register(static_cast<std::uint8_t>(param >> 4));
        reg2 = ReadTfrExg816Register(param);
    } else {
        reg1 = ReadExg168Register(static_cast<std::uint8_t>(param >> 4));
        reg2 = ReadExg168Register(param);
    }
    WriteExgTfrRegister(param, reg1);
    WriteExgTfrRegister(static_cast<std::uint8_t>(param >> 4), reg2);
    DummyVma(6);
}

void MameDerivedM6809Adapter::OpTfr() {
    std::uint8_t param = ReadOpcodeArg();
    std::uint16_t reg = ReadTfrExg816Register(static_cast<std::uint8_t>(param >> 4));
    WriteExgTfrRegister(param, reg);
    if ((param & 0x0F) == 4) ldsEncountered_ = true;
    DummyVma(4);
}

void MameDerivedM6809Adapter::OpFreerun() {
    // A CPU test mode that increments the address bus until reset - see Execute()'s own freeRun_
    // handling for why this just latches a flag rather than doing anything else.
    freeRun_ = true;
}

void MameDerivedM6809Adapter::OpX18() {
    // Undocumented opcode 0x18 (page 0): rotates CC in a way that also depends on the byte at PC
    // (read but NOT consumed - read_opcode_arg(m_pc.w) reads at the CURRENT pc without bumping it,
    // matching MAME's own explicit-address overload used here instead of the auto-advancing one).
    std::uint8_t v = ReadOpcodeArgAt(pc_);
    v = static_cast<std::uint8_t>((cc_ & v) << 1);
    cc_ = static_cast<std::uint8_t>(v | ((cc_ & 0x04) >> 1));
}

void MameDerivedM6809Adapter::OpXandcc() {
    Eat(1);
    OpAndcc();
}

void MameDerivedM6809Adapter::OpXres() {
    // Undocumented "software reset" (0x3E, page 0): pushes the entire state like SWI, then re-fetches
    // the hardware reset vector (0xFFFE) instead of a dedicated vector.
    SetEa(VECTOR_RESET);
    SetRegOp16(Reg16Id::S);
    temp_ = EntireStateRegisters();
    DummyReadOpcodeArg(0);
    DummyVma(1);
    PushRegisters();
    InterruptVectorFetch();
}

void MameDerivedM6809Adapter::OpXswi2() {
    // Undocumented SWI2 alias (0x3E, page 0x10).
    SetEa(VECTOR_SWI2);
    SetRegOp16(Reg16Id::S);
    temp_ = EntireStateRegisters();
    DummyReadOpcodeArg(0);
    DummyVma(1);
    PushRegisters();
    InterruptVectorFetch();
}

void MameDerivedM6809Adapter::OpXfirq() {
    // Undocumented "force FIRQ" (0x3E, page 0x11): pushes the ENTIRE state (unlike a real FIRQ, which
    // pushes the partial state) then vectors through VECTOR_FIRQ.
    SetEa(VECTOR_FIRQ);
    SetRegOp16(Reg16Id::S);
    temp_ = EntireStateRegisters();
    DummyReadOpcodeArg(0);
    DummyVma(1);
    PushRegisters();
    InterruptVectorFetch();
}

void MameDerivedM6809Adapter::OpIllegal() {
    // MAME's log_illegal() only logs (logerror) - no CPU-visible effect, PC has already advanced past
    // the fetched opcode byte and execution simply continues with whatever follows.
}

// ---------------------------------------------------------------------------
// Dispatch (transcribed from the m6809make.py-generated m6809.hxx; see the class comment in the
// header for the push_state/goto -> plain-call transform). Each case below corresponds 1:1 to
// one `case 0xNN: <setup>; goto <TARGET>;` line in the generated dispatcher.
// ---------------------------------------------------------------------------

void MameDerivedM6809Adapter::DispatchPage0(std::uint8_t opcode) {
    switch (opcode) {
        case 0x00:
        case 0x01:
            ModeDirect();
            OpNeg8();
            break;
        case 0x02:
            ModeDirect();
            OpXnc8();
            break;
        case 0x03:
            ModeDirect();
            OpCom8();
            break;
        case 0x04:
        case 0x05:
            ModeDirect();
            OpLsr8();
            break;
        case 0x06:
            ModeDirect();
            OpRor8();
            break;
        case 0x07:
            ModeDirect();
            OpAsr8();
            break;
        case 0x08:
            ModeDirect();
            OpAsl8();
            break;
        case 0x09:
            ModeDirect();
            OpRol8();
            break;
        case 0x0A:
            ModeDirect();
            OpDec8();
            break;
        case 0x0B:
            ModeDirect();
            OpXdec8();
            break;
        case 0x0C:
            ModeDirect();
            OpInc8();
            break;
        case 0x0D:
            ModeDirect();
            OpTst8();
            break;
        case 0x0E:
            ModeDirect();
            OpJmp();
            break;
        case 0x0F:
            ModeDirect();
            OpClr8();
            break;

        case 0x10:
            DispatchPage10(ReadOpcode());
            return;
        case 0x11:
            DispatchPage11(ReadOpcode());
            return;
        case 0x12:
            OpNop();
            break;
        case 0x13:
            OpSync();
            break;
        case 0x14:
        case 0x15:
            OpFreerun();
            break;
        case 0x16:
            cond_ = true;
            OpLBranch();
            break;
        case 0x17:
            OpLbsr();
            break;
        case 0x18:
            OpX18();
            break;
        case 0x19:
            OpDaa();
            break;
        case 0x1A:
            SetImm();
            OpOrcc();
            break;
        case 0x1B:
            OpNop();
            break;
        case 0x1C:
            SetImm();
            OpAndcc();
            break;
        case 0x1D:
            OpSex();
            break;
        case 0x1E:
            OpExg();
            break;
        case 0x1F:
            OpTfr();
            break;

        case 0x20:
            cond_ = true;
            OpBranch();
            break;
        case 0x21:
            cond_ = false;
            OpBranch();
            break;
        case 0x22:
            cond_ = CondHi();
            OpBranch();
            break;
        case 0x23:
            cond_ = !CondHi();
            OpBranch();
            break;
        case 0x24:
            cond_ = CondCc();
            OpBranch();
            break;
        case 0x25:
            cond_ = !CondCc();
            OpBranch();
            break;
        case 0x26:
            cond_ = CondNe();
            OpBranch();
            break;
        case 0x27:
            cond_ = !CondNe();
            OpBranch();
            break;
        case 0x28:
            cond_ = CondVc();
            OpBranch();
            break;
        case 0x29:
            cond_ = !CondVc();
            OpBranch();
            break;
        case 0x2A:
            cond_ = CondPl();
            OpBranch();
            break;
        case 0x2B:
            cond_ = !CondPl();
            OpBranch();
            break;
        case 0x2C:
            cond_ = CondGe();
            OpBranch();
            break;
        case 0x2D:
            cond_ = !CondGe();
            OpBranch();
            break;
        case 0x2E:
            cond_ = CondGt();
            OpBranch();
            break;
        case 0x2F:
            cond_ = !CondGt();
            OpBranch();
            break;

        case 0x30:
            SetRegOp16(Reg16Id::X);
            ModeIndexed();
            OpLeaXy();
            break;
        case 0x31:
            SetRegOp16(Reg16Id::Y);
            ModeIndexed();
            OpLeaXy();
            break;
        case 0x32:
            SetRegOp16(Reg16Id::S);
            ModeIndexed();
            OpLeaUs();
            break;
        case 0x33:
            SetRegOp16(Reg16Id::U);
            ModeIndexed();
            OpLeaUs();
            break;
        case 0x34:
            OpPshs();
            break;
        case 0x35:
            OpPuls();
            break;
        case 0x36:
            OpPshu();
            break;
        case 0x37:
            OpPulu();
            break;
        case 0x38:
            SetImm();
            OpXandcc();
            break;
        case 0x39:
            OpRts();
            break;
        case 0x3A:
            OpAbx();
            break;
        case 0x3B:
            OpRti();
            break;
        case 0x3C:
            OpCwai();
            break;
        case 0x3D:
            OpMul();
            break;
        case 0x3E:
            OpXres();
            break;
        case 0x3F:
            OpSwi();
            break;

        case 0x40:
        case 0x41:
            SetA();
            OpNeg8();
            break;
        case 0x42:
            SetA();
            OpXnc8();
            break;
        case 0x43:
            SetA();
            OpCom8();
            break;
        case 0x44:
        case 0x45:
            SetA();
            OpLsr8();
            break;
        case 0x46:
            SetA();
            OpRor8();
            break;
        case 0x47:
            SetA();
            OpAsr8();
            break;
        case 0x48:
            SetA();
            OpAsl8();
            break;
        case 0x49:
            SetA();
            OpRol8();
            break;
        case 0x4A:
            SetA();
            OpDec8();
            break;
        case 0x4B:
            SetA();
            OpXdec8();
            break;
        case 0x4C:
            SetA();
            OpInc8();
            break;
        case 0x4D:
            SetA();
            OpTst8();
            break;
        case 0x4E:
            SetA();
            OpXclr8();
            break;
        case 0x4F:
            SetA();
            OpClr8();
            break;

        case 0x50:
        case 0x51:
            SetB();
            OpNeg8();
            break;
        case 0x52:
            SetB();
            OpXnc8();
            break;
        case 0x53:
            SetB();
            OpCom8();
            break;
        case 0x54:
        case 0x55:
            SetB();
            OpLsr8();
            break;
        case 0x56:
            SetB();
            OpRor8();
            break;
        case 0x57:
            SetB();
            OpAsr8();
            break;
        case 0x58:
            SetB();
            OpAsl8();
            break;
        case 0x59:
            SetB();
            OpRol8();
            break;
        case 0x5A:
            SetB();
            OpDec8();
            break;
        case 0x5B:
            SetB();
            OpXdec8();
            break;
        case 0x5C:
            SetB();
            OpInc8();
            break;
        case 0x5D:
            SetB();
            OpTst8();
            break;
        case 0x5E:
            SetB();
            OpXclr8();
            break;
        case 0x5F:
            SetB();
            OpClr8();
            break;

        case 0x60:
        case 0x61:
            ModeIndexed();
            OpNeg8();
            break;
        case 0x62:
            ModeIndexed();
            OpXnc8();
            break;
        case 0x63:
            ModeIndexed();
            OpCom8();
            break;
        case 0x64:
        case 0x65:
            ModeIndexed();
            OpLsr8();
            break;
        case 0x66:
            ModeIndexed();
            OpRor8();
            break;
        case 0x67:
            ModeIndexed();
            OpAsr8();
            break;
        case 0x68:
            ModeIndexed();
            OpAsl8();
            break;
        case 0x69:
            ModeIndexed();
            OpRol8();
            break;
        case 0x6A:
            ModeIndexed();
            OpDec8();
            break;
        case 0x6B:
            ModeIndexed();
            OpXdec8();
            break;
        case 0x6C:
            ModeIndexed();
            OpInc8();
            break;
        case 0x6D:
            ModeIndexed();
            OpTst8();
            break;
        case 0x6E:
            ModeIndexed();
            OpJmp();
            break;
        case 0x6F:
            ModeIndexed();
            OpClr8();
            break;

        case 0x70:
        case 0x71:
            ModeExtended();
            OpNeg8();
            break;
        case 0x72:
            ModeExtended();
            OpXnc8();
            break;
        case 0x73:
            ModeExtended();
            OpCom8();
            break;
        case 0x74:
        case 0x75:
            ModeExtended();
            OpLsr8();
            break;
        case 0x76:
            ModeExtended();
            OpRor8();
            break;
        case 0x77:
            ModeExtended();
            OpAsr8();
            break;
        case 0x78:
            ModeExtended();
            OpAsl8();
            break;
        case 0x79:
            ModeExtended();
            OpRol8();
            break;
        case 0x7A:
            ModeExtended();
            OpDec8();
            break;
        case 0x7B:
            ModeExtended();
            OpXdec8();
            break;
        case 0x7C:
            ModeExtended();
            OpInc8();
            break;
        case 0x7D:
            ModeExtended();
            OpTst8();
            break;
        case 0x7E:
            ModeExtended();
            OpJmp();
            break;
        case 0x7F:
            ModeExtended();
            OpClr8();
            break;

        case 0x80:
            SetRegOp8(Reg8Id::A);
            SetImm();
            OpSub8();
            break;
        case 0x81:
            SetRegOp8(Reg8Id::A);
            SetImm();
            OpCmp8();
            break;
        case 0x82:
            SetRegOp8(Reg8Id::A);
            SetImm();
            OpSbc8();
            break;
        case 0x83:
            SetRegOp16(Reg16Id::D);
            SetImm();
            OpSub16();
            break;
        case 0x84:
            SetRegOp8(Reg8Id::A);
            SetImm();
            OpAnd8();
            break;
        case 0x85:
            SetRegOp8(Reg8Id::A);
            SetImm();
            OpBit8();
            break;
        case 0x86:
            SetRegOp8(Reg8Id::A);
            SetImm();
            OpLd8();
            break;
        case 0x87:
            SetRegOp8(Reg8Id::A);
            SetImm();
            OpXst8();
            break;
        case 0x88:
            SetRegOp8(Reg8Id::A);
            SetImm();
            OpEor8();
            break;
        case 0x89:
            SetRegOp8(Reg8Id::A);
            SetImm();
            OpAdc8();
            break;
        case 0x8A:
            SetRegOp8(Reg8Id::A);
            SetImm();
            OpOr8();
            break;
        case 0x8B:
            SetRegOp8(Reg8Id::A);
            SetImm();
            OpAdd8();
            break;
        case 0x8C:
            SetRegOp16(Reg16Id::X);
            SetImm();
            OpCmp16();
            break;
        case 0x8D:
            OpBsr();
            break;
        case 0x8E:
            SetRegOp16(Reg16Id::X);
            SetImm();
            OpLd16();
            break;
        case 0x8F:
            SetRegOp16(Reg16Id::X);
            SetImm();
            OpXst16();
            break;

        case 0x90:
            SetRegOp8(Reg8Id::A);
            ModeDirect();
            OpSub8();
            break;
        case 0x91:
            SetRegOp8(Reg8Id::A);
            ModeDirect();
            OpCmp8();
            break;
        case 0x92:
            SetRegOp8(Reg8Id::A);
            ModeDirect();
            OpSbc8();
            break;
        case 0x93:
            SetRegOp16(Reg16Id::D);
            ModeDirect();
            OpSub16();
            break;
        case 0x94:
            SetRegOp8(Reg8Id::A);
            ModeDirect();
            OpAnd8();
            break;
        case 0x95:
            SetRegOp8(Reg8Id::A);
            ModeDirect();
            OpBit8();
            break;
        case 0x96:
            SetRegOp8(Reg8Id::A);
            ModeDirect();
            OpLd8();
            break;
        case 0x97:
            SetRegOp8(Reg8Id::A);
            ModeDirect();
            OpSt8();
            break;
        case 0x98:
            SetRegOp8(Reg8Id::A);
            ModeDirect();
            OpEor8();
            break;
        case 0x99:
            SetRegOp8(Reg8Id::A);
            ModeDirect();
            OpAdc8();
            break;
        case 0x9A:
            SetRegOp8(Reg8Id::A);
            ModeDirect();
            OpOr8();
            break;
        case 0x9B:
            SetRegOp8(Reg8Id::A);
            ModeDirect();
            OpAdd8();
            break;
        case 0x9C:
            SetRegOp16(Reg16Id::X);
            ModeDirect();
            OpCmp16();
            break;
        case 0x9D:
            ModeDirect();
            OpJsr();
            break;
        case 0x9E:
            SetRegOp16(Reg16Id::X);
            ModeDirect();
            OpLd16();
            break;
        case 0x9F:
            SetRegOp16(Reg16Id::X);
            ModeDirect();
            OpSt16();
            break;

        case 0xA0:
            SetRegOp8(Reg8Id::A);
            ModeIndexed();
            OpSub8();
            break;
        case 0xA1:
            SetRegOp8(Reg8Id::A);
            ModeIndexed();
            OpCmp8();
            break;
        case 0xA2:
            SetRegOp8(Reg8Id::A);
            ModeIndexed();
            OpSbc8();
            break;
        case 0xA3:
            SetRegOp16(Reg16Id::D);
            ModeIndexed();
            OpSub16();
            break;
        case 0xA4:
            SetRegOp8(Reg8Id::A);
            ModeIndexed();
            OpAnd8();
            break;
        case 0xA5:
            SetRegOp8(Reg8Id::A);
            ModeIndexed();
            OpBit8();
            break;
        case 0xA6:
            SetRegOp8(Reg8Id::A);
            ModeIndexed();
            OpLd8();
            break;
        case 0xA7:
            SetRegOp8(Reg8Id::A);
            ModeIndexed();
            OpSt8();
            break;
        case 0xA8:
            SetRegOp8(Reg8Id::A);
            ModeIndexed();
            OpEor8();
            break;
        case 0xA9:
            SetRegOp8(Reg8Id::A);
            ModeIndexed();
            OpAdc8();
            break;
        case 0xAA:
            SetRegOp8(Reg8Id::A);
            ModeIndexed();
            OpOr8();
            break;
        case 0xAB:
            SetRegOp8(Reg8Id::A);
            ModeIndexed();
            OpAdd8();
            break;
        case 0xAC:
            SetRegOp16(Reg16Id::X);
            ModeIndexed();
            OpCmp16();
            break;
        case 0xAD:
            ModeIndexed();
            OpJsr();
            break;
        case 0xAE:
            SetRegOp16(Reg16Id::X);
            ModeIndexed();
            OpLd16();
            break;
        case 0xAF:
            SetRegOp16(Reg16Id::X);
            ModeIndexed();
            OpSt16();
            break;

        case 0xB0:
            SetRegOp8(Reg8Id::A);
            ModeExtended();
            OpSub8();
            break;
        case 0xB1:
            SetRegOp8(Reg8Id::A);
            ModeExtended();
            OpCmp8();
            break;
        case 0xB2:
            SetRegOp8(Reg8Id::A);
            ModeExtended();
            OpSbc8();
            break;
        case 0xB3:
            SetRegOp16(Reg16Id::D);
            ModeExtended();
            OpSub16();
            break;
        case 0xB4:
            SetRegOp8(Reg8Id::A);
            ModeExtended();
            OpAnd8();
            break;
        case 0xB5:
            SetRegOp8(Reg8Id::A);
            ModeExtended();
            OpBit8();
            break;
        case 0xB6:
            SetRegOp8(Reg8Id::A);
            ModeExtended();
            OpLd8();
            break;
        case 0xB7:
            SetRegOp8(Reg8Id::A);
            ModeExtended();
            OpSt8();
            break;
        case 0xB8:
            SetRegOp8(Reg8Id::A);
            ModeExtended();
            OpEor8();
            break;
        case 0xB9:
            SetRegOp8(Reg8Id::A);
            ModeExtended();
            OpAdc8();
            break;
        case 0xBA:
            SetRegOp8(Reg8Id::A);
            ModeExtended();
            OpOr8();
            break;
        case 0xBB:
            SetRegOp8(Reg8Id::A);
            ModeExtended();
            OpAdd8();
            break;
        case 0xBC:
            SetRegOp16(Reg16Id::X);
            ModeExtended();
            OpCmp16();
            break;
        case 0xBD:
            ModeExtended();
            OpJsr();
            break;
        case 0xBE:
            SetRegOp16(Reg16Id::X);
            ModeExtended();
            OpLd16();
            break;
        case 0xBF:
            SetRegOp16(Reg16Id::X);
            ModeExtended();
            OpSt16();
            break;

        case 0xC0:
            SetRegOp8(Reg8Id::B);
            SetImm();
            OpSub8();
            break;
        case 0xC1:
            SetRegOp8(Reg8Id::B);
            SetImm();
            OpCmp8();
            break;
        case 0xC2:
            SetRegOp8(Reg8Id::B);
            SetImm();
            OpSbc8();
            break;
        case 0xC3:
            SetRegOp16(Reg16Id::D);
            SetImm();
            OpAdd16();
            break;
        case 0xC4:
            SetRegOp8(Reg8Id::B);
            SetImm();
            OpAnd8();
            break;
        case 0xC5:
            SetRegOp8(Reg8Id::B);
            SetImm();
            OpBit8();
            break;
        case 0xC6:
            SetRegOp8(Reg8Id::B);
            SetImm();
            OpLd8();
            break;
        case 0xC7:
            SetRegOp8(Reg8Id::B);
            SetImm();
            OpXst8();
            break;
        case 0xC8:
            SetRegOp8(Reg8Id::B);
            SetImm();
            OpEor8();
            break;
        case 0xC9:
            SetRegOp8(Reg8Id::B);
            SetImm();
            OpAdc8();
            break;
        case 0xCA:
            SetRegOp8(Reg8Id::B);
            SetImm();
            OpOr8();
            break;
        case 0xCB:
            SetRegOp8(Reg8Id::B);
            SetImm();
            OpAdd8();
            break;
        case 0xCC:
            SetRegOp16(Reg16Id::D);
            SetImm();
            OpLd16();
            break;
        case 0xCD:
            OpFreerun();
            break;
        case 0xCE:
            SetRegOp16(Reg16Id::U);
            SetImm();
            OpLd16();
            break;
        case 0xCF:
            SetRegOp16(Reg16Id::U);
            SetImm();
            OpXst16();
            break;

        case 0xD0:
            SetRegOp8(Reg8Id::B);
            ModeDirect();
            OpSub8();
            break;
        case 0xD1:
            SetRegOp8(Reg8Id::B);
            ModeDirect();
            OpCmp8();
            break;
        case 0xD2:
            SetRegOp8(Reg8Id::B);
            ModeDirect();
            OpSbc8();
            break;
        case 0xD3:
            SetRegOp16(Reg16Id::D);
            ModeDirect();
            OpAdd16();
            break;
        case 0xD4:
            SetRegOp8(Reg8Id::B);
            ModeDirect();
            OpAnd8();
            break;
        case 0xD5:
            SetRegOp8(Reg8Id::B);
            ModeDirect();
            OpBit8();
            break;
        case 0xD6:
            SetRegOp8(Reg8Id::B);
            ModeDirect();
            OpLd8();
            break;
        case 0xD7:
            SetRegOp8(Reg8Id::B);
            ModeDirect();
            OpSt8();
            break;
        case 0xD8:
            SetRegOp8(Reg8Id::B);
            ModeDirect();
            OpEor8();
            break;
        case 0xD9:
            SetRegOp8(Reg8Id::B);
            ModeDirect();
            OpAdc8();
            break;
        case 0xDA:
            SetRegOp8(Reg8Id::B);
            ModeDirect();
            OpOr8();
            break;
        case 0xDB:
            SetRegOp8(Reg8Id::B);
            ModeDirect();
            OpAdd8();
            break;
        case 0xDC:
            SetRegOp16(Reg16Id::D);
            ModeDirect();
            OpLd16();
            break;
        case 0xDD:
            SetRegOp16(Reg16Id::D);
            ModeDirect();
            OpSt16();
            break;
        case 0xDE:
            SetRegOp16(Reg16Id::U);
            ModeDirect();
            OpLd16();
            break;
        case 0xDF:
            SetRegOp16(Reg16Id::U);
            ModeDirect();
            OpSt16();
            break;

        case 0xE0:
            SetRegOp8(Reg8Id::B);
            ModeIndexed();
            OpSub8();
            break;
        case 0xE1:
            SetRegOp8(Reg8Id::B);
            ModeIndexed();
            OpCmp8();
            break;
        case 0xE2:
            SetRegOp8(Reg8Id::B);
            ModeIndexed();
            OpSbc8();
            break;
        case 0xE3:
            SetRegOp16(Reg16Id::D);
            ModeIndexed();
            OpAdd16();
            break;
        case 0xE4:
            SetRegOp8(Reg8Id::B);
            ModeIndexed();
            OpAnd8();
            break;
        case 0xE5:
            SetRegOp8(Reg8Id::B);
            ModeIndexed();
            OpBit8();
            break;
        case 0xE6:
            SetRegOp8(Reg8Id::B);
            ModeIndexed();
            OpLd8();
            break;
        case 0xE7:
            SetRegOp8(Reg8Id::B);
            ModeIndexed();
            OpSt8();
            break;
        case 0xE8:
            SetRegOp8(Reg8Id::B);
            ModeIndexed();
            OpEor8();
            break;
        case 0xE9:
            SetRegOp8(Reg8Id::B);
            ModeIndexed();
            OpAdc8();
            break;
        case 0xEA:
            SetRegOp8(Reg8Id::B);
            ModeIndexed();
            OpOr8();
            break;
        case 0xEB:
            SetRegOp8(Reg8Id::B);
            ModeIndexed();
            OpAdd8();
            break;
        case 0xEC:
            SetRegOp16(Reg16Id::D);
            ModeIndexed();
            OpLd16();
            break;
        case 0xED:
            SetRegOp16(Reg16Id::D);
            ModeIndexed();
            OpSt16();
            break;
        case 0xEE:
            SetRegOp16(Reg16Id::U);
            ModeIndexed();
            OpLd16();
            break;
        case 0xEF:
            SetRegOp16(Reg16Id::U);
            ModeIndexed();
            OpSt16();
            break;

        case 0xF0:
            SetRegOp8(Reg8Id::B);
            ModeExtended();
            OpSub8();
            break;
        case 0xF1:
            SetRegOp8(Reg8Id::B);
            ModeExtended();
            OpCmp8();
            break;
        case 0xF2:
            SetRegOp8(Reg8Id::B);
            ModeExtended();
            OpSbc8();
            break;
        case 0xF3:
            SetRegOp16(Reg16Id::D);
            ModeExtended();
            OpAdd16();
            break;
        case 0xF4:
            SetRegOp8(Reg8Id::B);
            ModeExtended();
            OpAnd8();
            break;
        case 0xF5:
            SetRegOp8(Reg8Id::B);
            ModeExtended();
            OpBit8();
            break;
        case 0xF6:
            SetRegOp8(Reg8Id::B);
            ModeExtended();
            OpLd8();
            break;
        case 0xF7:
            SetRegOp8(Reg8Id::B);
            ModeExtended();
            OpSt8();
            break;
        case 0xF8:
            SetRegOp8(Reg8Id::B);
            ModeExtended();
            OpEor8();
            break;
        case 0xF9:
            SetRegOp8(Reg8Id::B);
            ModeExtended();
            OpAdc8();
            break;
        case 0xFA:
            SetRegOp8(Reg8Id::B);
            ModeExtended();
            OpOr8();
            break;
        case 0xFB:
            SetRegOp8(Reg8Id::B);
            ModeExtended();
            OpAdd8();
            break;
        case 0xFC:
            SetRegOp16(Reg16Id::D);
            ModeExtended();
            OpLd16();
            break;
        case 0xFD:
            SetRegOp16(Reg16Id::D);
            ModeExtended();
            OpSt16();
            break;
        case 0xFE:
            SetRegOp16(Reg16Id::U);
            ModeExtended();
            OpLd16();
            break;
        case 0xFF:
            SetRegOp16(Reg16Id::U);
            ModeExtended();
            OpSt16();
            break;

        default:
            OpIllegal();
            break;
    }
}

void MameDerivedM6809Adapter::DispatchPage10(std::uint8_t opcode) {
    switch (opcode) {
        case 0x10:
        case 0x11:
            // A redundant prefix byte - stay on page 0x10 (matches MAME's own "case 0x10: goto
            // DISPATCH10;" - re-fetch and re-dispatch).
            DispatchPage10(ReadOpcode());
            return;

        case 0x20:
            cond_ = true;
            OpLBranch();
            break;
        case 0x21:
            cond_ = false;
            OpLBranch();
            break;
        case 0x22:
            cond_ = CondHi();
            OpLBranch();
            break;
        case 0x23:
            cond_ = !CondHi();
            OpLBranch();
            break;
        case 0x24:
            cond_ = CondCc();
            OpLBranch();
            break;
        case 0x25:
            cond_ = !CondCc();
            OpLBranch();
            break;
        case 0x26:
            cond_ = CondNe();
            OpLBranch();
            break;
        case 0x27:
            cond_ = !CondNe();
            OpLBranch();
            break;
        case 0x28:
            cond_ = CondVc();
            OpLBranch();
            break;
        case 0x29:
            cond_ = !CondVc();
            OpLBranch();
            break;
        case 0x2A:
            cond_ = CondPl();
            OpLBranch();
            break;
        case 0x2B:
            cond_ = !CondPl();
            OpLBranch();
            break;
        case 0x2C:
            cond_ = CondGe();
            OpLBranch();
            break;
        case 0x2D:
            cond_ = !CondGe();
            OpLBranch();
            break;
        case 0x2E:
            cond_ = CondGt();
            OpLBranch();
            break;
        case 0x2F:
            cond_ = !CondGt();
            OpLBranch();
            break;

        case 0x3E:
            OpXswi2();
            break;
        case 0x3F:
            OpSwi2();
            break;

        case 0x83:
            SetRegOp16(Reg16Id::D);
            SetImm();
            OpCmp16();
            break;
        case 0x87:
            SetRegOp8(Reg8Id::A);
            SetImm();
            OpXst8();
            break;
        case 0x8C:
            SetRegOp16(Reg16Id::Y);
            SetImm();
            OpCmp16();
            break;
        case 0x8E:
            SetRegOp16(Reg16Id::Y);
            SetImm();
            OpLd16();
            break;
        case 0x8F:
            SetRegOp16(Reg16Id::Y);
            SetImm();
            OpXst16();
            break;

        case 0x93:
            SetRegOp16(Reg16Id::D);
            ModeDirect();
            OpCmp16();
            break;
        case 0x9C:
            SetRegOp16(Reg16Id::Y);
            ModeDirect();
            OpCmp16();
            break;
        case 0x9E:
            SetRegOp16(Reg16Id::Y);
            ModeDirect();
            OpLd16();
            break;
        case 0x9F:
            SetRegOp16(Reg16Id::Y);
            ModeDirect();
            OpSt16();
            break;

        case 0xA3:
            SetRegOp16(Reg16Id::D);
            ModeIndexed();
            OpCmp16();
            break;
        case 0xAC:
            SetRegOp16(Reg16Id::Y);
            ModeIndexed();
            OpCmp16();
            break;
        case 0xAE:
            SetRegOp16(Reg16Id::Y);
            ModeIndexed();
            OpLd16();
            break;
        case 0xAF:
            SetRegOp16(Reg16Id::Y);
            ModeIndexed();
            OpSt16();
            break;

        case 0xB3:
            SetRegOp16(Reg16Id::D);
            ModeExtended();
            OpCmp16();
            break;
        case 0xBC:
            SetRegOp16(Reg16Id::Y);
            ModeExtended();
            OpCmp16();
            break;
        case 0xBE:
            SetRegOp16(Reg16Id::Y);
            ModeExtended();
            OpLd16();
            break;
        case 0xBF:
            SetRegOp16(Reg16Id::Y);
            ModeExtended();
            OpSt16();
            break;

        case 0xC3:
            SetRegOp16(Reg16Id::D);
            SetImm();
            OpXadd16();
            break;
        case 0xC7:
            SetRegOp8(Reg8Id::B);
            SetImm();
            OpXst8();
            break;
        case 0xCE:
            SetRegOp16(Reg16Id::S);
            SetImm();
            OpLd16();
            break;
        case 0xCF:
            SetRegOp16(Reg16Id::S);
            SetImm();
            OpXst16();
            break;

        case 0xD3:
            SetRegOp16(Reg16Id::D);
            ModeDirect();
            OpXadd16();
            break;
        case 0xDE:
            SetRegOp16(Reg16Id::S);
            ModeDirect();
            OpLd16();
            break;
        case 0xDF:
            SetRegOp16(Reg16Id::S);
            ModeDirect();
            OpSt16();
            break;

        case 0xE3:
            SetRegOp16(Reg16Id::D);
            ModeIndexed();
            OpXadd16();
            break;
        case 0xEE:
            SetRegOp16(Reg16Id::S);
            ModeIndexed();
            OpLd16();
            break;
        case 0xEF:
            SetRegOp16(Reg16Id::S);
            ModeIndexed();
            OpSt16();
            break;

        case 0xF3:
            SetRegOp16(Reg16Id::D);
            ModeExtended();
            OpXadd16();
            break;
        case 0xFE:
            SetRegOp16(Reg16Id::S);
            ModeExtended();
            OpLd16();
            break;
        case 0xFF:
            SetRegOp16(Reg16Id::S);
            ModeExtended();
            OpSt16();
            break;

        default:
            // "goto DISPATCH01" in the generated code - unhandled page-0x10 opcodes fall through to
            // the plain page-0 handler for the SAME already-fetched byte.
            DispatchPage0(opcode);
            break;
    }
}

void MameDerivedM6809Adapter::DispatchPage11(std::uint8_t opcode) {
    switch (opcode) {
        case 0x10:
        case 0x11:
            DispatchPage11(ReadOpcode());
            return;

        case 0x3E:
            OpXfirq();
            break;
        case 0x3F:
            OpSwi3();
            break;

        case 0x83:
            SetRegOp16(Reg16Id::U);
            SetImm();
            OpCmp16();
            break;
        case 0x8C:
            SetRegOp16(Reg16Id::S);
            SetImm();
            OpCmp16();
            break;
        case 0x87:
            SetRegOp8(Reg8Id::A);
            SetImm();
            OpXst8();
            break;
        case 0x8F:
            SetRegOp16(Reg16Id::X);
            SetImm();
            OpXst16();
            break;

        case 0x93:
            SetRegOp16(Reg16Id::U);
            ModeDirect();
            OpCmp16();
            break;
        case 0x9C:
            SetRegOp16(Reg16Id::S);
            ModeDirect();
            OpCmp16();
            break;

        case 0xC3:
            SetRegOp16(Reg16Id::U);
            SetImm();
            OpXadd16();
            break;
        case 0xC7:
            SetRegOp8(Reg8Id::B);
            SetImm();
            OpXst8();
            break;
        case 0xCF:
            SetRegOp16(Reg16Id::U);
            SetImm();
            OpXst16();
            break;

        case 0xA3:
            SetRegOp16(Reg16Id::U);
            ModeIndexed();
            OpCmp16();
            break;
        case 0xAC:
            SetRegOp16(Reg16Id::S);
            ModeIndexed();
            OpCmp16();
            break;

        case 0xB3:
            SetRegOp16(Reg16Id::U);
            ModeExtended();
            OpCmp16();
            break;
        case 0xBC:
            SetRegOp16(Reg16Id::S);
            ModeExtended();
            OpCmp16();
            break;

        case 0xD3:
            SetRegOp16(Reg16Id::U);
            ModeDirect();
            OpXadd16();
            break;
        case 0xE3:
            SetRegOp16(Reg16Id::U);
            ModeIndexed();
            OpXadd16();
            break;
        case 0xF3:
            SetRegOp16(Reg16Id::U);
            ModeExtended();
            OpXadd16();
            break;

        default:
            DispatchPage0(opcode);
            break;
    }
}

MameDerivedM6809Adapter::RegisterSnapshot MameDerivedM6809Adapter::Snapshot() const {
    RegisterSnapshot snap;
    snap.pc = pc_;
    snap.s = s_;
    snap.u = u_;
    snap.x = x_;
    snap.y = y_;
    snap.a = a_;
    snap.b = b_;
    snap.dp = dp_;
    snap.cc = cc_;
    snap.nmiLine = nmiLine_;
    snap.firqLine = firqLine_;
    snap.irqLine = irqLine_;
    snap.ldsEncountered = ldsEncountered_;
    snap.syncWaiting = syncWaiting_;
    snap.cwaiWaiting = cwaiWaiting_;
    snap.nmiServiceCount = nmiServiceCount_;
    snap.firqServiceCount = firqServiceCount_;
    snap.irqServiceCount = irqServiceCount_;
    return snap;
}
