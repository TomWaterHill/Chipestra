// license:BSD-3-Clause
// copyright-holders:Fabio Priuli
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/bus/nes/sunsoft.cpp  [copyright-holders:Fabio Priuli]
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#include "nes_fme7.h"

Fme7Prg::Fme7Prg(std::size_t bankCount) : bank_count_(bankCount < 4 ? 4 : bankCount) { Reset(); }

void Fme7Prg::Reset() {
    banks_[0] = 0;
    banks_[1] = 1;
    banks_[2] = static_cast<std::uint8_t>(bank_count_ - 2);
    command_ = 0;
}

void Fme7Prg::SetState(std::uint8_t bank9, std::uint8_t bankA, std::uint8_t bankB) {
    banks_[0] = bank9;
    banks_[1] = bankA;
    banks_[2] = bankB;
}

Fme7Prg::Sound Fme7Prg::Write(std::uint16_t address, std::uint8_t value) {
    // MAME's nes_sunsoft_fme7_device::fme7_write and nes_sunsoft_5_device::write_h, selected by A14-A13.
    switch (address & 0x6000) {
    case 0x0000:
        command_ = value & 0x0F;
        return Sound::None;
    case 0x2000:
        if (command_ >= 9 && command_ <= 0x0B) banks_[command_ - 9] = value;
        return Sound::None;
    case 0x4000:
        return Sound::Address;
    default:
        return Sound::Data;
    }
}

std::size_t Fme7Prg::Offset(std::uint16_t address) const {
    std::size_t bank = 0;
    switch ((address >> 13) & 3) {
    case 0: bank = banks_[0]; break;
    case 1: bank = banks_[1]; break;
    case 2: bank = banks_[2]; break;
    default: bank = bank_count_ - 1; break;
    }
    return (bank % bank_count_) * 0x2000 + (address & 0x1FFF);
}
