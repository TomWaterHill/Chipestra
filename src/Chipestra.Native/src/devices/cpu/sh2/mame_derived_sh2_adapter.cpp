// license:BSD-3-Clause
// copyright-holders:Juergen Buchmueller, R. Belmont
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289): see mame_derived_sh2_adapter.h.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#include "mame_derived_sh2_adapter.h"

#include <bit>

namespace {
inline std::uint32_t Sext8(std::uint32_t v) { return static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(v))); }
inline std::uint32_t Sext16(std::uint32_t v) { return static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(v))); }
inline std::uint32_t Bit31(std::uint32_t v) { return v >> 31; }
}  // namespace

void MameDerivedSh2Adapter::Reset() {
    for (auto& r : r_) r = 0;
    pr_ = gbr_ = vbr_ = mach_ = macl_ = 0;
    hasDelay_ = false;
    delayTarget_ = 0;
    pendingExternal_ = 0;
    pendingNmi_ = false;
    nmiLine_ = false;
    internalLevel_ = 0;
    internalVector_ = 0;
    sleepMode_ = 0;
    exceptionCount_ = 0;
    sr_ = kI;
    pc_ = Read32(0);
    r_[15] = Read32(4);
}

MameDerivedSh2Adapter::Registers MameDerivedSh2Adapter::GetRegisters() const {
    Registers out;
    for (int i = 0; i < 16; ++i) out.r[i] = r_[i];
    out.pc = pc_; out.pr = pr_; out.sr = sr_; out.gbr = gbr_; out.vbr = vbr_; out.mach = mach_; out.macl = macl_;
    out.inDelaySlot = hasDelay_;
    out.exceptionCount = exceptionCount_;
    return out;
}

void MameDerivedSh2Adapter::SetRegisters(const Registers& in) {
    for (int i = 0; i < 16; ++i) r_[i] = in.r[i];
    pc_ = in.pc; pr_ = in.pr; sr_ = in.sr & kSrMask; gbr_ = in.gbr; vbr_ = in.vbr; mach_ = in.mach; macl_ = in.macl;
    hasDelay_ = false;
}

void MameDerivedSh2Adapter::SetInterruptLine(int level, bool asserted) {
    if (level < 1 || level > 15) return;
    if (asserted) pendingExternal_ |= 1u << level; else pendingExternal_ &= ~(1u << level);
}

void MameDerivedSh2Adapter::SetNmi(bool asserted) {
    if (asserted && !nmiLine_) pendingNmi_ = true;
    nmiLine_ = asserted;
}

void MameDerivedSh2Adapter::SetInternalInterrupt(int level, std::uint32_t vector) {
    internalLevel_ = level;
    internalVector_ = vector;
}

int MameDerivedSh2Adapter::PendingLevel() const {
    if (pendingNmi_) return 16;
    int level = internalLevel_;
    if (pendingExternal_ != 0) {
        const int external = std::bit_width(pendingExternal_) - 1;
        if (external >= level) level = external;
    }
    return level;
}

void MameDerivedSh2Adapter::PushAndVector(int vector) {
    r_[15] -= 4;
    Write32(r_[15], sr_);
    r_[15] -= 4;
    Write32(r_[15], pc_);
    pc_ = Read32(vbr_ + static_cast<std::uint32_t>(vector) * 4);
}

void MameDerivedSh2Adapter::TakeException(int level) {
    int vector;
    if (level == 16) {
        pendingNmi_ = false;
        vector = 11;
        PushAndVector(vector);
        sr_ |= kI;
    } else {
        if (level <= static_cast<int>((sr_ >> 4) & 15)) return;   // the mask forbids it: it stays pending
        if (internalLevel_ == level) {
            vector = static_cast<int>(internalVector_);
            internalLevel_ = 0;                                   // taken once
        } else {
            vector = cb_.irqAcknowledge ? static_cast<int>(cb_.irqAcknowledge(level)) : 64 + level / 2;
        }
        PushAndVector(vector);
        sr_ = (sr_ & ~kI) | (static_cast<std::uint32_t>(level) << 4);
    }
    ++exceptionCount_;
    if (sleepMode_ == 1) sleepMode_ = 2;
}

std::uint64_t MameDerivedSh2Adapter::Execute(std::uint64_t cycleBudget) {
    std::uint64_t consumed = 0;
    executing_ = true;
    do {
        consumedBefore_ = consumed;
        if (!hasDelay_ && (pendingNmi_ || internalLevel_ != 0 || pendingExternal_ != 0)) {
            const int level = PendingLevel();
            if (level > 0) TakeException(level);
        }
        const std::uint16_t opcode = Read16(pc_);
        if (hasDelay_) {
            pc_ = delayTarget_;
            hasDelay_ = false;
        } else {
            pc_ += 2;
        }
        cycles_ = 1;
        ExecuteOne(opcode);
        consumed += cycles_;
    } while (consumed < cycleBudget);
    executing_ = false;
    return consumed;
}

void MameDerivedSh2Adapter::Illegal() {
    r_[15] -= 4;
    Write32(r_[15], sr_);
    r_[15] -= 4;
    Write32(r_[15], pc_ - 2);
    pc_ = Read32(vbr_ + 4 * 4);
    cycles_ += 5;
}

void MameDerivedSh2Adapter::Div1(std::uint32_t m, std::uint32_t n) {
    const std::uint32_t oldQ = sr_ & kQ;
    if (r_[n] & 0x80000000u) sr_ |= kQ; else sr_ &= ~kQ;
    r_[n] = (r_[n] << 1) | (sr_ & kT);
    const std::uint32_t tmp = r_[n];
    if (!oldQ) {
        if (!(sr_ & kM)) {
            r_[n] -= r_[m];
            if (!(sr_ & kQ)) { if (r_[n] > tmp) sr_ |= kQ; else sr_ &= ~kQ; }
            else { if (r_[n] > tmp) sr_ &= ~kQ; else sr_ |= kQ; }
        } else {
            r_[n] += r_[m];
            if (!(sr_ & kQ)) { if (r_[n] < tmp) sr_ &= ~kQ; else sr_ |= kQ; }
            else { if (r_[n] < tmp) sr_ |= kQ; else sr_ &= ~kQ; }
        }
    } else {
        if (!(sr_ & kM)) {
            r_[n] += r_[m];
            if (!(sr_ & kQ)) { if (r_[n] < tmp) sr_ |= kQ; else sr_ &= ~kQ; }
            else { if (r_[n] < tmp) sr_ &= ~kQ; else sr_ |= kQ; }
        } else {
            r_[n] -= r_[m];
            if (!(sr_ & kQ)) { if (r_[n] > tmp) sr_ &= ~kQ; else sr_ |= kQ; }
            else { if (r_[n] > tmp) sr_ |= kQ; else sr_ &= ~kQ; }
        }
    }
    const std::uint32_t qm = sr_ & (kQ | kM);
    SetT(qm == 0 || qm == (kQ | kM));
}

void MameDerivedSh2Adapter::MacL(std::uint32_t m, std::uint32_t n) {
    const std::int64_t tempn = static_cast<std::int32_t>(Read32(r_[n]));
    r_[n] += 4;
    const std::int64_t tempm = static_cast<std::int32_t>(Read32(r_[m]));
    r_[m] += 4;
    // MACH:MACL is a 64-bit accumulator; with S set the sum saturates at 48 bits (MACH holds the sign-extended top 16 bits).
    // (MAME adds only the low 16 bits of MACH and stores 0x8000 for a negative overflow; this follows the Hitachi manual.)
    const std::int64_t accumulator = static_cast<std::int64_t>((static_cast<std::uint64_t>(mach_) << 32) | macl_);
    const std::int64_t product = tempn * tempm;
    std::int64_t sum = static_cast<std::int64_t>(static_cast<std::uint64_t>(accumulator) + static_cast<std::uint64_t>(product));
    if (sr_ & kS) {
        const std::int64_t high = (static_cast<std::int64_t>(1) << 47) - 1;
        const std::int64_t low = -(static_cast<std::int64_t>(1) << 47);
        const bool overflow64 = ((accumulator ^ sum) & (product ^ sum)) < 0;
        if (overflow64) sum = product < 0 ? low : high;
        else if (sum > high) sum = high;
        else if (sum < low) sum = low;
    }
    mach_ = static_cast<std::uint32_t>(static_cast<std::uint64_t>(sum) >> 32);
    macl_ = static_cast<std::uint32_t>(sum);
    cycles_ += 2;
}

void MameDerivedSh2Adapter::MacW(std::uint32_t m, std::uint32_t n) {
    std::int32_t tempn = static_cast<std::int16_t>(Read16(r_[n]));
    r_[n] += 2;
    std::int32_t tempm = static_cast<std::int16_t>(Read16(r_[m]));
    r_[m] += 2;
    const std::uint32_t templ = macl_;
    tempm *= tempn;
    const std::int32_t dest = static_cast<std::int32_t>(Bit31(macl_));
    const std::int32_t src = static_cast<std::int32_t>(Bit31(static_cast<std::uint32_t>(tempm))) + dest;
    tempn = Bit31(static_cast<std::uint32_t>(tempm)) ? -1 : 0;
    macl_ += static_cast<std::uint32_t>(tempm);
    const std::int32_t ans = static_cast<std::int32_t>(Bit31(macl_)) + dest;
    if (sr_ & kS) {
        if (ans == 1) {
            if (src == 0) macl_ = 0x7fffffffu;
            else if (src == 2) macl_ = 0x80000000u;
        }
    } else {
        mach_ += static_cast<std::uint32_t>(tempn);
        if (templ > macl_) mach_ += 1;
    }
    cycles_ += 2;
}

void MameDerivedSh2Adapter::ExecuteOne(std::uint16_t op) {
    const std::uint32_t n = (op >> 8) & 15, m = (op >> 4) & 15, d4 = op & 15, d8 = op & 0xff;
    switch (op >> 12) {
    case 0x0: ExecuteGroup0(op); break;
    case 0x1: Write32(r_[n] + d4 * 4, r_[m]); break;                                  // MOV.L Rm,@(disp,Rn)
    case 0x2: ExecuteGroup2(op); break;
    case 0x3: ExecuteGroup3(op); break;
    case 0x4: ExecuteGroup4(op); break;
    case 0x5: r_[n] = Read32(r_[m] + d4 * 4); break;                                  // MOV.L @(disp,Rm),Rn
    case 0x6: ExecuteGroup6(op); break;
    case 0x7: r_[n] += Sext8(d8); break;                                              // ADD #imm,Rn
    case 0x8: ExecuteGroup8(op); break;
    case 0x9: r_[n] = Sext16(Read16(pc_ + d8 * 2 + 2)); break;                        // MOV.W @(disp,PC),Rn
    case 0xA: {                                                                       // BRA
        const std::int32_t disp = static_cast<std::int32_t>(Sext16((op & 0xfff) << 4)) >> 4;
        Branch(pc_ + static_cast<std::uint32_t>(disp * 2) + 2);
        cycles_ += 1;
        break;
    }
    case 0xB: {                                                                       // BSR
        pr_ = pc_ + 2;
        const std::int32_t disp = static_cast<std::int32_t>(Sext16((op & 0xfff) << 4)) >> 4;
        Branch(pc_ + static_cast<std::uint32_t>(disp * 2) + 2);
        cycles_ += 1;
        break;
    }
    case 0xC: ExecuteGroupC(op); break;
    case 0xD: r_[n] = Read32(((pc_ + 2) & ~3u) + d8 * 4); break;                      // MOV.L @(disp,PC),Rn
    case 0xE: r_[n] = Sext8(d8); break;                                               // MOV #imm,Rn
    default: Illegal(); break;
    }
}

void MameDerivedSh2Adapter::ExecuteGroup0(std::uint16_t op) {
    const std::uint32_t n = (op >> 8) & 15, m = (op >> 4) & 15;
    switch (op & 0xf) {
    case 0x2:
        switch ((op >> 4) & 3) {
        case 0: r_[n] = sr_; break;
        case 1: r_[n] = gbr_; break;
        case 2: r_[n] = vbr_; break;
        default: Illegal(); break;
        }
        break;
    case 0x3:
        switch ((op >> 4) & 3) {
        case 0: pr_ = pc_ + 2; Branch(pc_ + r_[n] + 2); cycles_ += 1; break;       // BSRF
        case 2: Branch(pc_ + r_[n] + 2); cycles_ += 1; break;                      // BRAF
        default: Illegal(); break;
        }
        break;
    case 0x4: Write8(r_[0] + r_[n], static_cast<std::uint8_t>(r_[m])); break;       // MOV.B Rm,@(R0,Rn)
    case 0x5: Write16(r_[0] + r_[n], static_cast<std::uint16_t>(r_[m])); break;
    case 0x6: Write32(r_[0] + r_[n], r_[m]); break;
    case 0x7: macl_ = r_[n] * r_[m]; cycles_ += 1; break;                           // MUL.L
    case 0x8:
        switch ((op >> 4) & 3) {
        case 0: sr_ &= ~kT; break;                                                   // CLRT
        case 1: sr_ |= kT; break;                                                    // SETT
        case 2: mach_ = 0; macl_ = 0; break;                                         // CLRMAC
        default: Illegal(); break;
        }
        break;
    case 0x9:
        switch ((op >> 4) & 3) {
        case 0: break;                                                               // NOP
        case 1: sr_ &= ~(kM | kQ | kT); break;                                       // DIV0U
        case 2: r_[n] = sr_ & kT; break;                                             // MOVT
        default: Illegal(); break;
        }
        break;
    case 0xA:
        switch ((op >> 4) & 3) {
        case 0: r_[n] = mach_; break;
        case 1: r_[n] = macl_; break;
        case 2: r_[n] = pr_; break;
        default: Illegal(); break;
        }
        break;
    case 0xB:
        switch ((op >> 4) & 3) {
        case 0: Branch(pr_); cycles_ += 1; break;                                    // RTS
        case 1:                                                                      // SLEEP
            if (sleepMode_ != 2) pc_ -= 2;
            cycles_ += 2;
            if (sleepMode_ == 0) sleepMode_ = 1; else if (sleepMode_ == 2) sleepMode_ = 0;
            break;
        case 2: {                                                                    // RTE
            const std::uint32_t target = Read32(r_[15]);
            r_[15] += 4;
            sr_ = Read32(r_[15]) & kSrMask;
            r_[15] += 4;
            Branch(target);
            cycles_ += 3;
            break;
        }
        default: Illegal(); break;
        }
        break;
    case 0xC: r_[n] = Sext8(Read8(r_[0] + r_[m])); break;                           // MOV.B @(R0,Rm),Rn
    case 0xD: r_[n] = Sext16(Read16(r_[0] + r_[m])); break;
    case 0xE: r_[n] = Read32(r_[0] + r_[m]); break;
    case 0xF: MacL(m, n); break;
    default: Illegal(); break;
    }
}

void MameDerivedSh2Adapter::ExecuteGroup2(std::uint16_t op) {
    const std::uint32_t n = (op >> 8) & 15, m = (op >> 4) & 15;
    switch (op & 0xf) {
    case 0x0: Write8(r_[n], static_cast<std::uint8_t>(r_[m])); break;
    case 0x1: Write16(r_[n], static_cast<std::uint16_t>(r_[m])); break;
    case 0x2: Write32(r_[n], r_[m]); break;
    case 0x4: { const std::uint8_t data = static_cast<std::uint8_t>(r_[m]); r_[n] -= 1; Write8(r_[n], data); break; }
    case 0x5: { const std::uint16_t data = static_cast<std::uint16_t>(r_[m]); r_[n] -= 2; Write16(r_[n], data); break; }
    case 0x6: { const std::uint32_t data = r_[m]; r_[n] -= 4; Write32(r_[n], data); break; }
    case 0x7:                                                                         // DIV0S
        if (r_[n] >> 31) sr_ |= kQ; else sr_ &= ~kQ;
        if (r_[m] >> 31) sr_ |= kM; else sr_ &= ~kM;
        SetT(Bit31(r_[m] ^ r_[n]) != 0);
        break;
    case 0x8: SetT((r_[n] & r_[m]) == 0); break;                                      // TST
    case 0x9: r_[n] &= r_[m]; break;
    case 0xA: r_[n] ^= r_[m]; break;
    case 0xB: r_[n] |= r_[m]; break;
    case 0xC: {                                                                       // CMP/STR
        const std::uint32_t t = r_[n] ^ r_[m];
        SetT(!((t >> 24) & 0xff) || !((t >> 16) & 0xff) || !((t >> 8) & 0xff) || !(t & 0xff));
        break;
    }
    case 0xD: r_[n] = (r_[n] >> 16) | (r_[m] << 16); break;                           // XTRCT
    case 0xE: macl_ = static_cast<std::uint16_t>(r_[n]) * static_cast<std::uint16_t>(r_[m]); break;   // MULU.W
    case 0xF: macl_ = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(r_[n])) * static_cast<std::int32_t>(static_cast<std::int16_t>(r_[m]))); break;   // MULS.W
    default: Illegal(); break;
    }
}

void MameDerivedSh2Adapter::ExecuteGroup3(std::uint16_t op) {
    const std::uint32_t n = (op >> 8) & 15, m = (op >> 4) & 15;
    switch (op & 0xf) {
    case 0x0: SetT(r_[n] == r_[m]); break;
    case 0x2: SetT(r_[n] >= r_[m]); break;                                            // CMP/HS
    case 0x3: SetT(static_cast<std::int32_t>(r_[n]) >= static_cast<std::int32_t>(r_[m])); break;
    case 0x4: Div1(m, n); break;
    case 0x5: {                                                                       // DMULU.L
        const std::uint64_t p = static_cast<std::uint64_t>(r_[n]) * r_[m];
        mach_ = static_cast<std::uint32_t>(p >> 32);
        macl_ = static_cast<std::uint32_t>(p);
        cycles_ += 1;
        break;
    }
    case 0x6: SetT(r_[n] > r_[m]); break;                                             // CMP/HI
    case 0x7: SetT(static_cast<std::int32_t>(r_[n]) > static_cast<std::int32_t>(r_[m])); break;
    case 0x8: r_[n] -= r_[m]; break;
    case 0xA: {                                                                       // SUBC
        const std::uint32_t tmp1 = r_[n] - r_[m];
        const std::uint32_t tmp0 = r_[n];
        r_[n] = tmp1 - (sr_ & kT);
        SetT(tmp0 < tmp1);
        if (tmp1 < r_[n]) sr_ |= kT;
        break;
    }
    case 0xB: {                                                                       // SUBV
        const std::int32_t dest = static_cast<std::int32_t>(Bit31(r_[n]));
        std::int32_t src = static_cast<std::int32_t>(Bit31(r_[m]));
        src += dest;
        r_[n] -= r_[m];
        const std::int32_t ans = static_cast<std::int32_t>(Bit31(r_[n])) + dest;
        SetT(src == 1 && ans == 1);
        break;
    }
    case 0xC: r_[n] += r_[m]; break;
    case 0xD: {                                                                       // DMULS.L
        const std::int64_t p = static_cast<std::int64_t>(static_cast<std::int32_t>(r_[n])) * static_cast<std::int32_t>(r_[m]);
        mach_ = static_cast<std::uint32_t>(static_cast<std::uint64_t>(p) >> 32);
        macl_ = static_cast<std::uint32_t>(p);
        cycles_ += 1;
        break;
    }
    case 0xE: {                                                                       // ADDC
        const std::uint32_t tmp1 = r_[n] + r_[m];
        const std::uint32_t tmp0 = r_[n];
        r_[n] = tmp1 + (sr_ & kT);
        SetT(tmp0 > tmp1);
        if (tmp1 > r_[n]) sr_ |= kT;
        break;
    }
    case 0xF: {                                                                       // ADDV
        const std::int32_t dest = static_cast<std::int32_t>(Bit31(r_[n]));
        std::int32_t src = static_cast<std::int32_t>(Bit31(r_[m]));
        src += dest;
        r_[n] += r_[m];
        const std::int32_t ans = static_cast<std::int32_t>(Bit31(r_[n])) + dest;
        if (src != 1) SetT(ans == 1); else sr_ &= ~kT;
        break;
    }
    default: Illegal(); break;
    }
}

void MameDerivedSh2Adapter::ExecuteGroup4(std::uint16_t op) {
    const std::uint32_t n = (op >> 8) & 15, m = n;
    const std::uint32_t sub = (op >> 4) & 3;
    switch (op & 0xf) {
    case 0x0:
        if (sub == 0 || sub == 2) { sr_ = (sr_ & ~kT) | (r_[n] >> 31); r_[n] <<= 1; }       // SHLL, SHAL
        else if (sub == 1) { r_[n] -= 1; SetT(r_[n] == 0); }                                  // DT
        else Illegal();
        break;
    case 0x1:
        if (sub == 0) { sr_ = (sr_ & ~kT) | (r_[n] & kT); r_[n] >>= 1; }                     // SHLR
        else if (sub == 1) SetT(static_cast<std::int32_t>(r_[n]) >= 0);                       // CMP/PZ
        else if (sub == 2) { sr_ = (sr_ & ~kT) | (r_[n] & kT); r_[n] = static_cast<std::uint32_t>(static_cast<std::int32_t>(r_[n]) >> 1); }   // SHAR
        else Illegal();
        break;
    case 0x2:
    case 0x3:
        if (sub > 2) { Illegal(); break; }
        {
            std::uint32_t value;
            const bool control = (op & 0xf) == 0x3;
            if (!control) value = sub == 0 ? mach_ : sub == 1 ? macl_ : pr_;               // STS.L
            else value = sub == 0 ? sr_ : sub == 1 ? gbr_ : vbr_;                           // STC.L
            r_[n] -= 4;
            Write32(r_[n], value);
            if (control) cycles_ += 1;
        }
        break;
    case 0x4:
        if (sub == 0) { const std::uint32_t t = r_[n] >> 31; r_[n] = (r_[n] << 1) | t; SetT(t != 0); }           // ROTL
        else if (sub == 2) { const std::uint32_t t = r_[n] >> 31; r_[n] = (r_[n] << 1) | (sr_ & kT); SetT(t != 0); }   // ROTCL
        else Illegal();
        break;
    case 0x5:
        if (sub == 0) { const std::uint32_t t = r_[n] & 1; r_[n] = (r_[n] >> 1) | (t << 31); SetT(t != 0); }      // ROTR
        else if (sub == 1) SetT(static_cast<std::int32_t>(r_[n]) > 0);                                              // CMP/PL
        else if (sub == 2) { const std::uint32_t t = (sr_ & kT) << 31; SetT((r_[n] & 1) != 0); r_[n] = (r_[n] >> 1) | t; }   // ROTCR
        else Illegal();
        break;
    case 0x6:
    case 0x7:
        if (sub > 2) { Illegal(); break; }
        {
            const std::uint32_t value = Read32(r_[m]);
            r_[m] += 4;
            if ((op & 0xf) == 0x6) { if (sub == 0) mach_ = value; else if (sub == 1) macl_ = value; else pr_ = value; }   // LDS.L
            else {                                                                                                         // LDC.L
                if (sub == 0) sr_ = value & kSrMask; else if (sub == 1) gbr_ = value; else vbr_ = value;
                cycles_ += 2;
            }
        }
        break;
    case 0x8:
        if (sub == 0) r_[n] <<= 2; else if (sub == 1) r_[n] <<= 8; else if (sub == 2) r_[n] <<= 16; else Illegal();
        break;
    case 0x9:
        if (sub == 0) r_[n] >>= 2; else if (sub == 1) r_[n] >>= 8; else if (sub == 2) r_[n] >>= 16; else Illegal();
        break;
    case 0xA:
        if (sub == 0) mach_ = r_[m]; else if (sub == 1) macl_ = r_[m]; else if (sub == 2) pr_ = r_[m]; else Illegal();
        break;
    case 0xB:
        if (sub == 0) { pr_ = pc_ + 2; Branch(r_[n]); cycles_ += 1; }                      // JSR
        else if (sub == 1) {                                                                // TAS.B
            const std::uint8_t v = Read8(r_[n]);
            SetT(v == 0);
            Write8(r_[n], static_cast<std::uint8_t>(v | 0x80));
            cycles_ += 3;
        } else if (sub == 2) { Branch(r_[n]); cycles_ += 1; }                               // JMP: two cycles as in the manual (MAME charges one)
        else Illegal();
        break;
    case 0xE:
        if (sub == 0) sr_ = r_[m] & kSrMask; else if (sub == 1) gbr_ = r_[m]; else if (sub == 2) vbr_ = r_[m]; else Illegal();
        break;
    case 0xF: MacW((op >> 4) & 15, n); break;
    default: Illegal(); break;
    }
}

void MameDerivedSh2Adapter::ExecuteGroup6(std::uint16_t op) {
    const std::uint32_t n = (op >> 8) & 15, m = (op >> 4) & 15;
    switch (op & 0xf) {
    case 0x0: r_[n] = Sext8(Read8(r_[m])); break;
    case 0x1: r_[n] = Sext16(Read16(r_[m])); break;
    case 0x2: r_[n] = Read32(r_[m]); break;
    case 0x3: r_[n] = r_[m]; break;
    case 0x4: r_[n] = Sext8(Read8(r_[m])); if (n != m) r_[m] += 1; break;
    case 0x5: r_[n] = Sext16(Read16(r_[m])); if (n != m) r_[m] += 2; break;
    case 0x6: r_[n] = Read32(r_[m]); if (n != m) r_[m] += 4; break;
    case 0x7: r_[n] = ~r_[m]; break;
    case 0x8: { const std::uint32_t s = r_[m]; r_[n] = (s & 0xffff0000u) | ((s & 0xff) << 8) | ((s >> 8) & 0xff); break; }   // SWAP.B
    case 0x9: r_[n] = (r_[m] << 16) | (r_[m] >> 16); break;
    case 0xA: {                                                                       // NEGC
        const std::uint32_t t = r_[m];
        r_[n] = 0u - t - (sr_ & kT);
        SetT(t != 0 || (sr_ & kT) != 0);
        break;
    }
    case 0xB: r_[n] = 0u - r_[m]; break;
    case 0xC: r_[n] = r_[m] & 0xff; break;
    case 0xD: r_[n] = r_[m] & 0xffff; break;
    case 0xE: r_[n] = Sext8(r_[m]); break;
    case 0xF: r_[n] = Sext16(r_[m]); break;
    }
}

void MameDerivedSh2Adapter::ExecuteGroup8(std::uint16_t op) {
    const std::uint32_t m = (op >> 4) & 15, d4 = op & 15, d8 = op & 0xff;
    switch ((op >> 8) & 15) {
    case 0x0: Write8(r_[m] + d4, static_cast<std::uint8_t>(r_[0])); break;
    case 0x1: Write16(r_[m] + d4 * 2, static_cast<std::uint16_t>(r_[0])); break;
    case 0x4: r_[0] = Sext8(Read8(r_[m] + d4)); break;
    case 0x5: r_[0] = Sext16(Read16(r_[m] + d4 * 2)); break;
    case 0x8: SetT(r_[0] == Sext8(d8)); break;                                         // CMP/EQ #imm,R0
    case 0x9: if (sr_ & kT) { pc_ = pc_ + Sext8(d8) * 2 + 2; cycles_ += 2; } break;     // BT
    case 0xB: if (!(sr_ & kT)) { pc_ = pc_ + Sext8(d8) * 2 + 2; cycles_ += 2; } break;  // BF
    case 0xD: if (sr_ & kT) { Branch(pc_ + Sext8(d8) * 2 + 2); cycles_ += 1; } break;   // BT/S
    case 0xF: if (!(sr_ & kT)) { Branch(pc_ + Sext8(d8) * 2 + 2); cycles_ += 1; } break;   // BF/S
    default: Illegal(); break;
    }
}

void MameDerivedSh2Adapter::ExecuteGroupC(std::uint16_t op) {
    const std::uint32_t d8 = op & 0xff;
    switch ((op >> 8) & 15) {
    case 0x0: Write8(gbr_ + d8, static_cast<std::uint8_t>(r_[0])); break;
    case 0x1: Write16(gbr_ + d8 * 2, static_cast<std::uint16_t>(r_[0])); break;
    case 0x2: Write32(gbr_ + d8 * 4, r_[0]); break;
    case 0x3: {                                                                       // TRAPA
        const std::uint32_t ea = vbr_ + d8 * 4;
        r_[15] -= 4;
        Write32(r_[15], sr_);
        r_[15] -= 4;
        Write32(r_[15], pc_);
        pc_ = Read32(ea);
        cycles_ += 7;
        break;
    }
    case 0x4: r_[0] = Sext8(Read8(gbr_ + d8)); break;
    case 0x5: r_[0] = Sext16(Read16(gbr_ + d8 * 2)); break;
    case 0x6: r_[0] = Read32(gbr_ + d8 * 4); break;
    case 0x7: r_[0] = ((pc_ + 2) & ~3u) + d8 * 4; break;                              // MOVA
    case 0x8: SetT((r_[0] & d8) == 0); break;
    case 0x9: r_[0] &= d8; break;
    case 0xA: r_[0] ^= d8; break;
    case 0xB: r_[0] |= d8; break;
    case 0xC: SetT((d8 & Read8(gbr_ + r_[0])) == 0); cycles_ += 2; break;              // TST.B
    case 0xD: { const std::uint32_t ea = gbr_ + r_[0]; Write8(ea, static_cast<std::uint8_t>(d8 & Read8(ea))); cycles_ += 2; break; }
    case 0xE: { const std::uint32_t ea = gbr_ + r_[0]; Write8(ea, static_cast<std::uint8_t>(Read8(ea) ^ d8)); cycles_ += 2; break; }
    case 0xF: { const std::uint32_t ea = gbr_ + r_[0]; Write8(ea, static_cast<std::uint8_t>(Read8(ea) | d8)); cycles_ += 2; break; }
    }
}
