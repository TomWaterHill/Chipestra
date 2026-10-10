// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "devices/taito/ninjaw/ninja_warriors_sound_memory_map.h"

#include <stdexcept>

namespace {

// Unmapped accesses: reads return 0xFF (open bus); writes are dropped.
std::uint8_t OnUnmappedRead(std::uint64_t) { return 0xFF; }
void OnUnmappedWrite(std::uint64_t, std::uint8_t) {}

}  // namespace

std::uint8_t NinjaWarriorsSoundMemoryMap::RomEndpoint::Read8(std::uint64_t offset) const {
    if (offset < NinjaWarriorsSoundMemoryMap::kBankSize) {
        return rom_[offset];
    }
    const std::uint64_t bankOffset = offset - NinjaWarriorsSoundMemoryMap::kBankSize;
    return rom_[static_cast<std::size_t>(bank_) * NinjaWarriorsSoundMemoryMap::kBankSize + bankOffset];
}

void NinjaWarriorsSoundMemoryMap::RomEndpoint::Write8(std::uint64_t, std::uint8_t) {
    // The ROM chip ignores the write strobe.
}

NinjaWarriorsSoundMemoryMap::NinjaWarriorsSoundMemoryMap()
    : ram_(kRamSize, /*read_only=*/false), space_(OnUnmappedRead, OnUnmappedWrite) {
    space_.Map(kRomWindowStart, kRomWindowSize, &rom_endpoint_);
    space_.Map(kRamStart, kRamSize, &ram_);
    // kIoStart is mapped later via MapDevice() (NinjaWarriorsMachine owns the I/O glue).
}

void NinjaWarriorsSoundMemoryMap::LoadRom(const std::uint8_t* data, std::size_t size) {
    if (size > kFullRomSize) {
        throw std::invalid_argument("NinjaWarriorsSoundMemoryMap::LoadRom: size exceeds the 128KB audiocpu ROM");
    }
    std::uint8_t* rom = rom_endpoint_.RawRom();
    for (std::size_t i = 0; i < size; ++i) {
        rom[i] = data[i];
    }
    for (std::size_t i = size; i < kFullRomSize; ++i) {
        rom[i] = 0;
    }
}

void NinjaWarriorsSoundMemoryMap::SetBank(std::uint8_t bank) { rom_endpoint_.SetBank(bank); }

void NinjaWarriorsSoundMemoryMap::MapDevice(std::uint64_t start, std::uint64_t size, AddressSpaceEndpoint* endpoint) {
    space_.Map(start, size, endpoint);
}

std::uint8_t NinjaWarriorsSoundMemoryMap::Read8(std::uint16_t address) const { return space_.Read8(address); }

void NinjaWarriorsSoundMemoryMap::Write8(std::uint16_t address, std::uint8_t value) { space_.Write8(address, value); }
