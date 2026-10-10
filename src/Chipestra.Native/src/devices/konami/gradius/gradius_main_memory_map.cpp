// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "gradius_main_memory_map.h"

#include <utility>

namespace {

// Unmapped reads return 0xFF and writes are dropped. Everything the real map does not
// cover falls here (including the I/O window until MainIoBus is mapped in).
std::uint8_t OnUnmappedRead(std::uint64_t) {
    return 0xFF;
}

void OnUnmappedWrite(std::uint64_t, std::uint8_t) {}

}  // namespace

GradiusMainMemoryMap::GradiusMainMemoryMap(ReadSharedRamByteFn readSharedRam, WriteSharedRamByteFn writeSharedRam)
    : rom_(kRomSize, /*read_only=*/true),
      program_ram_(kProgramRamSize, /*read_only=*/false),
      shared_ram_bridge_(std::move(readSharedRam), std::move(writeSharedRam)),
      char_ram_(kCharRamSize, /*read_only=*/false),
      main_data_(kMainDataSize, /*read_only=*/true),
      work_ram_(kWorkRamSize, /*read_only=*/false),
      space_(OnUnmappedRead, OnUnmappedWrite) {
    space_.Map(kRomStart, kRomSize, &rom_);
    space_.Map(kProgramRamStart, kProgramRamSize, &program_ram_);
    space_.Map(kSharedRamBridgeStart, kSharedRamBridgeSize, &shared_ram_bridge_);
    space_.Map(kCharRamStart, kCharRamSize, &char_ram_);
    space_.Map(kMainDataStart, kMainDataSize, &main_data_);
    space_.Map(kWorkRamStart, kWorkRamSize, &work_ram_);
    // MainIoBus is mapped later via MapDevice(); GradiusMachine owns it.
}

void GradiusMainMemoryMap::LoadMainProgram(const std::uint8_t* data, std::size_t size) {
    const std::size_t loaded = size < kRomSize ? size : kRomSize;
    std::uint8_t* rom = rom_.RawData();
    for (std::size_t i = 0; i < loaded; ++i) rom[i] = data[i];
    for (std::size_t i = loaded; i < kRomSize; ++i) rom[i] = 0;
}

void GradiusMainMemoryMap::LoadMainData(const std::uint8_t* data, std::size_t size) {
    const std::size_t loaded = size < kMainDataSize ? size : kMainDataSize;
    std::uint8_t* rom = main_data_.RawData();
    for (std::size_t i = 0; i < loaded; ++i) rom[i] = data[i];
    for (std::size_t i = loaded; i < kMainDataSize; ++i) rom[i] = 0;
}

void GradiusMainMemoryMap::MapDevice(std::uint64_t start, std::uint64_t size, AddressSpaceEndpoint* endpoint) {
    space_.Map(start, size, endpoint);
}

std::uint8_t GradiusMainMemoryMap::Read8(std::uint32_t address) const {
    return space_.Read8(address);
}

void GradiusMainMemoryMap::Write8(std::uint32_t address, std::uint8_t value) {
    // The ROM chips ignore write strobes; writes to either ROM window are dropped
    // silently instead of being an error.
    if (address < kRomStart + kRomSize) return;
    if (address >= kMainDataStart && address < kMainDataStart + kMainDataSize) return;
    space_.Write8(address, value);
}
