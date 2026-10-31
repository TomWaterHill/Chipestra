// license:BSD-3-Clause
// copyright-holders:Fabio Priuli
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/bus/nes/konami.cpp  [copyright-holders:Fabio Priuli]
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#include "nes_vrc6.h"

Vrc6Prg::Vrc6Prg(std::size_t bankCount, bool swapAddressLines)
    : bankCount_(bankCount < 2 ? 2 : bankCount), swap_(swapAddressLines) {
    Reset();
}

void Vrc6Prg::Reset() {
    bank16_ = 0;
    bank8_ = 0;
}

void Vrc6Prg::SetState(std::uint8_t bank16, std::uint8_t bank8) {
    bank16_ = bank16 & 0x0F;
    bank8_ = bank8 & 0x1F;
}

bool Vrc6Prg::Write(std::uint16_t address, std::uint8_t value, unsigned* soundOffset) {
    // MAME's nes_konami_vrc6_device::write_h: the block is offset & 0x7000, the register comes from two address lines.
    const unsigned lo = address & 3;
    const unsigned reg = swap_ ? (((lo & 1) << 1) | (lo >> 1)) : lo;
    switch (address & 0x7000) {
    case 0x0000: bank16_ = value & 0x0F; break;
    case 0x4000: bank8_ = value & 0x1F; break;
    case 0x1000:  // pulse 1 and the global control
        *soundOffset = reg;
        return true;
    case 0x2000:  // pulse 2
        *soundOffset = 0x100 | reg;
        return true;
    case 0x3000:  // register 3 is the mirroring / PRG RAM control; the rest is the sawtooth
        if (reg == 3) return false;
        *soundOffset = 0x200 | reg;
        return true;
    default: break;
    }
    return false;
}

std::size_t Vrc6Prg::Offset(std::uint16_t address) const {
    std::size_t bank = 0;
    switch ((address >> 13) & 3) {
    case 0: bank = static_cast<std::size_t>(bank16_) * 2; break;      // $8000: first half of the 16 KB bank
    case 1: bank = static_cast<std::size_t>(bank16_) * 2 + 1; break;  // $A000: second half
    case 2: bank = bank8_; break;                                     // $C000
    default: bank = bankCount_ - 1; break;                            // $E000
    }
    return (bank % bankCount_) * 0x2000 + (address & 0x1FFF);
}
