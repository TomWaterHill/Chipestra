// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "ish2_core.h"

#include <cstdint>
#include <functional>

// The on-chip peripherals of the Hitachi SH7604 (the SH-2 of the Super 32X): the free-running timer (FRT), the watchdog timer
// (WDT), the divide unit (DIVU), the interrupt controller (INTC) and the registers of the DMA controller, the bus state
// controller, the cache, the serial interface and the user break controller. The registers live at 0xFFFFFE00-0xFFFFFFFF
// (and the area 7 mirrors of it); the owner passes the offset-bearing address as the CPU used it.
//
// Register layout and behaviour follow MAME's sh7604 device (BSD-3-Clause: src/devices/cpu/sh/sh7604.cpp, sh7604_wdt.cpp,
// sh_dmac.cpp, copyright-holders Juergen Buchmueller, R. Belmont, Olivier Galibert, Angelo Salese), checked against the
// Hitachi SH7604 hardware manual where MAME leaves a register out. The DIVU interrupt on overflow is implemented (MAME
// leaves it out). Time is the CPU's own clock: the owner calls Advance() with the cycles the CPU consumed.
//
// The DMA controller (two channels) moves data between addresses of the owner's bus (SetDmaBus). A channel set for auto request
// (CHCR bit 9) runs as soon as it is enabled; a channel set for an external request runs while the owner's DREQ function says the
// partner has data (on the 32X: the FIFO the 68000 fills, or the PWM unit). A unit takes two CPU cycles. The registers follow
// MAME's sh7604 (a unit of 16 bytes counts four in TCR) and the Hitachi SH7604 manual (TE is cleared only by writing 0).
class Sh7604OnChip {
public:
    explicit Sh7604OnChip(ISh2Core& core, bool isSlave) : core_(core), isSlave_(isSlave) { Reset(); }

    void Reset();
    // True when `address` is in the on-chip register area (0xFFFFFE00-0xFFFFFFFF in any of the area 7 mirrors).
    static bool Contains(std::uint32_t address) { return (address & 0xE0000000u) == 0xE0000000u && (address & 0x1FFFFFFFu) >= 0x1FFFFE00u; }
    std::uint32_t Read(std::uint32_t address, int bytes);
    void Write(std::uint32_t address, std::uint32_t value, int bytes);
    // Lets the timers run for `cpuCycles` CPU clocks. While the timers only count (no DMA transfer, the watchdog off) the cycles are
    // gathered and applied when a register is accessed or when the next counter event is due, which gives the same state at the
    // same instruction as applying them at once.
    void Advance(std::uint64_t cpuCycles) {
        if (eager_) { AdvanceNow(cpuCycles); UpdateFast(); return; }
        pending_ += cpuCycles;
        if (pending_ >= nextEvent_) Flush();
    }
    // The bus the DMA controller uses and, for external requests, whether channel 0/1 may transfer a unit now.
    void SetDmaBus(std::function<std::uint32_t(std::uint32_t, int)> read, std::function<void(std::uint32_t, std::uint32_t, int)> write, std::function<bool(int)> dreq) {
        dmaRead_ = std::move(read); dmaWrite_ = std::move(write); dmaDreq_ = std::move(dreq);
    }

    // The state some owners and the tests look at.
    std::uint16_t Frc() { Flush(); return frc_; }
    std::uint32_t ExternalVectorMode() const { return icr_ & 1; }

private:
    struct Reg {
        std::uint32_t base;
        int bytes;
    };
    std::uint32_t ReadRegister(std::uint32_t base);
    void WriteRegister(std::uint32_t base, std::uint32_t value, std::uint32_t mask);
    static int SizeOfRegisterAt(std::uint32_t offset, std::uint32_t* base);
    void RecalcIrq();
    void AdvanceFrc(std::uint64_t cycles);
    void Divide32();
    void Divide64();
    std::uint32_t OverflowQuotient(bool negative) const;
    void RunDma(std::uint64_t cpuCycles);
    void AdvanceNow(std::uint64_t cpuCycles);
    void Flush();
    // True when an access of `bytes` at register offset `offset` reaches the free-running timer or the watchdog (whose state the gathered cycles move)
    static bool TouchesTimers(std::uint32_t offset, int bytes) { return offset < 0x90 && offset + static_cast<std::uint32_t>(bytes) > 0x10; }
    void UpdateFast();
    bool DmaChannelActive(int channel) const;
    void DmaTransferUnit(int channel);

    ISh2Core& core_;
    bool isSlave_;

    // storage for registers without behaviour (indexed by the low 9 bits of the address)
    std::uint8_t storage_[0x200] = {};

    // FRT
    std::uint8_t tier_ = 1, ftcsr_ = 0, frcTcr_ = 0, tocr_ = 0;
    std::uint16_t frc_ = 0, ocra_ = 0xFFFF, ocrb_ = 0xFFFF, icr_frt_ = 0;
    std::uint64_t frcRemainder_ = 0;
    // WDT
    std::uint8_t wtcsr_ = 0, wtcnt_ = 0, rstcsr_ = 0;
    std::uint64_t wdtRemainder_ = 0;
    // INTC
    std::uint16_t ipra_ = 0, iprb_ = 0, vcra_ = 0, vcrb_ = 0, vcrc_ = 0, vcrd_ = 0, vcrwdt_ = 0, icr_ = 0;
    std::uint32_t vcrdiv_ = 0, vcrdma_[2] = {0, 0};
    // DMAC
    std::uint32_t sar_[2] = {0, 0}, dar_[2] = {0, 0}, tcr_[2] = {0, 0}, chcr_[2] = {0, 0}, dmaor_ = 0;
    std::uint8_t drcr_[2] = {0, 0};
    std::uint64_t dmaBudget_ = 0;
    // gathered cycles (see Advance)
    std::uint64_t pending_ = 0, nextEvent_ = ~0ull;
    bool eager_ = false;
    int dmaLastChannel_ = 0;
    std::function<std::uint32_t(std::uint32_t, int)> dmaRead_;
    std::function<void(std::uint32_t, std::uint32_t, int)> dmaWrite_;
    std::function<bool(int)> dmaDreq_;
    // DIVU
    std::uint32_t dvsr_ = 0, dvdntl_ = 0, dvdnth_ = 0;
    bool divuOvf_ = false, divuOvfie_ = false;
    // BSC
    std::uint32_t bcr1_ = 0x03F0, bcr2_ = 0x00FC, wcr_ = 0xAAFF, mcr_ = 0, rtcsr_ = 0, rtcnt_ = 0, rtcor_ = 0;
    std::uint8_t ccr_ = 0, sbycr_ = 0;
    std::uint16_t wtcwLatch_[2] = {0, 0};
};
