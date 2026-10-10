// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "devices/taito/darius/darius_sound_memory_map.h"

#include <stdexcept>

namespace {

// Unmapped accesses: reads return 0xFF (open bus); writes are dropped.
std::uint8_t OnUnmappedRead(std::uint64_t) { return 0xFF; }
void OnUnmappedWrite(std::uint64_t, std::uint8_t) {}

}  // namespace

std::uint8_t DariusSoundMemoryMap::RomEndpoint::Read8(std::uint64_t offset) const {
    if (offset < DariusSoundMemoryMap::kBankSize) {
        return rom_[offset];
    }
    const std::uint64_t bankOffset = offset - DariusSoundMemoryMap::kBankSize;
    return rom_[static_cast<std::size_t>(bank_) * DariusSoundMemoryMap::kBankSize + bankOffset];
}

void DariusSoundMemoryMap::RomEndpoint::Write8(std::uint64_t, std::uint8_t) {
    // The ROM chip ignores the write strobe.
}

DariusSoundMemoryMap::DariusSoundMemoryMap() : ram_(kRamSize, /*read_only=*/false), space_(OnUnmappedRead, OnUnmappedWrite) {
    space_.Map(kRomWindowStart, kRomWindowSize, &rom_endpoint_);
    space_.Map(kRamStart, kRamSize, &ram_);
    // kIoStart is mapped later via MapDevice() (DariusMachine owns the I/O glue).
}

void DariusSoundMemoryMap::LoadRom(const std::uint8_t* data, std::size_t size) {
    if (size > kFullRomSize) {
        throw std::invalid_argument("DariusSoundMemoryMap::LoadRom: size exceeds the 64KB audiocpu ROM");
    }
    std::uint8_t* rom = rom_endpoint_.RawRom();
    for (std::size_t i = 0; i < size; ++i) {
        rom[i] = data[i];
    }
    for (std::size_t i = size; i < kFullRomSize; ++i) {
        rom[i] = 0;
    }
}

void DariusSoundMemoryMap::SetBank(std::uint8_t bank) { rom_endpoint_.SetBank(bank); }

void DariusSoundMemoryMap::MapDevice(std::uint64_t start, std::uint64_t size, AddressSpaceEndpoint* endpoint) {
    space_.Map(start, size, endpoint);
}

std::uint8_t DariusSoundMemoryMap::Read8(std::uint16_t address) const { return space_.Read8(address); }

void DariusSoundMemoryMap::Write8(std::uint16_t address, std::uint8_t value) { space_.Write8(address, value); }
