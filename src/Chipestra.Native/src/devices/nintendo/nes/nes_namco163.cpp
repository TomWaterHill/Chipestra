// license:BSD-3-Clause
// copyright-holders:Fabio Priuli
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/bus/nes/namcot.cpp  [copyright-holders:Fabio Priuli]
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#include "nes_namco163.h"

Namco163Prg::Namco163Prg(std::size_t bankCount) : bank_count_(bankCount < 4 ? 4 : bankCount) { Reset(); }

void Namco163Prg::Reset() {
    banks_[0] = 0;
    banks_[1] = 1;
    banks_[2] = static_cast<std::uint8_t>((bank_count_ - 2) & 0x3F);
    last_sound_address_ = 0;
}

void Namco163Prg::SetState(std::uint8_t bank0, std::uint8_t bank1, std::uint8_t bank2) {
    banks_[0] = bank0 & 0x3F;
    banks_[1] = bank1 & 0x3F;
    banks_[2] = bank2 & 0x3F;
}

Namco163Prg::Sound Namco163Prg::Write(std::uint16_t address, std::uint8_t value, std::uint8_t& soundAddress) {
    // MAME's nes_namcot163_device::write_h: $E000 (bit 6 = sound off), $E800, $F000 select the banks; $F800 is the
    // sound address.
    switch (address & 0xF800) {
    case 0xE000:
        banks_[0] = value & 0x3F;
        return (value & 0x40) ? Sound::Disable : Sound::Enable;
    case 0xE800:
        banks_[1] = value & 0x3F;
        return Sound::None;
    case 0xF000:
        banks_[2] = value & 0x3F;
        return Sound::None;
    case 0xF800:
        last_sound_address_ = value;
        soundAddress = value;
        return Sound::Address;
    default:  // CHR banks
        return Sound::None;
    }
}

std::size_t Namco163Prg::Offset(std::uint16_t address) const {
    std::size_t bank = 0;
    switch ((address >> 13) & 3) {
    case 0: bank = banks_[0]; break;
    case 1: bank = banks_[1]; break;
    case 2: bank = banks_[2]; break;
    default: bank = bank_count_ - 1; break;
    }
    return (bank % bank_count_) * 0x2000 + (address & 0x1FFF);
}
