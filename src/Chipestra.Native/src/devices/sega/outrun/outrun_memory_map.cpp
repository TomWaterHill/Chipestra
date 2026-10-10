// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "outrun_memory_map.h"

#include <stdexcept>

namespace {

// Unmapped reads return 0xFF and writes are dropped; only a small gap that the
// real board also leaves floating is unmapped.
std::uint8_t OnUnmappedRead(std::uint64_t) {
    return 0xFF;
}

void OnUnmappedWrite(std::uint64_t, std::uint8_t) {}

}  // namespace

OutRunMemoryMap::OutRunMemoryMap()
    : rom_(kRomSize, /*read_only=*/true), ram_(kRamSize, /*read_only=*/false), space_(OnUnmappedRead, OnUnmappedWrite) {
    space_.Map(kRomStart, kRomSize, &rom_);
    space_.Map(kRamStart, kRamSize, &ram_);
}

void OutRunMemoryMap::LoadRom(const std::uint8_t* data, std::size_t size) {
    // The sound-program EPROM (0x10000 on Out Run) is larger than the CPU-mapped
    // window; bytes past kRomSize are not addressable by the Z80 and are dropped.
    const std::size_t loaded = size < kRomSize ? size : kRomSize;
    std::uint8_t* rom = rom_.RawData();
    for (std::size_t i = 0; i < loaded; ++i) {
        rom[i] = data[i];
    }
    for (std::size_t i = loaded; i < kRomSize; ++i) {
        rom[i] = 0;
    }
}

void OutRunMemoryMap::MapDevice(std::uint64_t start, std::uint64_t size, AddressSpaceEndpoint* endpoint) {
    space_.Map(start, size, endpoint);
}

std::uint8_t OutRunMemoryMap::Read8(std::uint16_t address) const {
    return space_.Read8(address);
}

void OutRunMemoryMap::Write8(std::uint16_t address, std::uint8_t value) {
    // Writes into the ROM window have no effect on the hardware (the ROM ignores
    // the write strobe), and a driver may poke there, so this is not an error.
    if (address < kRomStart + kRomSize) {
        return;
    }
    space_.Write8(address, value);
}
