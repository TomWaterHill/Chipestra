// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
//
// Behaviour after MAME's sh7604 device (BSD-3-Clause, src/devices/cpu/sh/sh7604.cpp, copyright-holders Juergen Buchmueller,
// R. Belmont, Olivier Galibert, Angelo Salese); see sh7604_onchip.h.

#include "sh7604_onchip.h"

namespace {
constexpr std::uint8_t kIcf = 0x80, kOcfa = 0x08, kOcfb = 0x04, kOvf = 0x02, kCclra = 0x01;
constexpr int kFrtShift[4] = {3, 5, 7, -1};
constexpr int kWdtShift[8] = {1, 6, 7, 8, 9, 10, 12, 13};
}  // namespace

void Sh7604OnChip::Reset() {
    for (auto& b : storage_) b = 0;
    storage_[0x04] = 0x84;   // SSR: transmit empty
    tier_ = 1; ftcsr_ = 0; frcTcr_ = 0; tocr_ = 0;
    frc_ = 0; ocra_ = 0xFFFF; ocrb_ = 0xFFFF; icr_frt_ = 0; frcRemainder_ = 0;
    wtcsr_ = 0; wtcnt_ = 0; rstcsr_ = 0; wdtRemainder_ = 0;
    ipra_ = iprb_ = vcra_ = vcrb_ = vcrc_ = vcrd_ = vcrwdt_ = icr_ = 0;
    vcrdiv_ = 0; vcrdma_[0] = vcrdma_[1] = 0;
    dvsr_ = dvdntl_ = dvdnth_ = 0; divuOvf_ = divuOvfie_ = false;
    bcr1_ = 0x03F0; bcr2_ = 0x00FC; wcr_ = 0xAAFF; mcr_ = 0; rtcsr_ = 0; rtcnt_ = 0; rtcor_ = 0;
    ccr_ = 0; sbycr_ = 0;
    core_.SetInternalInterrupt(0, 0);
    pending_ = 0;
    UpdateFast();
}

int Sh7604OnChip::SizeOfRegisterAt(std::uint32_t o, std::uint32_t* base) {
    auto set = [&](std::uint32_t b, int n) { *base = b; return n; };
    if (o < 0x06) return set(o, 1);
    if (o == 0x10 || o == 0x11) return set(o, 1);
    if (o == 0x12 || o == 0x13) return set(0x12, 2);
    if (o == 0x14 || o == 0x15) return set(0x14, 2);
    if (o == 0x16 || o == 0x17) return set(o, 1);
    if (o == 0x18 || o == 0x19) return set(0x18, 2);
    if (o >= 0x60 && o < 0x6A) return set(o & ~1u, 2);
    if (o == 0x71 || o == 0x72) return set(o, 1);
    if (o >= 0x80 && o < 0x84) return set(o & ~1u, 2);
    if (o >= 0x90 && o < 0x93) return set(o, 1);
    if (o >= 0xE0 && o < 0xE6) return set(o & ~1u, 2);
    if (o >= 0x100 && o < 0x120) return set(o & ~3u, 4);
    if ((o >= 0x140 && o < 0x144) || (o >= 0x160 && o < 0x164)) return set(o & ~1u, 2);
    if (o >= 0x180 && o < 0x1B4) return set(o & ~3u, 4);
    if (o >= 0x1E0 && o < 0x1FC) return set(o & ~3u, 4);
    return set(o, 1);
}

std::uint32_t Sh7604OnChip::ReadRegister(std::uint32_t o) {
    switch (o) {
    case 0x10: return tier_;
    case 0x11: return ftcsr_;
    case 0x12: return frc_;
    case 0x14: return (tocr_ & 0x10) ? ocrb_ : ocra_;
    case 0x16: return frcTcr_ & 0x83;
    case 0x17: return (tocr_ & 0x13) | 0xE0;
    case 0x18: return icr_frt_;
    case 0x60: return iprb_ & 0xFF00;
    case 0x62: return vcra_ & 0x7F7F;
    case 0x64: return vcrb_ & 0x7F7F;
    case 0x66: return vcrc_ & 0x7F7F;
    case 0x68: return vcrd_ & 0x7F00;
    case 0x80: return static_cast<std::uint32_t>(((wtcsr_ | 0x18) << 8) | wtcnt_);
    case 0x82: return static_cast<std::uint32_t>((rstcsr_ & 0xE0) | 0x1F);
    case 0x92: return ccr_ & ~0x30u;
    case 0xE0: return static_cast<std::uint32_t>(0x8000 | (icr_ & 0x0101));   // NMI input high
    case 0xE2: return ipra_ & 0xFFF0;
    case 0xE4: return vcrwdt_ & 0x7F7F;
    case 0x100: return dvsr_;
    case 0x104: return dvdntl_;
    case 0x108: return (divuOvfie_ ? 2u : 0u) | (divuOvf_ ? 1u : 0u);
    case 0x10C: return vcrdiv_ & 0xFFFF;
    case 0x110: case 0x118: return dvdnth_;
    case 0x114: case 0x11C: return dvdntl_;
    case 0x71: return drcr_[0] & 3;
    case 0x72: return drcr_[1] & 3;
    case 0x180: return sar_[0];
    case 0x184: return dar_[0];
    case 0x188: return tcr_[0] & 0xFFFFFF;
    case 0x18C: return chcr_[0] & 0xFFFF;
    case 0x190: return sar_[1];
    case 0x194: return dar_[1];
    case 0x198: return tcr_[1] & 0xFFFFFF;
    case 0x19C: return chcr_[1] & 0xFFFF;
    case 0x1B0: return dmaor_ & 0xF;
    case 0x1A0: return vcrdma_[0] & 0x7F;
    case 0x1A8: return vcrdma_[1] & 0x7F;
    case 0x1E0: return (bcr1_ & ~0xE008u) | (isSlave_ ? 0x8000u : 0u);
    case 0x1E4: return bcr2_;
    case 0x1E8: return wcr_;
    case 0x1EC: return mcr_ & ~0x103u;
    case 0x1F0: return rtcsr_ & 0xF8;
    case 0x1F4: return rtcnt_ & 0xFF;
    case 0x1F8: return rtcor_ & 0xFF;
    default: break;
    }
    // storage: assemble the bytes of the register
    std::uint32_t base;
    const int n = SizeOfRegisterAt(o, &base);
    std::uint32_t v = 0;
    for (int i = 0; i < n; ++i) v = (v << 8) | storage_[(o + i) & 0x1FF];
    return v;
}

void Sh7604OnChip::WriteRegister(std::uint32_t o, std::uint32_t value, std::uint32_t mask) {
    auto combine = [&](std::uint32_t old) { return (old & ~mask) | (value & mask); };
    switch (o) {
    case 0x10: tier_ = static_cast<std::uint8_t>(combine(tier_)); RecalcIrq(); return;
    case 0x11: {
        const std::uint8_t old = ftcsr_;
        const std::uint8_t data = static_cast<std::uint8_t>(combine(ftcsr_));
        constexpr std::uint8_t flags = kIcf | kOcfa | kOcfb | kOvf;
        ftcsr_ = static_cast<std::uint8_t>((data & ~flags) | (old & data & flags));   // a flag clears only by writing 0
        RecalcIrq();
        return;
    }
    case 0x12: frc_ = static_cast<std::uint16_t>(combine(frc_)); RecalcIrq(); return;
    case 0x14:
        if (tocr_ & 0x10) ocrb_ = static_cast<std::uint16_t>(combine(ocrb_)); else ocra_ = static_cast<std::uint16_t>(combine(ocra_));
        RecalcIrq();
        return;
    case 0x16: frcTcr_ = static_cast<std::uint8_t>(combine(frcTcr_) & 0x83); return;
    case 0x17: tocr_ = static_cast<std::uint8_t>(combine(tocr_) & 0x13); return;
    case 0x60: iprb_ = static_cast<std::uint16_t>(combine(iprb_)); RecalcIrq(); return;
    case 0x62: vcra_ = static_cast<std::uint16_t>(combine(vcra_)); RecalcIrq(); return;
    case 0x64: vcrb_ = static_cast<std::uint16_t>(combine(vcrb_)); RecalcIrq(); return;
    case 0x66: vcrc_ = static_cast<std::uint16_t>(combine(vcrc_)); RecalcIrq(); return;
    case 0x68: vcrd_ = static_cast<std::uint16_t>(combine(vcrd_)); RecalcIrq(); return;
    case 0x80: {                                                       // WTCNT (0x5Axx) and WTCSR (0xA5xx) share one address
        wtcwLatch_[0] = static_cast<std::uint16_t>(combine(wtcwLatch_[0]));
        const std::uint16_t w = wtcwLatch_[0];
        if ((w & 0xFF00) == 0x5A00) {
            wtcnt_ = static_cast<std::uint8_t>(w);
        } else if ((w & 0xFF00) == 0xA500) {
            wtcsr_ = static_cast<std::uint8_t>((wtcsr_ & w & 0x80) | (w & 0x7F));
            if (!(wtcsr_ & 0x20)) { wtcnt_ = 0; wdtRemainder_ = 0; }
            RecalcIrq();
        }
        return;
    }
    case 0x82: {
        wtcwLatch_[1] = static_cast<std::uint16_t>(combine(wtcwLatch_[1]));
        const std::uint16_t w = wtcwLatch_[1];
        if ((w & 0xFF00) == 0xA500) { if ((w & 0x80) == 0) rstcsr_ &= 0x7F; }
        else if ((w & 0xFF00) == 0x5A00) rstcsr_ = static_cast<std::uint8_t>((rstcsr_ & 0x80) | (w & 0x60));
        return;
    }
    case 0x92: ccr_ = static_cast<std::uint8_t>(combine(ccr_)); return;
    case 0xE0: icr_ = static_cast<std::uint16_t>(combine(icr_)); return;
    case 0xE2: ipra_ = static_cast<std::uint16_t>(combine(ipra_)); RecalcIrq(); return;
    case 0xE4: vcrwdt_ = static_cast<std::uint16_t>(combine(vcrwdt_)); RecalcIrq(); return;
    case 0x100: dvsr_ = combine(dvsr_); return;
    case 0x104: dvdntl_ = combine(dvdntl_); Divide32(); return;
    case 0x108:
        if (mask & 0xFF) { divuOvf_ = (value & 1) != 0; divuOvfie_ = (value & 2) != 0; RecalcIrq(); }
        return;
    case 0x10C: vcrdiv_ = combine(vcrdiv_); RecalcIrq(); return;
    case 0x110: dvdnth_ = combine(dvdnth_); return;
    case 0x114: dvdntl_ = combine(dvdntl_); Divide64(); return;
    case 0x71: drcr_[0] = static_cast<std::uint8_t>(combine(drcr_[0]) & 3); return;
    case 0x72: drcr_[1] = static_cast<std::uint8_t>(combine(drcr_[1]) & 3); return;
    case 0x180: sar_[0] = combine(sar_[0]); return;
    case 0x184: dar_[0] = combine(dar_[0]); return;
    case 0x188: tcr_[0] = combine(tcr_[0]) & 0xFFFFFF; return;
    case 0x190: sar_[1] = combine(sar_[1]); return;
    case 0x194: dar_[1] = combine(dar_[1]); return;
    case 0x198: tcr_[1] = combine(tcr_[1]) & 0xFFFFFF; return;
    case 0x18C: case 0x19C: {
        const int c = o == 0x18C ? 0 : 1;
        const std::uint32_t data = combine(chcr_[c]) & 0xFFFF;
        chcr_[c] = (data & ~2u) | (chcr_[c] & data & 2u);   // TE is cleared by writing 0, never set by a write
        RecalcIrq();
        return;
    }
    case 0x1B0: {
        const std::uint32_t data = combine(dmaor_) & 0xF;
        dmaor_ = (data & ~6u) | (dmaor_ & data & 6u);       // NMIF and AE clear by writing 0
        return;
    }
    case 0x1A0: vcrdma_[0] = combine(vcrdma_[0]); return;
    case 0x1A8: vcrdma_[1] = combine(vcrdma_[1]); return;
    case 0x1E0:
        if (mask == 0xFFFFFFFFu) { if ((value & 0xFFFF0000u) == 0xA55A0000u) bcr1_ = value & 0xFFFF; }
        else bcr1_ = combine(bcr1_);
        return;
    case 0x1E4:
        if (mask == 0xFFFFFFFFu) { if ((value & 0xFFFF0000u) == 0xA55A0000u) bcr2_ = value & 0xFFFF; }
        else bcr2_ = combine(bcr2_);
        return;
    case 0x1E8: wcr_ = combine(wcr_); return;
    case 0x1EC: mcr_ = combine(mcr_); return;
    case 0x1F0: rtcsr_ = combine(rtcsr_); return;
    case 0x1F4: rtcnt_ = combine(rtcnt_) & 0xFF; return;
    case 0x1F8: rtcor_ = combine(rtcor_) & 0xFF; return;
    default: break;
    }
    std::uint32_t base;
    const int n = SizeOfRegisterAt(o, &base);
    std::uint32_t v = ReadRegister(o);
    v = (v & ~mask) | (value & mask);
    for (int i = 0; i < n; ++i) storage_[(o + i) & 0x1FF] = static_cast<std::uint8_t>(v >> (8 * (n - 1 - i)));
}

std::uint32_t Sh7604OnChip::Read(std::uint32_t address, int bytes) {
    const std::uint32_t offset = address & 0x1FF;
    if (TouchesTimers(offset, bytes)) Flush();
    std::uint32_t result = 0;
    for (int i = 0; i < bytes; ++i) {
        std::uint32_t base;
        const int size = SizeOfRegisterAt(offset + i, &base);
        const std::uint32_t reg = ReadRegister(base);
        const int shift = (size - 1 - static_cast<int>(offset + i - base)) * 8;
        result = (result << 8) | ((reg >> shift) & 0xFF);
    }
    return result;
}

void Sh7604OnChip::Write(std::uint32_t address, std::uint32_t value, int bytes) {
    const std::uint32_t offset = address & 0x1FF;
    if (TouchesTimers(offset, bytes) || offset >= 0x170) Flush();
    int i = 0;
    while (i < bytes) {
        std::uint32_t base;
        const int size = SizeOfRegisterAt(offset + i, &base);
        // the bytes of this access that fall in this register
        std::uint32_t regValue = 0, regMask = 0;
        int taken = 0;
        for (int j = i; j < bytes && (offset + j) < base + static_cast<std::uint32_t>(size); ++j) {
            const int shift = (size - 1 - static_cast<int>(offset + j - base)) * 8;
            const std::uint32_t byte = (value >> ((bytes - 1 - j) * 8)) & 0xFF;
            regValue |= byte << shift;
            regMask |= 0xFFu << shift;
            ++taken;
        }
        WriteRegister(base, regValue, regMask);
        i += taken;
    }
    UpdateFast();
}

void Sh7604OnChip::RecalcIrq() {
    int level = 0;
    std::uint32_t vector = 0;
    if (tier_ & ftcsr_ & (kIcf | kOcfa | kOcfb | kOvf)) {
        const int l = (iprb_ >> 8) & 15;
        if (l > level) {
            const std::uint8_t pending = tier_ & ftcsr_;
            level = l;
            if (pending & kIcf) vector = (vcrc_ >> 8) & 0x7F;
            else if (pending & (kOcfa | kOcfb)) vector = vcrc_ & 0x7F;
            else vector = (vcrd_ >> 8) & 0x7F;
        }
    }
    for (int c = 0; c < 2; ++c) {
        if ((chcr_[c] & 6) == 6) {                       // the transfer has ended and the end interrupt is enabled
            const int l = (ipra_ >> 8) & 15;
            if (l > level) { level = l; vector = vcrdma_[c] & 0x7F; }
        }
    }
    if (wtcsr_ & 0x80) {
        const int l = (ipra_ >> 4) & 15;
        if (l > level) { level = l; vector = (vcrwdt_ >> 8) & 0x7F; }
    }
    if (divuOvf_ && divuOvfie_) {
        const int l = (ipra_ >> 12) & 15;
        if (l > level) { level = l; vector = vcrdiv_ & 0x7F; }
    }
    core_.SetInternalInterrupt(level, vector);
}

// The quotient an overflowing division leaves (SH7604 hardware manual 10.3.3): with the overflow interrupt disabled the maximum
// H'7FFFFFFF for a positive quotient and the minimum H'80000000 for a negative one; with it enabled the manual only says the
// result after 6 cycles is stored, and the maximum is kept.
std::uint32_t Sh7604OnChip::OverflowQuotient(bool negative) const {
    return (!divuOvfie_ && negative) ? 0x80000000u : 0x7FFFFFFFu;
}

void Sh7604OnChip::Divide32() {
    const std::int32_t a = static_cast<std::int32_t>(dvdntl_);
    const std::int32_t b = static_cast<std::int32_t>(dvsr_);
    if (b != 0 && !(a == INT32_MIN && b == -1)) {
        dvdntl_ = static_cast<std::uint32_t>(a / b);
        dvdnth_ = static_cast<std::uint32_t>(a % b);
    } else if (b != 0) {          // INT32_MIN / -1 overflows
        divuOvf_ = true;
        dvdntl_ = OverflowQuotient(false);
        dvdnth_ = 0;
        RecalcIrq();
    } else {
        divuOvf_ = true;
        dvdntl_ = OverflowQuotient(a < 0);   // division by zero: the quotient would have the sign of the dividend
        dvdnth_ = 0x7FFFFFFF;
        RecalcIrq();
    }
}

void Sh7604OnChip::Divide64() {
    const std::int64_t a = static_cast<std::int64_t>((static_cast<std::uint64_t>(dvdnth_) << 32) | dvdntl_);
    const std::int64_t b = static_cast<std::int32_t>(dvsr_);
    if (b != 0 && !(a == INT64_MIN && b == -1)) {
        const std::int64_t q = a / b;
        if (q != static_cast<std::int32_t>(q)) {
            divuOvf_ = true;
            dvdntl_ = OverflowQuotient(q < 0);
            dvdnth_ = 0x7FFFFFFF;
            RecalcIrq();
        } else {
            dvdntl_ = static_cast<std::uint32_t>(q);
            dvdnth_ = static_cast<std::uint32_t>(a % b);
        }
    } else {
        divuOvf_ = true;
        dvdntl_ = OverflowQuotient(b == 0 ? a < 0 : false);
        dvdnth_ = 0x7FFFFFFF;
        RecalcIrq();
    }
}

void Sh7604OnChip::AdvanceFrc(std::uint64_t cycles) {
    const int shift = kFrtShift[frcTcr_ & 3];
    if (shift < 0) return;   // external clock: not counted
    frcRemainder_ += cycles;
    std::uint64_t ticks = frcRemainder_ >> shift;
    frcRemainder_ &= (1ull << shift) - 1;
    bool changed = false;
    while (ticks > 0) {
        // jump to the next event: a compare match or the overflow, whichever comes first
        std::uint32_t step = 0x10000u - frc_;   // ticks until the counter wraps to 0
        const std::uint32_t toA = static_cast<std::uint16_t>(ocra_ - frc_);
        const std::uint32_t toB = static_cast<std::uint16_t>(ocrb_ - frc_);
        if (toA != 0 && toA < step) step = toA;
        if (toB != 0 && toB < step) step = toB;
        if (step > ticks) { frc_ = static_cast<std::uint16_t>(frc_ + ticks); break; }
        ticks -= step;
        frc_ = static_cast<std::uint16_t>(frc_ + step);
        if (frc_ == ocrb_) { ftcsr_ |= kOcfb; changed = true; }
        if (frc_ == 0) { ftcsr_ |= kOvf; changed = true; }
        if (frc_ == ocra_) {
            ftcsr_ |= kOcfa;
            changed = true;
            if (ftcsr_ & kCclra) frc_ = 0;
        }
    }
    if (changed) RecalcIrq();
}

void Sh7604OnChip::Flush() {
    if (pending_ == 0) return;
    const std::uint64_t cycles = pending_;
    pending_ = 0;
    AdvanceNow(cycles);
    UpdateFast();
}

void Sh7604OnChip::UpdateFast() {
    bool dmaMayRun = false;
    for (int c = 0; c < 2; ++c) {
        if ((chcr_[c] & 1) && !(chcr_[c] & 2) && (dmaor_ & 1) && !(dmaor_ & 6)) dmaMayRun = true;
    }
    eager_ = dmaMayRun || (wtcsr_ & 0x20) != 0;
    nextEvent_ = ~0ull;
    const int shift = kFrtShift[frcTcr_ & 3];
    if (shift >= 0) {
        std::uint32_t step = 0x10000u - frc_;
        const std::uint32_t toA = static_cast<std::uint16_t>(ocra_ - frc_);
        const std::uint32_t toB = static_cast<std::uint16_t>(ocrb_ - frc_);
        if (toA != 0 && toA < step) step = toA;
        if (toB != 0 && toB < step) step = toB;
        nextEvent_ = (static_cast<std::uint64_t>(step) << shift) - frcRemainder_;
    }
}

void Sh7604OnChip::AdvanceNow(std::uint64_t cpuCycles) {
    AdvanceFrc(cpuCycles);
    RunDma(cpuCycles);
    if (wtcsr_ & 0x20) {
        const int shift = kWdtShift[wtcsr_ & 7];
        wdtRemainder_ += cpuCycles;
        std::uint64_t ticks = wdtRemainder_ >> shift;
        wdtRemainder_ &= (1ull << shift) - 1;
        while (ticks > 0) {
            const std::uint64_t toOverflow = 0x100u - wtcnt_;
            if (ticks < toOverflow) { wtcnt_ = static_cast<std::uint8_t>(wtcnt_ + ticks); break; }
            ticks -= toOverflow;
            wtcnt_ = 0;
            if (!(wtcsr_ & 0x40)) { wtcsr_ |= 0x80; RecalcIrq(); }   // interval timer mode
            else rstcsr_ |= 0x80;                                       // watchdog mode: the overflow flag (no reset)
        }
    }
}

bool Sh7604OnChip::DmaChannelActive(int c) const {
    if (!(chcr_[c] & 1) || (chcr_[c] & 2)) return false;                        // not enabled, or the transfer has ended (TE)
    if (!(dmaor_ & 1) || (dmaor_ & 6)) return false;                            // the controller is off, or NMIF/AE is set
    if (chcr_[c] & 0x200) return true;                                          // auto request
    return dmaDreq_ && dmaDreq_(c);                                             // external request: while the partner has data
}

void Sh7604OnChip::DmaTransferUnit(int c) {
    const int size = (chcr_[c] >> 10) & 3;
    const int sm = (chcr_[c] >> 12) & 3, dm = (chcr_[c] >> 14) & 3;
    static const std::uint32_t kBytes[4] = {1, 2, 4, 4};
    const std::uint32_t unit = kBytes[size];
    if (size == 3) {
        for (std::uint32_t i = 0; i < 4; ++i) {
            const std::uint32_t value = dmaRead_ ? dmaRead_(sar_[c] + i * 4, 4) : 0;
            if (dmaWrite_) dmaWrite_(dar_[c] + i * 4, value, 4);
        }
        sar_[c] += 16;                                                           // the source moves on by 16 whatever SM says
        if (dm == 1) dar_[c] += 16; else if (dm == 2) dar_[c] -= 16;
    } else {
        const std::uint32_t src = sar_[c];
        const std::uint32_t dst = dar_[c];
        const std::uint32_t value = dmaRead_ ? dmaRead_(src, static_cast<int>(unit)) : 0;
        if (dmaWrite_) dmaWrite_(dst, value, static_cast<int>(unit));
        if (sm == 1) sar_[c] += unit; else if (sm == 2) sar_[c] -= unit;
        if (dm == 1) dar_[c] += unit; else if (dm == 2) dar_[c] -= unit;
    }
    const std::uint32_t count = size == 3 ? 4u : 1u;
    tcr_[c] = tcr_[c] > count ? tcr_[c] - count : 0;
    if (tcr_[c] == 0) {
        chcr_[c] |= 2;                                                           // TE: the transfer has ended
        RecalcIrq();
    }
}

void Sh7604OnChip::RunDma(std::uint64_t cycles) {
    bool any = false;
    for (int c = 0; c < 2; ++c) if (DmaChannelActive(c)) any = true;
    if (!any) { dmaBudget_ = 0; return; }
    dmaBudget_ += cycles;
    while (dmaBudget_ >= 2) {
        // the channel that goes first: channel 0 (fixed priority, DMAOR.PR = 0), or the one that did not go last (round robin;
        // the first after a reset is channel 1)
        int first = 0;
        if (dmaor_ & 8) first = dmaLastChannel_ == 1 ? 0 : 1;
        int chosen = -1;
        for (int k = 0; k < 2; ++k) {
            const int c = (first + k) & 1;
            if (DmaChannelActive(c)) { chosen = c; break; }
        }
        if (chosen < 0) { dmaBudget_ = 0; break; }
        if (tcr_[chosen] == 0) tcr_[chosen] = 0x1000000;       // a count of 0 means 0x1000000 units
        DmaTransferUnit(chosen);
        dmaLastChannel_ = chosen;
        dmaBudget_ -= 2;
    }
}
