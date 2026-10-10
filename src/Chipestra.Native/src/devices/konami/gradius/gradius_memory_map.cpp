// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "gradius_memory_map.h"

namespace {

// Unmapped reads return 0xFF (open bus) and writes are dropped; the unmapped gaps are
// also unmapped on the real board.
std::uint8_t OnUnmappedRead(std::uint64_t) {
    return 0xFF;
}

void OnUnmappedWrite(std::uint64_t, std::uint8_t) {}

}  // namespace

GradiusMemoryMap::GradiusMemoryMap()
    : rom_(kRomSize, /*read_only=*/true),
      shared_ram_(kSharedRamSize, /*read_only=*/false),
      voice_ram_(kVoiceRamSize, /*read_only=*/false),
      space_(OnUnmappedRead, OnUnmappedWrite) {
    space_.Map(kRomStart, kRomSize, &rom_);
    space_.Map(kSharedRamStart, kSharedRamSize, &shared_ram_);
    space_.Map(kVoiceRamStart, kVoiceRamSize, &voice_ram_);
    // The K005289 windows are mapped later via MapDevice(); GradiusMachine owns the
    // adapter they dispatch to.
}

void GradiusMemoryMap::LoadRom(const std::uint8_t* data, std::size_t size) {
    const std::size_t loaded = size < kRomSize ? size : kRomSize;
    std::uint8_t* rom = rom_.RawData();
    for (std::size_t i = 0; i < loaded; ++i) {
        rom[i] = data[i];
    }
    for (std::size_t i = loaded; i < kRomSize; ++i) {
        rom[i] = 0;
    }
}

void GradiusMemoryMap::MapDevice(std::uint64_t start, std::uint64_t size, AddressSpaceEndpoint* endpoint) {
    space_.Map(start, size, endpoint);
}

std::uint8_t GradiusMemoryMap::Read8(std::uint16_t address) const {
    return space_.Read8(address);
}

void GradiusMemoryMap::Write8(std::uint16_t address, std::uint8_t value) {
    // The ROM chip ignores write strobes; writes to the ROM window are dropped silently.
    if (address < kRomStart + kRomSize) {
        return;
    }
    space_.Write8(address, value);
}
