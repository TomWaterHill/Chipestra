// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "devices/taito/darius/darius_adpcm_sound_map.h"

namespace {

// Unmapped accesses: reads return 0xFF (open bus); writes are dropped. The
// whole 64KB space is ROM, so nothing should normally reach these.
std::uint8_t OnUnmappedRead(std::uint64_t) { return 0xFF; }
void OnUnmappedWrite(std::uint64_t, std::uint8_t) {}

}  // namespace

DariusAdpcmSoundMap::DariusAdpcmSoundMap() : rom_(kRomSize, /*read_only=*/true), space_(OnUnmappedRead, OnUnmappedWrite) {
    space_.Map(0, kRomSize, &rom_);
}

void DariusAdpcmSoundMap::LoadRom(const std::uint8_t* data, std::size_t size) {
    const std::size_t loaded = size < kRomSize ? size : kRomSize;
    std::uint8_t* rom = rom_.RawData();
    for (std::size_t i = 0; i < loaded; ++i) {
        rom[i] = data[i];
    }
    for (std::size_t i = loaded; i < kRomSize; ++i) {
        rom[i] = 0;
    }
}

std::uint8_t DariusAdpcmSoundMap::Read8(std::uint16_t address) const { return space_.Read8(address); }

void DariusAdpcmSoundMap::Write8(std::uint16_t, std::uint8_t) {
    // The whole region is ROM; the chip ignores the write strobe.
}
