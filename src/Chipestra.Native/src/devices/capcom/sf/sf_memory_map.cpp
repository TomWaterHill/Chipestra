// license:BSD-3-Clause
// copyright-holders:Olivier Galibert
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Portions derived from MAME (src/mame/capcom/sf.cpp); see THIRD-PARTY-NOTICES.md.
#include "devices/capcom/sf/sf_memory_map.h"

#include <algorithm>

SfSoundMemoryMap::SfSoundMemoryMap() : sample_image_(kSampleImageSize, 0) {}

void SfSoundMemoryMap::LoadMusicRom(const std::uint8_t* data, std::size_t size) {
    music_rom_.fill(0);
    if (data != nullptr) {
        std::copy(data, data + std::min(size, kMusicRomSize), music_rom_.begin());
    }
}

void SfSoundMemoryMap::LoadSampleImage(const std::uint8_t* data, std::size_t size) {
    std::fill(sample_image_.begin(), sample_image_.end(), std::uint8_t{0});
    if (data != nullptr) {
        std::copy(data, data + std::min(size, kSampleImageSize), sample_image_.begin());
    }
}

void SfSoundMemoryMap::ResetState() {
    music_ram_.fill(0);
    sample_bank_ = 0;
}

std::uint8_t SfSoundMemoryMap::ReadMusic(std::uint16_t address) const {
    if (address < kMusicRomSize) {
        return music_rom_[address];
    }
    if (address >= kMusicRamStart && address < kMusicRamStart + kMusicRamSize) {
        return music_ram_[address - kMusicRamStart];
    }
    if (address == kLatchAddress) {
        return latch_read_ ? latch_read_() : std::uint8_t{0};
    }
    if (address == kYm2151Address || address == kYm2151Address + 1) {
        return ym_read_ ? ym_read_() : std::uint8_t{0};
    }
    return 0;
}

void SfSoundMemoryMap::WriteMusic(std::uint16_t address, std::uint8_t value) {
    if (address >= kMusicRamStart && address < kMusicRamStart + kMusicRamSize) {
        music_ram_[address - kMusicRamStart] = value;
    } else if ((address == kYm2151Address || address == kYm2151Address + 1) && ym_write_) {
        ym_write_(static_cast<std::uint8_t>(address - kYm2151Address), value);
    }
}

std::uint8_t SfSoundMemoryMap::ReadSamples(std::uint16_t address) const {
    if (address < kSampleRomWindowSize) {
        return sample_image_[address];
    }
    const std::size_t offset = kSampleBankSize + static_cast<std::size_t>(sample_bank_) * kSampleBankSize +
                               (address - kSampleRomWindowSize);
    return offset < sample_image_.size() ? sample_image_[offset] : kPastImageByte;
}
