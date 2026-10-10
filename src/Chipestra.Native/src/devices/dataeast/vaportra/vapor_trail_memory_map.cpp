// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "vapor_trail_memory_map.h"

#include <stdexcept>

namespace {

// Unmapped reads return 0x00 (open bus); see the header.
std::uint8_t OnUnmappedRead(std::uint64_t) {
    return 0x00;
}

void OnUnmappedWrite(std::uint64_t, std::uint8_t) {}

}  // namespace

VaporTrailMemoryMap::VaporTrailMemoryMap()
    : rom_(kRomSize, /*read_only=*/true), ram_(kRamSize, /*read_only=*/false), space_(OnUnmappedRead, OnUnmappedWrite) {
    space_.Map(kRomStart, kRomSize, &rom_);
    space_.Map(kRamStart, kRamSize, &ram_);
}

void VaporTrailMemoryMap::LoadRom(const std::uint8_t* data, std::size_t size) {
    if (size > kRomSize) {
        throw std::invalid_argument("VaporTrailMemoryMap::LoadRom: size exceeds kRomSize");
    }
    std::uint8_t* rom = rom_.RawData();
    for (std::size_t i = 0; i < size; ++i) {
        rom[i] = data[i];
    }
    for (std::size_t i = size; i < kRomSize; ++i) {
        rom[i] = 0;
    }
}

void VaporTrailMemoryMap::MapDevice(std::uint64_t start, std::uint64_t size, AddressSpaceEndpoint* endpoint) {
    space_.Map(start, size, endpoint);
}

std::uint8_t VaporTrailMemoryMap::Read8(std::uint32_t physicalAddress) const {
    return space_.Read8(physicalAddress);
}

void VaporTrailMemoryMap::Write8(std::uint32_t physicalAddress, std::uint8_t value) {
    // Writes into the ROM window have no effect (the ROM ignores the write strobe).
    if (physicalAddress < kRomStart + kRomSize) {
        return;
    }
    space_.Write8(physicalAddress, value);
}
