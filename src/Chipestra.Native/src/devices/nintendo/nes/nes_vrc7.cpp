// license:BSD-3-Clause
// copyright-holders:Fabio Priuli
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/bus/nes/konami.cpp  [copyright-holders:Fabio Priuli]
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#include "nes_vrc7.h"

Vrc7Prg::Vrc7Prg(std::size_t bankCount, unsigned registerLine)
    : bank_count_(bankCount < 4 ? 4 : bankCount), line_(registerLine == 3 ? 3 : 4) {
    Reset();
}

void Vrc7Prg::Reset() {
    banks_[0] = static_cast<std::uint8_t>((bank_count_ - 4) & 0x3F);
    banks_[1] = static_cast<std::uint8_t>((bank_count_ - 3) & 0x3F);
    banks_[2] = static_cast<std::uint8_t>((bank_count_ - 2) & 0x3F);
    mirroring_and_wram_ = 0;
    mismatched_writes_ = 0;
}

void Vrc7Prg::SetState(std::uint8_t bank0, std::uint8_t bank1, std::uint8_t bank2) {
    banks_[0] = bank0 & 0x3F;
    banks_[1] = bank1 & 0x3F;
    banks_[2] = bank2 & 0x3F;
}

Vrc7Prg::Sound Vrc7Prg::Write(std::uint16_t address, std::uint8_t value) {
    // MAME's nes_konami_vrc7_device::write_h: the register is address bits 14-12 and the cartridge's register line.
    const unsigned otherLine = line_ == 4 ? 3 : 4;
    if ((address >> otherLine) & 1) ++mismatched_writes_;
    const unsigned reg = (((address >> 12) & 7) << 1) | ((address >> line_) & 1);
    switch (reg) {
    case 0x0:
    case 0x1:
    case 0x2:
        banks_[reg] = value & 0x3F;
        return Sound::None;
    case 0x3:
        return (address & 0x20) ? Sound::Data : Sound::Address;
    case 0xC:
        mirroring_and_wram_ = value;
        return Sound::None;
    default:  // CHR banks and the IRQ registers
        return Sound::None;
    }
}

std::size_t Vrc7Prg::Offset(std::uint16_t address) const {
    std::size_t bank = 0;
    switch ((address >> 13) & 3) {
    case 0: bank = banks_[0]; break;
    case 1: bank = banks_[1]; break;
    case 2: bank = banks_[2]; break;
    default: bank = bank_count_ - 1; break;
    }
    return (bank % bank_count_) * 0x2000 + (address & 0x1FFF);
}
