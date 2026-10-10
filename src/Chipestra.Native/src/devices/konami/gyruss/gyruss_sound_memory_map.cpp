// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "devices/konami/gyruss/gyruss_sound_memory_map.h"

#include <stdexcept>

namespace {
std::uint8_t OnUnmappedRead(std::uint64_t) { return 0xFF; }
void OnUnmappedWrite(std::uint64_t, std::uint8_t) {}
}  // namespace

GyrussSoundMemoryMap::GyrussSoundMemoryMap()
    : ram_(kRamSize, /*read_only=*/false), space_(OnUnmappedRead, OnUnmappedWrite) {
    space_.Map(kRomStart, kRomSize, &rom_endpoint_);
    space_.Map(kRamStart, kRamSize, &ram_);
    // kSoundLatchAddress is mapped later via MapDevice().
}

void GyrussSoundMemoryMap::LoadRom(const std::uint8_t* data, std::size_t size) {
    if (size > kRomSize) {
        throw std::invalid_argument("GyrussSoundMemoryMap::LoadRom: size exceeds the 24KB audiocpu ROM window");
    }
    std::uint8_t* rom = rom_endpoint_.RawRom();
    for (std::size_t i = 0; i < size; ++i) {
        rom[i] = data[i];
    }
    for (std::size_t i = size; i < rom_endpoint_.RawRomSize(); ++i) {
        rom[i] = 0;
    }
}

void GyrussSoundMemoryMap::MapDevice(std::uint64_t start, std::uint64_t size, AddressSpaceEndpoint* endpoint) {
    space_.Map(start, size, endpoint);
}

std::uint8_t GyrussSoundMemoryMap::Read8(std::uint16_t address) const { return space_.Read8(address); }
void GyrussSoundMemoryMap::Write8(std::uint16_t address, std::uint8_t value) { space_.Write8(address, value); }
