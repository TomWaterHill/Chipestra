// license:BSD-3-Clause
// copyright-holders:Aaron Giles
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/cpu/m6800/m6800.cpp
//   src/devices/cpu/m6800/m6800.h
//   src/devices/cpu/m6800/m6801.cpp
//   src/devices/cpu/m6800/m6801.h
//   src/devices/cpu/m6800/6800ops.hxx
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#include "mame_derived_hd63701_adapter.h"

#include <algorithm>

namespace {
// hd63701_insn[]/cycles_63701[] (m6801.cpp): the opcode->handler table is
// transcribed into Dispatch()'s switch (which keeps each opcode's addressing
// mode visible at the call site) and the cycle table into the array below.
constexpr std::uint8_t kCycles63701[256] = {
    /*        0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F */
    /*0*/     4, 1, 4, 4, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /*1*/     1, 1, 4, 4, 4, 4, 1, 1, 2, 2, 4, 1, 4, 4, 4, 4,
    /*2*/     3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    /*3*/     1, 1, 3, 3, 1, 1, 4, 4, 4, 5, 1,10, 5, 7, 9,12,
    /*4*/     1, 4, 4, 1, 1, 4, 1, 1, 1, 1, 1, 4, 1, 1, 4, 1,
    /*5*/     1, 4, 4, 1, 1, 4, 1, 1, 1, 1, 1, 4, 1, 1, 4, 1,
    /*6*/     6, 7, 7, 6, 6, 7, 6, 6, 6, 6, 6, 5, 6, 4, 3, 5,
    /*7*/     6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 4, 6, 4, 3, 5,
    /*8*/     2, 2, 2, 3, 2, 2, 2, 4, 2, 2, 2, 2, 3, 5, 3, 4,
    /*9*/     3, 3, 3, 4, 3, 3, 3, 3, 3, 3, 3, 3, 4, 5, 4, 4,
    /*A*/     4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5,
    /*B*/     4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 4, 4, 5, 6, 5, 5,
    /*C*/     2, 2, 2, 3, 2, 2, 2, 4, 2, 2, 2, 2, 3, 4, 3, 4,
    /*D*/     3, 3, 3, 4, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4,
    /*E*/     4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5,
    /*F*/     4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5,
};
// Entry-for-entry transcription of m6801.cpp's cycles_63701[256]. MAME's `XX`
// placeholder is `#define XX 4` (illegal opcode, unknown cycle count), including
// for the undocumented opcodes 0x12/0x13.

std::uint8_t Flags8Inc(std::uint8_t a) {
    // Closed form of m6800.cpp's flags8i[256]: Z at 0x00, N|V at 0x80 (the only INC
    // overflow, 0x7F->0x80), N alone for 0x81..0xFF, nothing for 0x01..0x7F.
    std::uint8_t cc = 0;
    if (a == 0) cc |= 0x04;         // Z
    if (a & 0x80) cc |= 0x08;       // N
    if (a == 0x80) cc |= 0x02;      // V
    return cc;
}

std::uint8_t Flags8Dec(std::uint8_t a) {
    // Closed form of m6800.cpp's flags8d[256]: Z at 0x00, N for 0x80..0xFF, V alone at
    // 0x7F (the only DEC overflow, 0x80->0x7F).
    std::uint8_t cc = 0;
    if (a == 0) cc |= 0x04;         // Z
    if (a & 0x80) cc |= 0x08;       // N
    if (a == 0x7F) cc |= 0x02;      // V
    return cc;
}
}  // namespace

MameDerivedHd63701Adapter::MameDerivedHd63701Adapter() { Reset(); }

void MameDerivedHd63701Adapter::SetMemoryCallbacks(MemoryReadFn read, MemoryWriteFn write) {
    memoryRead_ = std::move(read);
    memoryWrite_ = std::move(write);
}

std::uint8_t MameDerivedHd63701Adapter::ReadMemory(std::uint16_t address) {
    if (IsInternalRegister(address)) return ReadInternalRegister(address);
    if (IsInternalRam(address)) return internalRam_[address - kInternalRamBase];
    return memoryRead_ ? memoryRead_(address) : 0;
}

void MameDerivedHd63701Adapter::WriteMemory(std::uint16_t address, std::uint8_t value) {
    if (IsInternalRegister(address)) {
        WriteInternalRegister(address, value);
        return;
    }
    if (IsInternalRam(address)) {
        internalRam_[address - kInternalRamBase] = value;
        return;
    }
    if (memoryWrite_) memoryWrite_(address, value);
}

std::uint8_t MameDerivedHd63701Adapter::ReadOpcode() { return ReadOpcodeArg(); }

std::uint8_t MameDerivedHd63701Adapter::ReadOpcodeArg() {
    std::uint8_t value = ReadMemory(pc_);
    ++pc_;
    return value;
}

std::uint16_t MameDerivedHd63701Adapter::ReadMemory16(std::uint16_t address) {
    std::uint16_t hi = ReadMemory(address);
    std::uint16_t lo = ReadMemory(static_cast<std::uint16_t>(address + 1));
    return static_cast<std::uint16_t>((hi << 8) | lo);
}

void MameDerivedHd63701Adapter::WriteMemory16(std::uint16_t address, std::uint16_t value) {
    WriteMemory(address, static_cast<std::uint8_t>(value >> 8));
    WriteMemory(static_cast<std::uint16_t>(address + 1), static_cast<std::uint8_t>(value & 0xFF));
}

// ---------------------------------------------------------------------------
// On-chip registers (m6801.cpp's m6801_io map + p1/p2/p3/p4/tcsr/ch/cl/ocrh/
// ocrl/icrh/icrl/sci_*/rcr read/write handlers; see the class comment for the
// port 2/3 and SCI scope).
// ---------------------------------------------------------------------------

std::uint8_t MameDerivedHd63701Adapter::ReadInternalRegister(std::uint16_t address) {
    switch (address) {
        case 0x00: return 0xFF;  // ff_r() - port 1 DDR is write-only
        case 0x01: return 0xFF;  // ff_r() - port 2 DDR is write-only
        case 0x02: return port_[0].ReadRegister();
        case 0x03: return port_[1].ReadRegister();
        case 0x04: return 0xFF;  // ff_r() - port 3 DDR is write-only
        case 0x05: return 0xFF;  // ff_r() - port 4 DDR is write-only
        case 0x06: return port_[2].ReadRegister();
        case 0x07: return port_[3].ReadRegister();
        case 0x08: return TcsrRead();
        case 0x09: return ChRead();
        case 0x0A: return ClRead();
        case 0x0B: return OcrhRead();
        case 0x0C: return OcrlRead();
        case 0x0D: return IcrhRead();
        case 0x0E: return IcrlRead();
        case 0x0F: return 0xFF;  // p3_csr_r(): handshake not ported, reads as the idle value
        case 0x10: return rmcr_;
        case 0x11: {
            // sci_trcsr_r(): a read arms the next data-register read to clear RDRF/ORFE, or the
            // next data-register write to clear TDRE. Inert here (the serial clock is not
            // ported), kept for register-file fidelity.
            if (trcsr_ & TRCSR_TDRE) trcsrReadTdre_ = true;
            if (trcsr_ & TRCSR_ORFE) trcsrReadOrfe_ = true;
            if (trcsr_ & TRCSR_RDRF) trcsrReadRdrf_ = true;
            return trcsr_;
        }
        case 0x12: {
            if (trcsrReadOrfe_) { trcsrReadOrfe_ = false; trcsr_ &= static_cast<std::uint8_t>(~TRCSR_ORFE); }
            if (trcsrReadRdrf_) { trcsrReadRdrf_ = false; trcsr_ &= static_cast<std::uint8_t>(~TRCSR_RDRF); }
            return rdr_;
        }
        case 0x14: return static_cast<std::uint8_t>(ramControl_ | 0x3F);
        default:
            // 0x13 (sci_tdr_w) has no read handler in m6801_io: unlike 0x00/0x01/0x04/0x05/0x0F
            // (ff_r(), reads 0xFF), it falls through to the unmapped-read default, 0x00.
            return 0x00;
    }
}

void MameDerivedHd63701Adapter::WriteInternalRegister(std::uint16_t address, std::uint8_t value) {
    switch (address) {
        case 0x00:  // p1_ddr_w
            if (port_[0].ddr != value) { port_[0].ddr = value; port_[0].NotifyWrite(); }
            return;
        case 0x01:  // p2_ddr_w
            if (port_[1].ddr != value) { port_[1].ddr = value; port_[1].NotifyWrite(); }
            return;
        case 0x02:  // p1_data_w
            port_[0].data = value;
            port_[0].NotifyWrite();
            return;
        case 0x03:  // p2_data_w
            port_[1].data = value;
            port_[1].NotifyWrite();
            return;
        case 0x04:  // p3_ddr_w
            if (port_[2].ddr != value) { port_[2].ddr = value; port_[2].NotifyWrite(); }
            return;
        case 0x05:  // p4_ddr_w
            if (port_[3].ddr != value) { port_[3].ddr = value; port_[3].NotifyWrite(); }
            return;
        case 0x06:  // p3_data_w
            port_[2].data = value;
            port_[2].NotifyWrite();
            return;
        case 0x07:  // p4_data_w
            port_[3].data = value;
            port_[3].NotifyWrite();
            return;
        case 0x08: TcsrWrite(value); return;
        case 0x09: ChWrite(value); return;
        case 0x0A: ClWrite(value); return;
        case 0x0B: OcrhWrite(value); return;
        case 0x0C: OcrlWrite(value); return;
        case 0x0F: return;  // p3_csr_w - handshake not ported, register write accepted and discarded
        case 0x10: rmcr_ = value; return;  // sci_rmcr_w() minus set_rmcr()'s serial-clock bookkeeping
        case 0x11:  // sci_trcsr_w()
            trcsr_ = static_cast<std::uint8_t>((trcsr_ & 0xE0) | (value & 0x1F));
            return;
        case 0x13:  // sci_tdr_w()
            if (trcsrReadTdre_) { trcsrReadTdre_ = false; trcsr_ &= static_cast<std::uint8_t>(~TRCSR_TDRE); }
            tdr_ = value;
            return;
        case 0x14: ramControl_ = value; return;  // rcr_w()
        default: return;  // 0x0D/0x0E/0x12 (ICRH/ICRL/RDR, read-only) and any other unlisted address
    }
}

// ---------------------------------------------------------------------------
// Timer (m6801.cpp's tcsr_r/w, ch_r/w, cl_r/w, ocrh_r/w, ocrl_r/w, icrh_r,
// icrl_r, modified_counters(), set_timer_event(), check_timer_event(),
// increment_counter()).
// ---------------------------------------------------------------------------

std::uint8_t MameDerivedHd63701Adapter::TcsrRead() {
    pendingTcsr_ = 0;
    return tcsr_;
}

void MameDerivedHd63701Adapter::TcsrWrite(std::uint8_t data) {
    data &= 0x1F;
    tcsr_ = static_cast<std::uint8_t>(data | (tcsr_ & 0xE0));
    pendingTcsr_ &= tcsr_;
    CheckIrq2();
}

std::uint8_t MameDerivedHd63701Adapter::ChRead() {
    if (!(pendingTcsr_ & TCSR_TOF)) tcsr_ &= static_cast<std::uint8_t>(~TCSR_TOF);
    return static_cast<std::uint8_t>((counter_ >> 8) & 0xFF);
}

void MameDerivedHd63701Adapter::ChWrite(std::uint8_t data) {
    latch09_ = data;
    std::uint64_t high = counter_ & ~static_cast<std::uint64_t>(0xFFFF);  // CTH, unchanged by this write
    counter_ = high | 0xFFF8;  // CT = 0xfff8
    // TOH = CTH (m6801.cpp)
    timerOver_ = (timerOver_ & 0xFFFF) | high;
    ModifiedCounters();
}

void MameDerivedHd63701Adapter::ClWrite(std::uint8_t data) {
    std::uint64_t high = counter_ & ~static_cast<std::uint64_t>(0xFFFF);
    std::uint16_t lowWord = static_cast<std::uint16_t>((static_cast<std::uint16_t>(latch09_) << 8) | data);
    counter_ = high | lowWord;  // CT = (m_latch09 << 8) | data
    timerOver_ = (timerOver_ & 0xFFFF) | high;  // TOH = CTH
    ModifiedCounters();
}

void MameDerivedHd63701Adapter::OcrhWrite(std::uint8_t data) {
    if (!(pendingTcsr_ & TCSR_OCF)) tcsr_ &= static_cast<std::uint8_t>(~TCSR_OCF);
    std::uint8_t oldHi = static_cast<std::uint8_t>((outputCompare_ >> 8) & 0xFF);
    if (oldHi != data) {
        outputCompare_ = (outputCompare_ & 0xFFFFFFFFFFFF00FFULL) | (static_cast<std::uint64_t>(data) << 8);
        ModifiedCounters();
    }
}

void MameDerivedHd63701Adapter::OcrlWrite(std::uint8_t data) {
    if (!(pendingTcsr_ & TCSR_OCF)) tcsr_ &= static_cast<std::uint8_t>(~TCSR_OCF);
    std::uint8_t oldLo = static_cast<std::uint8_t>(outputCompare_ & 0xFF);
    if (oldLo != data) {
        outputCompare_ = (outputCompare_ & 0xFFFFFFFFFFFFFF00ULL) | data;
        ModifiedCounters();
    }
}

std::uint8_t MameDerivedHd63701Adapter::IcrhRead() {
    if (!(pendingTcsr_ & TCSR_ICF)) tcsr_ &= static_cast<std::uint8_t>(~TCSR_ICF);
    return static_cast<std::uint8_t>((inputCapture_ >> 8) & 0xFF);
}

void MameDerivedHd63701Adapter::ModifiedCounters() {
    // OCH = (OC >= CT) ? CTH : CTH+1 (modified_counters()): rebuilds the full 64-bit
    // output-compare deadline from its 16-bit low half, picking the counter's current
    // high bits or the next epoch so the deadline stays in the future.
    std::uint64_t counterHigh = counter_ & ~static_cast<std::uint64_t>(0xFFFF);
    std::uint16_t counterLow16 = static_cast<std::uint16_t>(counter_ & 0xFFFF);
    std::uint16_t ocLow16 = static_cast<std::uint16_t>(outputCompare_ & 0xFFFF);
    std::uint64_t ocHigh = (ocLow16 >= counterLow16) ? counterHigh : (counterHigh + 0x10000);
    outputCompare_ = ocHigh | ocLow16;
    SetTimerEvent();
}

void MameDerivedHd63701Adapter::CheckTimerEvent() {
    if (counter_ >= outputCompare_) {
        outputCompare_ += 0x10000;  // OCH++ - next IRQ point
        tcsr_ |= TCSR_OCF;
        pendingTcsr_ |= TCSR_OCF;
        if (port_[1].ddr & 0x02) {
            port_[1].data &= static_cast<std::uint8_t>(~0x02);
            port_[1].data = static_cast<std::uint8_t>(port_[1].data | ((tcsr_ & TCSR_OLVL) << 1));
            port_[1].NotifyWrite();
        }
    }
    if (counter_ >= timerOver_) {
        timerOver_ += 0x10000;  // TOH++ - next IRQ point
        tcsr_ |= TCSR_TOF;
        pendingTcsr_ |= TCSR_TOF;
    }
    CheckIrq2();
    SetTimerEvent();
}

void MameDerivedHd63701Adapter::IncrementCounter(std::uint32_t amount) {
    Eat(static_cast<int>(amount));
    counter_ += amount;
    if (counter_ >= timerNext_) CheckTimerEvent();
}

// ---------------------------------------------------------------------------
// Interrupts (m6800.cpp's enter_interrupt()/check_irq_lines(), m6801.cpp's
// check_irq2()/take_irq2(), hd6301_cpu_device::take_trap()).
// ---------------------------------------------------------------------------

void MameDerivedHd63701Adapter::EnterInterrupt(std::uint16_t vector) {
    int cyclesToEat;
    if (waiState_ & kWaiFlag) {
        cyclesToEat = 4;
        waiState_ &= static_cast<std::uint8_t>(~kWaiFlag);
    } else {
        PushWord(pc_);
        PushWord(x_);
        PushByte(a_);
        PushByte(b_);
        PushByte(cc_);
        cyclesToEat = 12;
    }
    cc_ |= CC_I;
    pc_ = ReadMemory16(vector);
    IncrementCounter(static_cast<std::uint32_t>(cyclesToEat));
}

bool MameDerivedHd63701Adapter::CheckPendingInterrupts() {
    if (nmiPending_) {
        waiState_ &= static_cast<std::uint8_t>(~kSlpFlag);
        nmiPending_ = false;
        ++nmiServiceCount_;
        EnterInterrupt(VECTOR_NMI);
        return true;
    }
    if (irq1Line_) {
        // As in m6800.cpp's check_irq_lines(): an asserted IRQ1 takes this branch even when
        // CC.I masks it, so CheckIrq2() is only reached while IRQ1 is not asserted.
        waiState_ &= static_cast<std::uint8_t>(~kSlpFlag);
        if (!(cc_ & CC_I)) {
            ++irq1ServiceCount_;
            EnterInterrupt(VECTOR_IRQ1);
            return true;
        }
        return false;
    }
    irq2TakenThisCheck_ = false;
    CheckIrq2();
    return irq2TakenThisCheck_;
}

void MameDerivedHd63701Adapter::TakeIrq2(std::uint16_t vector) {
    waiState_ &= static_cast<std::uint8_t>(~kSlpFlag);
    if (!(cc_ & CC_I)) {
        ++irq2ServiceCount_;
        EnterInterrupt(vector);
        irq2TakenThisCheck_ = true;
    }
}

void MameDerivedHd63701Adapter::CheckIrq2() {
    // check_irq2() priority: ICI > OCI > TOI > SCI. The SCI branch can never be true
    // here (RDRF/ORFE/TDRE never change, see the class comment), so it is omitted.
    if ((tcsr_ & (TCSR_EICI | TCSR_ICF)) == (TCSR_EICI | TCSR_ICF)) {
        TakeIrq2(VECTOR_ICI);
    } else if ((tcsr_ & (TCSR_EOCI | TCSR_OCF)) == (TCSR_EOCI | TCSR_OCF)) {
        TakeIrq2(VECTOR_OCI);
    } else if ((tcsr_ & (TCSR_ETOI | TCSR_TOF)) == (TCSR_ETOI | TCSR_TOF)) {
        TakeIrq2(VECTOR_TOI);
    }
}

void MameDerivedHd63701Adapter::TakeTrap() {
    ++trapCount_;
    EnterInterrupt(VECTOR_TRAP);
}

// ---------------------------------------------------------------------------
// Addressing modes (m6800.cpp's DIRECT/EXTENDED/INDEXED macros).
// ---------------------------------------------------------------------------

std::uint16_t MameDerivedHd63701Adapter::ModeDirect() { return ReadOpcodeArg(); }

std::uint16_t MameDerivedHd63701Adapter::ModeExtended() {
    std::uint16_t hi = ReadOpcodeArg();
    std::uint16_t lo = ReadOpcodeArg();
    return static_cast<std::uint16_t>((hi << 8) | lo);
}

std::uint16_t MameDerivedHd63701Adapter::ModeIndexed() {
    std::uint8_t disp = ReadOpcodeArg();
    return static_cast<std::uint16_t>(x_ + disp);
}

// ---------------------------------------------------------------------------
// Flags (m6800.cpp's SET_FLAGS8/16, SET_H, SET_NZ8/16, SET_FLAGS8I/D).
// ---------------------------------------------------------------------------

void MameDerivedHd63701Adapter::SetFlags8(std::uint8_t a, std::uint8_t b, std::uint32_t r) {
    if (r & 0x80) cc_ |= CC_N;
    if ((r & 0xFF) == 0) cc_ |= CC_Z;
    if (((a ^ b ^ r ^ (r >> 1)) & 0x80) >> 6) cc_ |= CC_V;
    if (r & 0x100) cc_ |= CC_C;
}

void MameDerivedHd63701Adapter::SetFlags16(std::uint16_t a, std::uint16_t b, std::uint32_t r) {
    if (r & 0x8000) cc_ |= CC_N;
    if ((r & 0xFFFF) == 0) cc_ |= CC_Z;
    if (((a ^ b ^ r ^ (r >> 1)) & 0x8000) >> 14) cc_ |= CC_V;
    if (r & 0x10000) cc_ |= CC_C;
}

void MameDerivedHd63701Adapter::SetNz8(std::uint8_t a) {
    if (a & 0x80) cc_ |= CC_N;
    if (a == 0) cc_ |= CC_Z;
}

void MameDerivedHd63701Adapter::SetNz16(std::uint16_t a) {
    if (a & 0x8000) cc_ |= CC_N;
    if (a == 0) cc_ |= CC_Z;
}

void MameDerivedHd63701Adapter::SetFlags8Inc(std::uint8_t a) { cc_ |= Flags8Inc(a); }
void MameDerivedHd63701Adapter::SetFlags8Dec(std::uint8_t a) { cc_ |= Flags8Dec(a); }

// ---------------------------------------------------------------------------
// Reset / Execute / SetInputLine
// ---------------------------------------------------------------------------

void MameDerivedHd63701Adapter::Reset() {
    // device_reset() (m6800.cpp base + m6801_cpu_device override).
    cc_ = static_cast<std::uint8_t>(0xC0 | CC_I);  // m_cc=0xc0; SEI;
    pc_ = ReadMemory16(VECTOR_RESET);
    waiState_ = 0;
    nmiLine_ = false;
    nmiPending_ = false;
    irq1Line_ = false;

    for (auto& p : port_) { p.ddr = 0; p.data = 0; }
    tcsr_ = 0;
    pendingTcsr_ = 0;
    counter_ = 0;
    outputCompare_ = 0xFFFF;
    timerOver_ = 0xFFFF;
    timerNext_ = 0xFFFF;
    inputCapture_ = 0;
    latch09_ = 0;

    trcsr_ = TRCSR_TDRE;
    rmcr_ = 0;
    rdr_ = 0;
    tdr_ = 0;
    trcsrReadTdre_ = false;
    trcsrReadOrfe_ = false;
    trcsrReadRdrf_ = false;

    ramControl_ |= 0x40;

    cyclesConsumedThisCall_ = 0;
}

void MameDerivedHd63701Adapter::SetInputLine(Hd63701InputLine line, Hd63701InputLineState state) {
    bool asserted = state == Hd63701InputLineState::Assert;
    switch (line) {
        case Hd63701InputLine::Nmi:
            if (!nmiLine_ && asserted) nmiPending_ = true;
            nmiLine_ = asserted;
            break;
        case Hd63701InputLine::Irq1:
            irq1Line_ = asserted;
            break;
    }
}

std::uint64_t MameDerivedHd63701Adapter::Execute(std::uint64_t cycleBudget) {
    executing_ = true;
    cyclesConsumedThisCall_ = 0;

    while (cyclesConsumedThisCall_ < cycleBudget) {
        if (waiState_ & (kWaiFlag | kSlpFlag)) {
            // Idle (see IHd63701Core::Execute()): checked once per outstanding cycle.
            if (CheckPendingInterrupts()) continue;
            IncrementCounter(1);
        } else {
            // Interrupt entry and the handler's first instruction are separate turns: if an
            // interrupt was taken, do not also dispatch an instruction in the same pass.
            if (CheckPendingInterrupts()) continue;
            DispatchOneInstruction();
        }
    }

    executing_ = false;
    return cyclesConsumedThisCall_;
}

// ---------------------------------------------------------------------------
// Dispatch (execute_one()).
// ---------------------------------------------------------------------------

void MameDerivedHd63701Adapter::DispatchOneInstruction() {
    std::uint16_t opcodePc = pc_;
    std::uint8_t opcode = ReadOpcode();
    if (opcodeFetchObserver_) opcodeFetchObserver_(opcodePc, opcode);
    Dispatch(opcode);
    IncrementCounter(kCycles63701[opcode]);
}

void MameDerivedHd63701Adapter::Dispatch(std::uint8_t opcode) {
    switch (opcode) {
        // ---- row 0x0_ ----
        case 0x00: OpTrap(); break;
        case 0x01: OpNop(); break;
        case 0x02: OpTrap(); break;
        case 0x03: OpTrap(); break;
        case 0x04: OpLsrd(); break;
        case 0x05: OpAsld(); break;
        case 0x06: OpTap(); break;
        case 0x07: OpTpa(); break;
        case 0x08: OpInx(); break;
        case 0x09: OpDex(); break;
        case 0x0A: OpClv(); break;
        case 0x0B: OpSev(); break;
        case 0x0C: OpClc(); break;
        case 0x0D: OpSec(); break;
        case 0x0E: OpCli(); break;
        case 0x0F: OpSei(); break;
        // ---- row 0x1_ ----
        case 0x10: OpSba(); break;
        case 0x11: OpCba(); break;
        case 0x12: OpUndoc1(); break;
        case 0x13: OpUndoc2(); break;
        case 0x14: OpTrap(); break;
        case 0x15: OpTrap(); break;
        case 0x16: b_ = a_; { std::uint8_t v=a_; cc_ &= static_cast<std::uint8_t>(~(CC_N|CC_Z|CC_V)); SetNz8(v);} break;  // TAB
        case 0x17: a_ = b_; { std::uint8_t v=b_; cc_ &= static_cast<std::uint8_t>(~(CC_N|CC_Z|CC_V)); SetNz8(v);} break;  // TBA
        case 0x18: OpXgdx(); break;
        case 0x19: OpDaa(); break;
        case 0x1A: OpSlp(); break;
        case 0x1B: OpAba(); break;
        case 0x1C: OpTrap(); break;
        case 0x1D: OpTrap(); break;
        case 0x1E: OpTrap(); break;
        case 0x1F: OpTrap(); break;
        // ---- row 0x2_: branches ----
        case 0x20: OpBranch(true); break;                                  // BRA
        case 0x21: OpBranch(false); break;                                 // BRN
        case 0x22: OpBranch(!(cc_ & (CC_Z | CC_C))); break;                // BHI
        case 0x23: OpBranch(static_cast<bool>(cc_ & (CC_Z | CC_C))); break;// BLS
        case 0x24: OpBranch(!(cc_ & CC_C)); break;                         // BCC
        case 0x25: OpBranch(static_cast<bool>(cc_ & CC_C)); break;         // BCS
        case 0x26: OpBranch(!(cc_ & CC_Z)); break;                         // BNE
        case 0x27: OpBranch(static_cast<bool>(cc_ & CC_Z)); break;         // BEQ
        case 0x28: OpBranch(!(cc_ & CC_V)); break;                         // BVC
        case 0x29: OpBranch(static_cast<bool>(cc_ & CC_V)); break;         // BVS
        case 0x2A: OpBranch(!(cc_ & CC_N)); break;                         // BPL
        case 0x2B: OpBranch(static_cast<bool>(cc_ & CC_N)); break;         // BMI
        case 0x2C: OpBranch(!NxorV()); break;                              // BGE
        case 0x2D: OpBranch(NxorV()); break;                               // BLT
        case 0x2E: OpBranch(!(NxorV() || (cc_ & CC_Z))); break;            // BGT
        case 0x2F: OpBranch(NxorV() || (cc_ & CC_Z)); break;               // BLE
        // ---- row 0x3_ ----
        case 0x30: OpTsx(); break;
        case 0x31: OpIns(); break;
        case 0x32: OpPula(); break;
        case 0x33: OpPulb(); break;
        case 0x34: OpDes(); break;
        case 0x35: OpTxs(); break;
        case 0x36: OpPsha(); break;
        case 0x37: OpPshb(); break;
        case 0x38: OpPulx(); break;
        case 0x39: OpRts(); break;
        case 0x3A: OpAbx(); break;
        case 0x3B: OpRti(); break;
        case 0x3C: OpPshx(); break;
        case 0x3D: OpMul(); break;
        case 0x3E: OpWai(); break;
        case 0x3F: OpSwi(); break;
        // ---- row 0x4_: A accumulator inherent RMW ----
        case 0x40: OpNegA(); break;
        case 0x41: OpTrap(); break;
        case 0x42: OpTrap(); break;
        case 0x43: OpComA(); break;
        case 0x44: OpLsrA(); break;
        case 0x45: OpTrap(); break;
        case 0x46: OpRorA(); break;
        case 0x47: OpAsrA(); break;
        case 0x48: OpAslA(); break;
        case 0x49: OpRolA(); break;
        case 0x4A: OpDecA(); break;
        case 0x4B: OpTrap(); break;
        case 0x4C: OpIncA(); break;
        case 0x4D: OpTstA(); break;
        case 0x4E: OpTrap(); break;
        case 0x4F: OpClrA(); break;
        // ---- row 0x5_: B accumulator inherent RMW ----
        case 0x50: OpNegB(); break;
        case 0x51: OpTrap(); break;
        case 0x52: OpTrap(); break;
        case 0x53: OpComB(); break;
        case 0x54: OpLsrB(); break;
        case 0x55: OpTrap(); break;
        case 0x56: OpRorB(); break;
        case 0x57: OpAsrB(); break;
        case 0x58: OpAslB(); break;
        case 0x59: OpRolB(); break;
        case 0x5A: OpDecB(); break;
        case 0x5B: OpTrap(); break;
        case 0x5C: OpIncB(); break;
        case 0x5D: OpTstB(); break;
        case 0x5E: OpTrap(); break;
        case 0x5F: OpClrB(); break;
        // ---- row 0x6_: indexed memory RMW ----
        case 0x60: OpNegM(RmwMode::Indexed); break;
        case 0x61: OpAimM(BitOpMode::Indexed); break;
        case 0x62: OpOimM(BitOpMode::Indexed); break;
        case 0x63: OpComM(RmwMode::Indexed); break;
        case 0x64: OpLsrM(RmwMode::Indexed); break;
        case 0x65: OpEimM(BitOpMode::Indexed); break;
        case 0x66: OpRorM(RmwMode::Indexed); break;
        case 0x67: OpAsrM(RmwMode::Indexed); break;
        case 0x68: OpAslM(RmwMode::Indexed); break;
        case 0x69: OpRolM(RmwMode::Indexed); break;
        case 0x6A: OpDecM(RmwMode::Indexed); break;
        case 0x6B: OpTimM(BitOpMode::Indexed); break;
        case 0x6C: OpIncM(RmwMode::Indexed); break;
        case 0x6D: OpTstM(RmwMode::Indexed); break;
        case 0x6E: OpJmpM(RmwMode::Indexed); break;
        case 0x6F: OpClrM(RmwMode::Indexed); break;
        // ---- row 0x7_: extended memory RMW (direct for AIM/OIM/EIM/TIM) ----
        case 0x70: OpNegM(RmwMode::Extended); break;
        case 0x71: OpAimM(BitOpMode::Direct); break;
        case 0x72: OpOimM(BitOpMode::Direct); break;
        case 0x73: OpComM(RmwMode::Extended); break;
        case 0x74: OpLsrM(RmwMode::Extended); break;
        case 0x75: OpEimM(BitOpMode::Direct); break;
        case 0x76: OpRorM(RmwMode::Extended); break;
        case 0x77: OpAsrM(RmwMode::Extended); break;
        case 0x78: OpAslM(RmwMode::Extended); break;
        case 0x79: OpRolM(RmwMode::Extended); break;
        case 0x7A: OpDecM(RmwMode::Extended); break;
        case 0x7B: OpTimM(BitOpMode::Direct); break;
        case 0x7C: OpIncM(RmwMode::Extended); break;
        case 0x7D: OpTstM(RmwMode::Extended); break;
        case 0x7E: OpJmpM(RmwMode::Extended); break;
        case 0x7F: OpClrM(RmwMode::Extended); break;
        // ---- row 0x8_: A accumulator / 16-bit immediate ----
        case 0x80: OpSubA(OperandMode::Immediate); break;
        case 0x81: OpCmpA(OperandMode::Immediate); break;
        case 0x82: OpSbcA(OperandMode::Immediate); break;
        case 0x83: OpSubD(OperandMode::Immediate); break;
        case 0x84: OpAndA(OperandMode::Immediate); break;
        case 0x85: OpBitA(OperandMode::Immediate); break;
        case 0x86: OpLdaA(OperandMode::Immediate); break;
        case 0x87: OpTrap(); break;  // STA immediate - illegal
        case 0x88: OpEorA(OperandMode::Immediate); break;
        case 0x89: OpAdcA(OperandMode::Immediate); break;
        case 0x8A: OpOraA(OperandMode::Immediate); break;
        case 0x8B: OpAddA(OperandMode::Immediate); break;
        case 0x8C: OpCpx(OperandMode::Immediate); break;
        case 0x8D: OpBsr(); break;
        case 0x8E: OpLds(OperandMode::Immediate); break;
        case 0x8F: OpTrap(); break;  // STS immediate - illegal
        // ---- row 0x9_: A accumulator / 16-bit direct ----
        case 0x90: OpSubA(OperandMode::Direct); break;
        case 0x91: OpCmpA(OperandMode::Direct); break;
        case 0x92: OpSbcA(OperandMode::Direct); break;
        case 0x93: OpSubD(OperandMode::Direct); break;
        case 0x94: OpAndA(OperandMode::Direct); break;
        case 0x95: OpBitA(OperandMode::Direct); break;
        case 0x96: OpLdaA(OperandMode::Direct); break;
        case 0x97: OpStaA(OperandMode::Direct); break;
        case 0x98: OpEorA(OperandMode::Direct); break;
        case 0x99: OpAdcA(OperandMode::Direct); break;
        case 0x9A: OpOraA(OperandMode::Direct); break;
        case 0x9B: OpAddA(OperandMode::Direct); break;
        case 0x9C: OpCpx(OperandMode::Direct); break;
        case 0x9D: OpJsrM(JsrMode::Direct); break;
        case 0x9E: OpLds(OperandMode::Direct); break;
        case 0x9F: OpSts(OperandMode::Direct); break;
        // ---- row 0xA_: A accumulator / 16-bit indexed ----
        case 0xA0: OpSubA(OperandMode::Indexed); break;
        case 0xA1: OpCmpA(OperandMode::Indexed); break;
        case 0xA2: OpSbcA(OperandMode::Indexed); break;
        case 0xA3: OpSubD(OperandMode::Indexed); break;
        case 0xA4: OpAndA(OperandMode::Indexed); break;
        case 0xA5: OpBitA(OperandMode::Indexed); break;
        case 0xA6: OpLdaA(OperandMode::Indexed); break;
        case 0xA7: OpStaA(OperandMode::Indexed); break;
        case 0xA8: OpEorA(OperandMode::Indexed); break;
        case 0xA9: OpAdcA(OperandMode::Indexed); break;
        case 0xAA: OpOraA(OperandMode::Indexed); break;
        case 0xAB: OpAddA(OperandMode::Indexed); break;
        case 0xAC: OpCpx(OperandMode::Indexed); break;
        case 0xAD: OpJsrM(JsrMode::Indexed); break;
        case 0xAE: OpLds(OperandMode::Indexed); break;
        case 0xAF: OpSts(OperandMode::Indexed); break;
        // ---- row 0xB_: A accumulator / 16-bit extended ----
        case 0xB0: OpSubA(OperandMode::Extended); break;
        case 0xB1: OpCmpA(OperandMode::Extended); break;
        case 0xB2: OpSbcA(OperandMode::Extended); break;
        case 0xB3: OpSubD(OperandMode::Extended); break;
        case 0xB4: OpAndA(OperandMode::Extended); break;
        case 0xB5: OpBitA(OperandMode::Extended); break;
        case 0xB6: OpLdaA(OperandMode::Extended); break;
        case 0xB7: OpStaA(OperandMode::Extended); break;
        case 0xB8: OpEorA(OperandMode::Extended); break;
        case 0xB9: OpAdcA(OperandMode::Extended); break;
        case 0xBA: OpOraA(OperandMode::Extended); break;
        case 0xBB: OpAddA(OperandMode::Extended); break;
        case 0xBC: OpCpx(OperandMode::Extended); break;
        case 0xBD: OpJsrM(JsrMode::Extended); break;
        case 0xBE: OpLds(OperandMode::Extended); break;
        case 0xBF: OpSts(OperandMode::Extended); break;
        // ---- row 0xC_: B accumulator / 16-bit immediate ----
        case 0xC0: OpSubB(OperandMode::Immediate); break;
        case 0xC1: OpCmpB(OperandMode::Immediate); break;
        case 0xC2: OpSbcB(OperandMode::Immediate); break;
        case 0xC3: OpAddD(OperandMode::Immediate); break;
        case 0xC4: OpAndB(OperandMode::Immediate); break;
        case 0xC5: OpBitB(OperandMode::Immediate); break;
        case 0xC6: OpLdaB(OperandMode::Immediate); break;
        case 0xC7: OpTrap(); break;  // STB immediate - illegal
        case 0xC8: OpEorB(OperandMode::Immediate); break;
        case 0xC9: OpAdcB(OperandMode::Immediate); break;
        case 0xCA: OpOraB(OperandMode::Immediate); break;
        case 0xCB: OpAddB(OperandMode::Immediate); break;
        case 0xCC: OpLdd(OperandMode::Immediate); break;
        case 0xCD: OpTrap(); break;  // STD immediate - illegal
        case 0xCE: OpLdx(OperandMode::Immediate); break;
        case 0xCF: OpTrap(); break;  // STX immediate - illegal
        // ---- row 0xD_: B accumulator / 16-bit direct ----
        case 0xD0: OpSubB(OperandMode::Direct); break;
        case 0xD1: OpCmpB(OperandMode::Direct); break;
        case 0xD2: OpSbcB(OperandMode::Direct); break;
        case 0xD3: OpAddD(OperandMode::Direct); break;
        case 0xD4: OpAndB(OperandMode::Direct); break;
        case 0xD5: OpBitB(OperandMode::Direct); break;
        case 0xD6: OpLdaB(OperandMode::Direct); break;
        case 0xD7: OpStaB(OperandMode::Direct); break;
        case 0xD8: OpEorB(OperandMode::Direct); break;
        case 0xD9: OpAdcB(OperandMode::Direct); break;
        case 0xDA: OpOraB(OperandMode::Direct); break;
        case 0xDB: OpAddB(OperandMode::Direct); break;
        case 0xDC: OpLdd(OperandMode::Direct); break;
        case 0xDD: OpStd(OperandMode::Direct); break;
        case 0xDE: OpLdx(OperandMode::Direct); break;
        case 0xDF: OpStx(OperandMode::Direct); break;
        // ---- row 0xE_: B accumulator / 16-bit indexed ----
        case 0xE0: OpSubB(OperandMode::Indexed); break;
        case 0xE1: OpCmpB(OperandMode::Indexed); break;
        case 0xE2: OpSbcB(OperandMode::Indexed); break;
        case 0xE3: OpAddD(OperandMode::Indexed); break;
        case 0xE4: OpAndB(OperandMode::Indexed); break;
        case 0xE5: OpBitB(OperandMode::Indexed); break;
        case 0xE6: OpLdaB(OperandMode::Indexed); break;
        case 0xE7: OpStaB(OperandMode::Indexed); break;
        case 0xE8: OpEorB(OperandMode::Indexed); break;
        case 0xE9: OpAdcB(OperandMode::Indexed); break;
        case 0xEA: OpOraB(OperandMode::Indexed); break;
        case 0xEB: OpAddB(OperandMode::Indexed); break;
        case 0xEC: OpLdd(OperandMode::Indexed); break;
        case 0xED: OpStd(OperandMode::Indexed); break;
        case 0xEE: OpLdx(OperandMode::Indexed); break;
        case 0xEF: OpStx(OperandMode::Indexed); break;
        // ---- row 0xF_: B accumulator / 16-bit extended ----
        case 0xF0: OpSubB(OperandMode::Extended); break;
        case 0xF1: OpCmpB(OperandMode::Extended); break;
        case 0xF2: OpSbcB(OperandMode::Extended); break;
        case 0xF3: OpAddD(OperandMode::Extended); break;
        case 0xF4: OpAndB(OperandMode::Extended); break;
        case 0xF5: OpBitB(OperandMode::Extended); break;
        case 0xF6: OpLdaB(OperandMode::Extended); break;
        case 0xF7: OpStaB(OperandMode::Extended); break;
        case 0xF8: OpEorB(OperandMode::Extended); break;
        case 0xF9: OpAdcB(OperandMode::Extended); break;
        case 0xFA: OpOraB(OperandMode::Extended); break;
        case 0xFB: OpAddB(OperandMode::Extended); break;
        case 0xFC: OpLdd(OperandMode::Extended); break;
        case 0xFD: OpStd(OperandMode::Extended); break;
        case 0xFE: OpLdx(OperandMode::Extended); break;
        case 0xFF: OpStx(OperandMode::Extended); break;
    }
}

// ---------------------------------------------------------------------------
// Inherent / accumulator single-register ops (6800ops.hxx, rows 0x00-0x3F).
//
// Intermediate widths: MAME computes subtraction/negation results into a u16 (u32 for
// the 16-bit register group) before SET_FLAGS8/16; this port uses std::uint32_t throughout.
// Widening only changes bits above the original width, and SetFlags8/16 only examine
// bits 0-16, so the flags are identical.
// ---------------------------------------------------------------------------

void MameDerivedHd63701Adapter::OpTrap() {
    --pc_;
    TakeTrap();
}

void MameDerivedHd63701Adapter::OpUndoc1() {
    x_ = static_cast<std::uint16_t>(x_ + ReadMemory(static_cast<std::uint16_t>(s_ + 1)));
}

void MameDerivedHd63701Adapter::OpUndoc2() {
    x_ = static_cast<std::uint16_t>(x_ + ReadMemory(static_cast<std::uint16_t>(s_ + 1)));
}

void MameDerivedHd63701Adapter::OpLsrd() {
    std::uint16_t t = GetD();
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V | CC_C));
    if (t & 0x0001) cc_ |= CC_C;
    t = static_cast<std::uint16_t>(t >> 1);
    if (t == 0) cc_ |= CC_Z;
    if (NxorC()) cc_ |= CC_V;
    SetD(t);
}

void MameDerivedHd63701Adapter::OpAsld() {
    std::uint16_t t = GetD();
    std::uint32_t r = static_cast<std::uint32_t>(t) << 1;
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V | CC_C));
    SetFlags16(t, t, r);
    SetD(static_cast<std::uint16_t>(r));
}

void MameDerivedHd63701Adapter::OpTap() {
    cc_ = a_;
    // TAP delays its effect on the I mask by one instruction (see class comment).
    DispatchOneInstruction();
    CheckPendingInterrupts();
}

void MameDerivedHd63701Adapter::OpCli() {
    bool wasMasked = (cc_ & CC_I) != 0;
    cc_ &= static_cast<std::uint8_t>(~CC_I);
    if (wasMasked) DispatchOneInstruction();  // see class comment
    CheckPendingInterrupts();
}

void MameDerivedHd63701Adapter::OpInx() {
    ++x_;
    cc_ &= static_cast<std::uint8_t>(~CC_Z);
    if (x_ == 0) cc_ |= CC_Z;
}

void MameDerivedHd63701Adapter::OpDex() {
    --x_;
    cc_ &= static_cast<std::uint8_t>(~CC_Z);
    if (x_ == 0) cc_ |= CC_Z;
}

void MameDerivedHd63701Adapter::OpSba() {
    std::uint32_t r = static_cast<std::uint32_t>(a_) - b_;
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V | CC_C));
    SetFlags8(a_, b_, r);
    a_ = static_cast<std::uint8_t>(r);
}

void MameDerivedHd63701Adapter::OpCba() {
    std::uint32_t r = static_cast<std::uint32_t>(a_) - b_;
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V | CC_C));
    SetFlags8(a_, b_, r);
}

void MameDerivedHd63701Adapter::OpXgdx() {
    std::uint16_t t = x_;
    x_ = GetD();
    SetD(t);
}

void MameDerivedHd63701Adapter::OpDaa() {
    std::uint8_t msn = static_cast<std::uint8_t>(a_ & 0xF0);
    std::uint8_t lsn = static_cast<std::uint8_t>(a_ & 0x0F);
    std::uint16_t cf = 0;
    if (lsn > 0x09 || (cc_ & CC_H)) cf |= 0x06;
    if (msn > 0x80 && lsn > 0x09) cf |= 0x60;
    if (msn > 0x90 || (cc_ & CC_C)) cf |= 0x60;
    std::uint16_t t = static_cast<std::uint16_t>(cf + a_);
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V));
    SetNz8(static_cast<std::uint8_t>(t));
    if (t & 0x100) cc_ |= CC_C;
    a_ = static_cast<std::uint8_t>(t);
}

void MameDerivedHd63701Adapter::OpSlp() {
    waiState_ |= kSlpFlag;
    CheckPendingInterrupts();
    // If still sleeping, Execute()'s idle loop takes over (see IHd63701Core::Execute()).
}

void MameDerivedHd63701Adapter::OpAba() {
    std::uint32_t r = static_cast<std::uint32_t>(a_) + b_;
    cc_ &= static_cast<std::uint8_t>(~(CC_H | CC_N | CC_Z | CC_V | CC_C));
    SetFlags8(a_, b_, r);
    SetH(a_, b_, r);
    a_ = static_cast<std::uint8_t>(r);
}

void MameDerivedHd63701Adapter::OpBranch(bool taken) {
    std::int8_t rel = static_cast<std::int8_t>(ReadOpcodeArg());
    if (taken) pc_ = static_cast<std::uint16_t>(pc_ + rel);
}

void MameDerivedHd63701Adapter::OpPulx() { PullWord(x_); }
void MameDerivedHd63701Adapter::OpRts() { PullWord(pc_); }

void MameDerivedHd63701Adapter::OpRti() {
    PullByte(cc_);
    PullByte(b_);
    PullByte(a_);
    PullWord(x_);
    PullWord(pc_);
    CheckPendingInterrupts();
}

void MameDerivedHd63701Adapter::OpPshx() { PushWord(x_); }

void MameDerivedHd63701Adapter::OpMul() {
    std::uint16_t t = static_cast<std::uint16_t>(static_cast<std::uint16_t>(a_) * b_);
    cc_ &= static_cast<std::uint8_t>(~CC_C);
    if (t & 0x80) cc_ |= CC_C;
    SetD(t);
}

void MameDerivedHd63701Adapter::OpWai() {
    waiState_ |= kWaiFlag;
    PushWord(pc_);
    PushWord(x_);
    PushByte(a_);
    PushByte(b_);
    PushByte(cc_);
    CheckPendingInterrupts();
    // If still waiting, Execute()'s idle loop takes over.
}

void MameDerivedHd63701Adapter::OpSwi() {
    PushWord(pc_);
    PushWord(x_);
    PushByte(a_);
    PushByte(b_);
    PushByte(cc_);
    cc_ |= CC_I;
    pc_ = ReadMemory16(VECTOR_SWI);
}

// ---------------------------------------------------------------------------
// Accumulator inherent RMW (rows 0x40-0x5F).
// ---------------------------------------------------------------------------

void MameDerivedHd63701Adapter::OpNegA() {
    std::uint32_t r = static_cast<std::uint32_t>(0) - a_;
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V | CC_C));
    SetFlags8(0, a_, r);
    a_ = static_cast<std::uint8_t>(r);
}
void MameDerivedHd63701Adapter::OpComA() {
    a_ = static_cast<std::uint8_t>(~a_);
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V));
    SetNz8(a_);
    cc_ |= CC_C;
}
void MameDerivedHd63701Adapter::OpLsrA() {
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V | CC_C));
    if (a_ & 1) cc_ |= CC_C;
    a_ = static_cast<std::uint8_t>(a_ >> 1);
    if (a_ == 0) cc_ |= CC_Z;
    if (NxorC()) cc_ |= CC_V;
}
void MameDerivedHd63701Adapter::OpRorA() {
    std::uint8_t r = static_cast<std::uint8_t>((cc_ & CC_C) << 7);
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V | CC_C));
    if (a_ & 1) cc_ |= CC_C;
    r = static_cast<std::uint8_t>(r | (a_ >> 1));
    SetNz8(r);
    if (NxorC()) cc_ |= CC_V;
    a_ = r;
}
void MameDerivedHd63701Adapter::OpAsrA() {
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V | CC_C));
    if (a_ & 1) cc_ |= CC_C;
    a_ = static_cast<std::uint8_t>(a_ >> 1);
    a_ = static_cast<std::uint8_t>(a_ | ((a_ & 0x40) << 1));
    SetNz8(a_);
    if (NxorC()) cc_ |= CC_V;
}
void MameDerivedHd63701Adapter::OpAslA() {
    std::uint32_t r = static_cast<std::uint32_t>(a_) << 1;
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V | CC_C));
    SetFlags8(a_, a_, r);
    a_ = static_cast<std::uint8_t>(r);
}
void MameDerivedHd63701Adapter::OpRolA() {
    std::uint8_t t = a_;
    std::uint32_t r = static_cast<std::uint32_t>((cc_ & CC_C) | (t << 1));
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V | CC_C));
    SetFlags8(t, t, r);
    a_ = static_cast<std::uint8_t>(r);
}
void MameDerivedHd63701Adapter::OpDecA() {
    --a_;
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V));
    SetFlags8Dec(a_);
}
void MameDerivedHd63701Adapter::OpIncA() {
    ++a_;
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V));
    SetFlags8Inc(a_);
}
void MameDerivedHd63701Adapter::OpTstA() {
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V | CC_C));
    SetNz8(a_);
}
void MameDerivedHd63701Adapter::OpClrA() {
    a_ = 0;
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V | CC_C));
    cc_ |= CC_Z;
}

void MameDerivedHd63701Adapter::OpNegB() {
    std::uint32_t r = static_cast<std::uint32_t>(0) - b_;
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V | CC_C));
    SetFlags8(0, b_, r);
    b_ = static_cast<std::uint8_t>(r);
}
void MameDerivedHd63701Adapter::OpComB() {
    b_ = static_cast<std::uint8_t>(~b_);
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V));
    SetNz8(b_);
    cc_ |= CC_C;
}
void MameDerivedHd63701Adapter::OpLsrB() {
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V | CC_C));
    if (b_ & 1) cc_ |= CC_C;
    b_ = static_cast<std::uint8_t>(b_ >> 1);
    if (b_ == 0) cc_ |= CC_Z;
    if (NxorC()) cc_ |= CC_V;
}
void MameDerivedHd63701Adapter::OpRorB() {
    std::uint8_t r = static_cast<std::uint8_t>((cc_ & CC_C) << 7);
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V | CC_C));
    if (b_ & 1) cc_ |= CC_C;
    r = static_cast<std::uint8_t>(r | (b_ >> 1));
    SetNz8(r);
    if (NxorC()) cc_ |= CC_V;
    b_ = r;
}
void MameDerivedHd63701Adapter::OpAsrB() {
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V | CC_C));
    if (b_ & 1) cc_ |= CC_C;
    b_ = static_cast<std::uint8_t>(b_ >> 1);
    b_ = static_cast<std::uint8_t>(b_ | ((b_ & 0x40) << 1));
    SetNz8(b_);
    if (NxorC()) cc_ |= CC_V;
}
void MameDerivedHd63701Adapter::OpAslB() {
    std::uint32_t r = static_cast<std::uint32_t>(b_) << 1;
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V | CC_C));
    SetFlags8(b_, b_, r);
    b_ = static_cast<std::uint8_t>(r);
}
void MameDerivedHd63701Adapter::OpRolB() {
    std::uint8_t t = b_;
    std::uint32_t r = static_cast<std::uint32_t>((cc_ & CC_C) | (t << 1));
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V | CC_C));
    SetFlags8(t, t, r);
    b_ = static_cast<std::uint8_t>(r);
}
void MameDerivedHd63701Adapter::OpDecB() {
    --b_;
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V));
    SetFlags8Dec(b_);
}
void MameDerivedHd63701Adapter::OpIncB() {
    ++b_;
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V));
    SetFlags8Inc(b_);
}
void MameDerivedHd63701Adapter::OpTstB() {
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V | CC_C));
    SetNz8(b_);
}
void MameDerivedHd63701Adapter::OpClrB() {
    b_ = 0;
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V | CC_C));
    cc_ |= CC_Z;
}

// ---------------------------------------------------------------------------
// Indexed/extended memory RMW (rows 0x60-0x7F, minus AIM/OIM/EIM/TIM below).
// ---------------------------------------------------------------------------

void MameDerivedHd63701Adapter::OpNegM(RmwMode mode) {
    std::uint16_t ea = (mode == RmwMode::Indexed) ? ModeIndexed() : ModeExtended();
    std::uint8_t t = ReadMemory(ea);
    std::uint32_t r = static_cast<std::uint32_t>(0) - t;
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V | CC_C));
    SetFlags8(0, t, r);
    WriteMemory(ea, static_cast<std::uint8_t>(r));
}
void MameDerivedHd63701Adapter::OpComM(RmwMode mode) {
    std::uint16_t ea = (mode == RmwMode::Indexed) ? ModeIndexed() : ModeExtended();
    std::uint8_t t = static_cast<std::uint8_t>(~ReadMemory(ea));
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V));
    SetNz8(t);
    cc_ |= CC_C;
    WriteMemory(ea, t);
}
void MameDerivedHd63701Adapter::OpLsrM(RmwMode mode) {
    std::uint16_t ea = (mode == RmwMode::Indexed) ? ModeIndexed() : ModeExtended();
    std::uint8_t t = ReadMemory(ea);
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V | CC_C));
    if (t & 1) cc_ |= CC_C;
    t = static_cast<std::uint8_t>(t >> 1);
    if (t == 0) cc_ |= CC_Z;
    if (NxorC()) cc_ |= CC_V;
    WriteMemory(ea, t);
}
void MameDerivedHd63701Adapter::OpRorM(RmwMode mode) {
    std::uint16_t ea = (mode == RmwMode::Indexed) ? ModeIndexed() : ModeExtended();
    std::uint8_t t = ReadMemory(ea);
    std::uint8_t r = static_cast<std::uint8_t>((cc_ & CC_C) << 7);
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V | CC_C));
    if (t & 1) cc_ |= CC_C;
    r = static_cast<std::uint8_t>(r | (t >> 1));
    SetNz8(r);
    if (NxorC()) cc_ |= CC_V;
    WriteMemory(ea, r);
}
void MameDerivedHd63701Adapter::OpAsrM(RmwMode mode) {
    std::uint16_t ea = (mode == RmwMode::Indexed) ? ModeIndexed() : ModeExtended();
    std::uint8_t t = ReadMemory(ea);
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V | CC_C));
    if (t & 1) cc_ |= CC_C;
    t = static_cast<std::uint8_t>(t >> 1);
    t = static_cast<std::uint8_t>(t | ((t & 0x40) << 1));
    SetNz8(t);
    if (NxorC()) cc_ |= CC_V;
    WriteMemory(ea, t);
}
void MameDerivedHd63701Adapter::OpAslM(RmwMode mode) {
    std::uint16_t ea = (mode == RmwMode::Indexed) ? ModeIndexed() : ModeExtended();
    std::uint8_t t = ReadMemory(ea);
    std::uint32_t r = static_cast<std::uint32_t>(t) << 1;
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V | CC_C));
    SetFlags8(t, t, r);
    WriteMemory(ea, static_cast<std::uint8_t>(r));
}
void MameDerivedHd63701Adapter::OpRolM(RmwMode mode) {
    std::uint16_t ea = (mode == RmwMode::Indexed) ? ModeIndexed() : ModeExtended();
    std::uint8_t t = ReadMemory(ea);
    std::uint32_t r = static_cast<std::uint32_t>((cc_ & CC_C) | (t << 1));
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V | CC_C));
    SetFlags8(t, t, r);
    WriteMemory(ea, static_cast<std::uint8_t>(r));
}
void MameDerivedHd63701Adapter::OpDecM(RmwMode mode) {
    std::uint16_t ea = (mode == RmwMode::Indexed) ? ModeIndexed() : ModeExtended();
    std::uint8_t t = static_cast<std::uint8_t>(ReadMemory(ea) - 1);
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V));
    SetFlags8Dec(t);
    WriteMemory(ea, t);
}
void MameDerivedHd63701Adapter::OpIncM(RmwMode mode) {
    std::uint16_t ea = (mode == RmwMode::Indexed) ? ModeIndexed() : ModeExtended();
    std::uint8_t t = static_cast<std::uint8_t>(ReadMemory(ea) + 1);
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V));
    SetFlags8Inc(t);
    WriteMemory(ea, t);
}
void MameDerivedHd63701Adapter::OpTstM(RmwMode mode) {
    std::uint16_t ea = (mode == RmwMode::Indexed) ? ModeIndexed() : ModeExtended();
    std::uint8_t t = ReadMemory(ea);
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V | CC_C));
    SetNz8(t);
}
void MameDerivedHd63701Adapter::OpJmpM(RmwMode mode) {
    // JMP computes the target address without reading through it.
    pc_ = (mode == RmwMode::Indexed) ? ModeIndexed() : ModeExtended();
}
void MameDerivedHd63701Adapter::OpClrM(RmwMode mode) {
    std::uint16_t ea = (mode == RmwMode::Indexed) ? ModeIndexed() : ModeExtended();
    ReadMemory(ea);  // dummy read before write - RM(EAD); WM(EAD,0); in 6800ops.hxx
    WriteMemory(ea, 0);
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V | CC_C));
    cc_ |= CC_Z;
}

void MameDerivedHd63701Adapter::OpAimM(BitOpMode mode) {
    std::uint8_t imm = ReadOpcodeArg();
    std::uint16_t ea = (mode == BitOpMode::Indexed) ? ModeIndexed() : ModeDirect();
    std::uint8_t r = static_cast<std::uint8_t>(ReadMemory(ea) & imm);
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V));
    SetNz8(r);
    WriteMemory(ea, r);
}
void MameDerivedHd63701Adapter::OpOimM(BitOpMode mode) {
    std::uint8_t imm = ReadOpcodeArg();
    std::uint16_t ea = (mode == BitOpMode::Indexed) ? ModeIndexed() : ModeDirect();
    std::uint8_t r = static_cast<std::uint8_t>(ReadMemory(ea) | imm);
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V));
    SetNz8(r);
    WriteMemory(ea, r);
}
void MameDerivedHd63701Adapter::OpEimM(BitOpMode mode) {
    std::uint8_t imm = ReadOpcodeArg();
    std::uint16_t ea = (mode == BitOpMode::Indexed) ? ModeIndexed() : ModeDirect();
    std::uint8_t r = static_cast<std::uint8_t>(ReadMemory(ea) ^ imm);
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V));
    SetNz8(r);
    WriteMemory(ea, r);
}
void MameDerivedHd63701Adapter::OpTimM(BitOpMode mode) {
    std::uint8_t imm = ReadOpcodeArg();
    std::uint16_t ea = (mode == BitOpMode::Indexed) ? ModeIndexed() : ModeDirect();
    std::uint8_t r = static_cast<std::uint8_t>(ReadMemory(ea) & imm);
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V));
    SetNz8(r);
    // TIM: no write-back, flags only
}

// ---------------------------------------------------------------------------
// 8-bit accumulator register-op group (rows 0x80-0xFF, A and B forms).
// ---------------------------------------------------------------------------

std::uint8_t MameDerivedHd63701Adapter::ReadOperand8(OperandMode mode, std::uint16_t& eaOut) {
    switch (mode) {
        case OperandMode::Immediate: {
            eaOut = pc_;
            return ReadOpcodeArg();
        }
        case OperandMode::Direct: eaOut = ModeDirect(); return ReadMemory(eaOut);
        case OperandMode::Indexed: eaOut = ModeIndexed(); return ReadMemory(eaOut);
        case OperandMode::Extended: eaOut = ModeExtended(); return ReadMemory(eaOut);
    }
    return 0;
}

void MameDerivedHd63701Adapter::OpSubA(OperandMode mode) {
    std::uint16_t ea; std::uint8_t t = ReadOperand8(mode, ea);
    std::uint32_t r = static_cast<std::uint32_t>(a_) - t;
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V | CC_C));
    SetFlags8(a_, t, r);
    a_ = static_cast<std::uint8_t>(r);
}
void MameDerivedHd63701Adapter::OpCmpA(OperandMode mode) {
    std::uint16_t ea; std::uint8_t t = ReadOperand8(mode, ea);
    std::uint32_t r = static_cast<std::uint32_t>(a_) - t;
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V | CC_C));
    SetFlags8(a_, t, r);
}
void MameDerivedHd63701Adapter::OpSbcA(OperandMode mode) {
    std::uint16_t ea; std::uint8_t t = ReadOperand8(mode, ea);
    std::uint32_t r = static_cast<std::uint32_t>(a_) - t - (cc_ & CC_C);
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V | CC_C));
    SetFlags8(a_, t, r);
    a_ = static_cast<std::uint8_t>(r);
}
void MameDerivedHd63701Adapter::OpAndA(OperandMode mode) {
    std::uint16_t ea; a_ = static_cast<std::uint8_t>(a_ & ReadOperand8(mode, ea));
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V));
    SetNz8(a_);
}
void MameDerivedHd63701Adapter::OpBitA(OperandMode mode) {
    std::uint16_t ea; std::uint8_t t = ReadOperand8(mode, ea);
    std::uint8_t r = static_cast<std::uint8_t>(a_ & t);
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V));
    SetNz8(r);
}
void MameDerivedHd63701Adapter::OpLdaA(OperandMode mode) {
    std::uint16_t ea; a_ = ReadOperand8(mode, ea);
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V));
    SetNz8(a_);
}
void MameDerivedHd63701Adapter::OpStaA(OperandMode mode) {
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V));
    SetNz8(a_);
    std::uint16_t ea = (mode == OperandMode::Direct) ? ModeDirect()
                      : (mode == OperandMode::Indexed) ? ModeIndexed() : ModeExtended();
    WriteMemory(ea, a_);
}
void MameDerivedHd63701Adapter::OpEorA(OperandMode mode) {
    std::uint16_t ea; a_ = static_cast<std::uint8_t>(a_ ^ ReadOperand8(mode, ea));
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V));
    SetNz8(a_);
}
void MameDerivedHd63701Adapter::OpAdcA(OperandMode mode) {
    std::uint16_t ea; std::uint8_t t = ReadOperand8(mode, ea);
    std::uint32_t r = static_cast<std::uint32_t>(a_) + t + (cc_ & CC_C);
    cc_ &= static_cast<std::uint8_t>(~(CC_H | CC_N | CC_Z | CC_V | CC_C));
    SetFlags8(a_, t, r);
    SetH(a_, t, r);
    a_ = static_cast<std::uint8_t>(r);
}
void MameDerivedHd63701Adapter::OpOraA(OperandMode mode) {
    std::uint16_t ea; a_ = static_cast<std::uint8_t>(a_ | ReadOperand8(mode, ea));
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V));
    SetNz8(a_);
}
void MameDerivedHd63701Adapter::OpAddA(OperandMode mode) {
    std::uint16_t ea; std::uint8_t t = ReadOperand8(mode, ea);
    std::uint32_t r = static_cast<std::uint32_t>(a_) + t;
    cc_ &= static_cast<std::uint8_t>(~(CC_H | CC_N | CC_Z | CC_V | CC_C));
    SetFlags8(a_, t, r);
    SetH(a_, t, r);
    a_ = static_cast<std::uint8_t>(r);
}

void MameDerivedHd63701Adapter::OpSubB(OperandMode mode) {
    std::uint16_t ea; std::uint8_t t = ReadOperand8(mode, ea);
    std::uint32_t r = static_cast<std::uint32_t>(b_) - t;
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V | CC_C));
    SetFlags8(b_, t, r);
    b_ = static_cast<std::uint8_t>(r);
}
void MameDerivedHd63701Adapter::OpCmpB(OperandMode mode) {
    std::uint16_t ea; std::uint8_t t = ReadOperand8(mode, ea);
    std::uint32_t r = static_cast<std::uint32_t>(b_) - t;
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V | CC_C));
    SetFlags8(b_, t, r);
}
void MameDerivedHd63701Adapter::OpSbcB(OperandMode mode) {
    std::uint16_t ea; std::uint8_t t = ReadOperand8(mode, ea);
    std::uint32_t r = static_cast<std::uint32_t>(b_) - t - (cc_ & CC_C);
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V | CC_C));
    SetFlags8(b_, t, r);
    b_ = static_cast<std::uint8_t>(r);
}
void MameDerivedHd63701Adapter::OpAndB(OperandMode mode) {
    std::uint16_t ea; b_ = static_cast<std::uint8_t>(b_ & ReadOperand8(mode, ea));
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V));
    SetNz8(b_);
}
void MameDerivedHd63701Adapter::OpBitB(OperandMode mode) {
    std::uint16_t ea; std::uint8_t t = ReadOperand8(mode, ea);
    std::uint8_t r = static_cast<std::uint8_t>(b_ & t);
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V));
    SetNz8(r);
}
void MameDerivedHd63701Adapter::OpLdaB(OperandMode mode) {
    std::uint16_t ea; b_ = ReadOperand8(mode, ea);
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V));
    SetNz8(b_);
}
void MameDerivedHd63701Adapter::OpStaB(OperandMode mode) {
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V));
    SetNz8(b_);
    std::uint16_t ea = (mode == OperandMode::Direct) ? ModeDirect()
                      : (mode == OperandMode::Indexed) ? ModeIndexed() : ModeExtended();
    WriteMemory(ea, b_);
}
void MameDerivedHd63701Adapter::OpEorB(OperandMode mode) {
    std::uint16_t ea; b_ = static_cast<std::uint8_t>(b_ ^ ReadOperand8(mode, ea));
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V));
    SetNz8(b_);
}
void MameDerivedHd63701Adapter::OpAdcB(OperandMode mode) {
    std::uint16_t ea; std::uint8_t t = ReadOperand8(mode, ea);
    std::uint32_t r = static_cast<std::uint32_t>(b_) + t + (cc_ & CC_C);
    cc_ &= static_cast<std::uint8_t>(~(CC_H | CC_N | CC_Z | CC_V | CC_C));
    SetFlags8(b_, t, r);
    SetH(b_, t, r);
    b_ = static_cast<std::uint8_t>(r);
}
void MameDerivedHd63701Adapter::OpOraB(OperandMode mode) {
    std::uint16_t ea; b_ = static_cast<std::uint8_t>(b_ | ReadOperand8(mode, ea));
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V));
    SetNz8(b_);
}
void MameDerivedHd63701Adapter::OpAddB(OperandMode mode) {
    std::uint16_t ea; std::uint8_t t = ReadOperand8(mode, ea);
    std::uint32_t r = static_cast<std::uint32_t>(b_) + t;
    cc_ &= static_cast<std::uint8_t>(~(CC_H | CC_N | CC_Z | CC_V | CC_C));
    SetFlags8(b_, t, r);
    SetH(b_, t, r);
    b_ = static_cast<std::uint8_t>(r);
}

// ---------------------------------------------------------------------------
// 16-bit register-op group (D/X/S), rows 0x83/93/A3/B3 (SUBD), 0xC3/D3/E3/F3
// (ADDD), 0x8C/9C/AC/BC (CPX), 0xCC/DC/EC/FC (LDD), 0xDD/ED/FD (STD),
// 0xCE/DE/EE/FE (LDX), 0xDF/EF/FF (STX), 0x8E/9E/AE/BE (LDS), 0x9F/AF/BF (STS).
// ---------------------------------------------------------------------------

std::uint16_t MameDerivedHd63701Adapter::ReadOperand16(OperandMode mode, std::uint16_t& eaOut) {
    switch (mode) {
        case OperandMode::Immediate: {
            eaOut = pc_;
            std::uint16_t hi = ReadOpcodeArg();
            std::uint16_t lo = ReadOpcodeArg();
            return static_cast<std::uint16_t>((hi << 8) | lo);
        }
        case OperandMode::Direct: eaOut = ModeDirect(); return ReadMemory16(eaOut);
        case OperandMode::Indexed: eaOut = ModeIndexed(); return ReadMemory16(eaOut);
        case OperandMode::Extended: eaOut = ModeExtended(); return ReadMemory16(eaOut);
    }
    return 0;
}

void MameDerivedHd63701Adapter::OpSubD(OperandMode mode) {
    std::uint16_t ea; std::uint16_t operand = ReadOperand16(mode, ea);
    std::uint16_t d = GetD();
    std::uint32_t r = static_cast<std::uint32_t>(d) - operand;
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V | CC_C));
    SetFlags16(d, operand, r);
    SetD(static_cast<std::uint16_t>(r));
}
void MameDerivedHd63701Adapter::OpAddD(OperandMode mode) {
    std::uint16_t ea; std::uint16_t operand = ReadOperand16(mode, ea);
    std::uint16_t d = GetD();
    std::uint32_t r = static_cast<std::uint32_t>(d) + operand;
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V | CC_C));
    SetFlags16(d, operand, r);
    SetD(static_cast<std::uint16_t>(r));
}
void MameDerivedHd63701Adapter::OpCpx(OperandMode mode) {
    std::uint16_t ea; std::uint16_t operand = ReadOperand16(mode, ea);
    std::uint32_t r = static_cast<std::uint32_t>(x_) - operand;
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V | CC_C));
    SetFlags16(x_, operand, r);
}
void MameDerivedHd63701Adapter::OpLdd(OperandMode mode) {
    std::uint16_t ea; std::uint16_t v = ReadOperand16(mode, ea);
    SetD(v);
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V));
    SetNz16(v);
}
void MameDerivedHd63701Adapter::OpStd(OperandMode mode) {
    std::uint16_t d = GetD();
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V));
    SetNz16(d);
    std::uint16_t ea = (mode == OperandMode::Direct) ? ModeDirect()
                      : (mode == OperandMode::Indexed) ? ModeIndexed() : ModeExtended();
    WriteMemory16(ea, d);
}
void MameDerivedHd63701Adapter::OpLdx(OperandMode mode) {
    std::uint16_t ea; x_ = ReadOperand16(mode, ea);
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V));
    SetNz16(x_);
}
void MameDerivedHd63701Adapter::OpStx(OperandMode mode) {
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V));
    SetNz16(x_);
    std::uint16_t ea = (mode == OperandMode::Direct) ? ModeDirect()
                      : (mode == OperandMode::Indexed) ? ModeIndexed() : ModeExtended();
    WriteMemory16(ea, x_);
}
void MameDerivedHd63701Adapter::OpLds(OperandMode mode) {
    std::uint16_t ea; s_ = ReadOperand16(mode, ea);
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V));
    SetNz16(s_);
}
void MameDerivedHd63701Adapter::OpSts(OperandMode mode) {
    cc_ &= static_cast<std::uint8_t>(~(CC_N | CC_Z | CC_V));
    SetNz16(s_);
    std::uint16_t ea = (mode == OperandMode::Direct) ? ModeDirect()
                      : (mode == OperandMode::Indexed) ? ModeIndexed() : ModeExtended();
    WriteMemory16(ea, s_);
}

void MameDerivedHd63701Adapter::OpBsr() {
    std::int8_t rel = static_cast<std::int8_t>(ReadOpcodeArg());
    PushWord(pc_);
    pc_ = static_cast<std::uint16_t>(pc_ + rel);
}

void MameDerivedHd63701Adapter::OpJsrM(JsrMode mode) {
    std::uint16_t ea = (mode == JsrMode::Direct) ? ModeDirect()
                      : (mode == JsrMode::Indexed) ? ModeIndexed() : ModeExtended();
    PushWord(pc_);
    pc_ = ea;
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

MameDerivedHd63701Adapter::RegisterSnapshot MameDerivedHd63701Adapter::Snapshot() const {
    RegisterSnapshot snap;
    snap.pc = pc_;
    snap.s = s_;
    snap.x = x_;
    snap.a = a_;
    snap.b = b_;
    snap.cc = cc_;
    snap.nmiLine = nmiLine_;
    snap.irq1Line = irq1Line_;
    snap.waiWaiting = (waiState_ & kWaiFlag) != 0;
    snap.slpWaiting = (waiState_ & kSlpFlag) != 0;
    snap.nmiServiceCount = nmiServiceCount_;
    snap.irq1ServiceCount = irq1ServiceCount_;
    snap.irq2ServiceCount = irq2ServiceCount_;
    snap.trapCount = trapCount_;
    return snap;
}
