// license:BSD-3-Clause
// copyright-holders:Fabio Priuli
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/bus/nes/mmc3.cpp  [copyright-holders:Fabio Priuli]
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#include "nes_mmc3.h"

Mmc3Prg::Mmc3Prg(std::size_t bankCount) : bankCount_(bankCount == 0 ? 1 : bankCount) { Reset(); }

void Mmc3Prg::Reset() {
    latch_ = 0;
    r6_ = 0;
    r7_ = 1;
}

void Mmc3Prg::SetState(std::uint8_t r6, std::uint8_t r7, bool prgMode1) {
    r6_ = r6;
    r7_ = r7;
    latch_ = prgMode1 ? 0x40 : 0x00;
}

void Mmc3Prg::Write(std::uint16_t address, std::uint8_t value) {
    // MAME's txrom_write: offset & 0x6001 selects the register.
    switch (address & 0xE001) {
    case 0x8000: latch_ = value; break;
    case 0x8001:
        if ((latch_ & 7) == 6) r6_ = value;
        else if ((latch_ & 7) == 7) r7_ = value;
        break;
    default: break;
    }
}

std::size_t Mmc3Prg::Offset(std::uint16_t address) const {
    const bool mode1 = (latch_ & 0x40) != 0;
    std::size_t bank = 0;
    switch ((address >> 13) & 3) {
    case 0: bank = mode1 ? bankCount_ - 2 : r6_; break;
    case 1: bank = r7_; break;
    case 2: bank = mode1 ? r6_ : bankCount_ - 2; break;
    default: bank = bankCount_ - 1; break;
    }
    return (bank % bankCount_) * 0x2000 + (address & 0x1FFF);
}
