// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "dragon_spirit_memory_map.h"

#include <stdexcept>

namespace {

// See the header on why unmapped reads return 0x00 rather than 0xFF.
std::uint8_t OnUnmappedRead(std::uint64_t) { return 0x00; }
void OnUnmappedWrite(std::uint64_t, std::uint8_t) {}

}  // namespace

DragonSpiritMemoryMap::DragonSpiritMemoryMap()
    : sound_rom_(kFullSoundRomSize, /*read_only=*/true),
      ram_(kRamSize, /*read_only=*/false),
      triram_(kTriRamSize, /*read_only=*/false),
      space_(OnUnmappedRead, OnUnmappedWrite) {
    // The banked and fixed ROM windows index sound_rom_ directly, because bank switching and the
    // read/write overlap at 0xC000-0xC001 can't be expressed by AddressSpace's static Map().
    space_.Map(kTriRamStart, kTriRamSize, &triram_);
    space_.Map(kRamStart, kRamSize, &ram_);
}

void DragonSpiritMemoryMap::LoadSoundRom(const std::uint8_t* data, std::size_t size) {
    if (size > kFullSoundRomSize) {
        throw std::invalid_argument("DragonSpiritMemoryMap::LoadSoundRom: size exceeds kFullSoundRomSize");
    }
    std::uint8_t* rom = sound_rom_.RawData();
    for (std::size_t i = 0; i < size; ++i) rom[i] = data[i];
    for (std::size_t i = size; i < kFullSoundRomSize; ++i) rom[i] = 0;
}

void DragonSpiritMemoryMap::MapDevice(std::uint64_t start, std::uint64_t size, AddressSpaceEndpoint* endpoint) {
    space_.Map(start, size, endpoint);
}

std::uint8_t DragonSpiritMemoryMap::Read8(std::uint32_t physicalAddress) const {
    if (physicalAddress < kBankedRomWindowSize) {
        return sound_rom_.RawData()[static_cast<std::size_t>(current_bank_) * kBankedRomWindowSize + physicalAddress];
    }
    if (physicalAddress >= kFixedRomStart && physicalAddress < kFixedRomStart + kFixedRomSize) {
        return sound_rom_.RawData()[physicalAddress - kFixedRomStart];  // always bank 0
    }
    return space_.Read8(physicalAddress);
}

void DragonSpiritMemoryMap::Write8(std::uint32_t physicalAddress, std::uint8_t value) {
    if (physicalAddress < kBankedRomWindowSize) {
        return;  // ROM ignores the write strobe
    }
    if (physicalAddress == kBankSelectAddress0 || physicalAddress == kBankSelectAddress1) {
        current_bank_ = (static_cast<std::uint32_t>(value) & 0x70) >> 4;
        return;
    }
    if (physicalAddress == kWatchdogAddress) {
        return;  // not emulated (sound-only scope)
    }
    if (physicalAddress == kIrqAckAddress) {
        if (irq_ack_callback_) irq_ack_callback_();
        return;
    }
    if (physicalAddress >= kFixedRomStart && physicalAddress < kFixedRomStart + kFixedRomSize) {
        return;  // rest of the fixed ROM window: write strobe ignored, same as the banked window
    }
    space_.Write8(physicalAddress, value);
}
