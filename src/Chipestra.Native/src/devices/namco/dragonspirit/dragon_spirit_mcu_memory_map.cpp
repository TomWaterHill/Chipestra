// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "dragon_spirit_mcu_memory_map.h"

#include <stdexcept>

namespace {

// Unmapped reads are 0x00 (open bus); Read8 returns 0xFF for the DSW/CONTROL ports before reaching space_.
std::uint8_t OnUnmappedRead(std::uint64_t) { return 0x00; }
void OnUnmappedWrite(std::uint64_t, std::uint8_t) {}

// Voice ROM bank decode: the chip-select (data & 0xfc) picks a 4-bank group (0/4/8/12/16/20) and
// data & 3 the sub-bank. For ROM 0 (0xf8) bit 1 is inverted first (A16 inverted on that chip).
std::uint32_t DecodeVoiceBank(std::uint8_t data) {
    std::uint32_t bank = 0;
    switch (data & 0xfc) {
        case 0xf8: bank = 0; data = static_cast<std::uint8_t>(data ^ 2); break;
        case 0xf4: bank = 4; break;
        case 0xec: bank = 8; break;
        case 0xdc: bank = 12; break;
        case 0xbc: bank = 16; break;
        case 0x7c: bank = 20; break;
        default: bank = 0; break;
    }
    return bank + (data & 3);
}

}  // namespace

DragonSpiritMcuMemoryMap::DragonSpiritMcuMemoryMap(MemoryRegion& sharedTriRam)
    : internal_rom_(kInternalRomSize, /*read_only=*/true),
      voice_rom_(kFullVoiceRomSize, /*read_only=*/true),
      nvram_(kNvramSize, /*read_only=*/false),
      triram_(sharedTriRam),
      space_(OnUnmappedRead, OnUnmappedWrite) {
    space_.Map(kTriRamStart, kTriRamSize, &triram_);
    space_.Map(kNvramStart, kNvramSize, &nvram_);
    // The voice-ROM and internal-ROM windows index their regions directly (bank switching and the 0xF000
    // read/write overlap can't be expressed by AddressSpace), so neither is Map()-ed.
}

void DragonSpiritMcuMemoryMap::LoadInternalRom(const std::uint8_t* data, std::size_t size) {
    if (size > kInternalRomSize) {
        throw std::invalid_argument("DragonSpiritMcuMemoryMap::LoadInternalRom: size exceeds kInternalRomSize");
    }
    std::uint8_t* rom = internal_rom_.RawData();
    for (std::size_t i = 0; i < size; ++i) rom[i] = data[i];
    for (std::size_t i = size; i < kInternalRomSize; ++i) rom[i] = 0;
}

void DragonSpiritMcuMemoryMap::LoadVoiceRom(const std::uint8_t* data, std::size_t size) {
    if (size > kFullVoiceRomSize) {
        throw std::invalid_argument("DragonSpiritMcuMemoryMap::LoadVoiceRom: size exceeds kFullVoiceRomSize");
    }
    std::uint8_t* rom = voice_rom_.RawData();
    for (std::size_t i = 0; i < size; ++i) rom[i] = data[i];
    for (std::size_t i = size; i < kFullVoiceRomSize; ++i) rom[i] = 0;
}

void DragonSpiritMcuMemoryMap::MapDevice(std::uint64_t start, std::uint64_t size, AddressSpaceEndpoint* endpoint) {
    space_.Map(start, size, endpoint);
}

std::uint8_t DragonSpiritMcuMemoryMap::Read8(std::uint32_t physicalAddress) const {
    if (physicalAddress >= kDswStart && physicalAddress < kDswStart + kDswSize) return 0xFF;
    if (physicalAddress == kControl0Address || physicalAddress == kControl1Address) return 0xFF;
    if (physicalAddress >= kVoiceRomStart && physicalAddress < kVoiceRomStart + kVoiceRomWindowSize) {
        const std::size_t offsetInRom =
            static_cast<std::size_t>(current_bank_) * kVoiceRomWindowSize + (physicalAddress - kVoiceRomStart);
        const std::uint8_t value = voice_rom_.RawData()[offsetInRom];
        if (voice_read_observer_for_test_) voice_read_observer_for_test_(offsetInRom, value);
        return value;
    }
    if (physicalAddress >= kInternalRomStart && physicalAddress < kInternalRomStart + kInternalRomSize) {
        return internal_rom_.RawData()[physicalAddress - kInternalRomStart];
    }
    if (physicalAddress == kTriRamStart) {
        const std::uint8_t value = space_.Read8(physicalAddress);
        if (triram_gate_read_observer_for_test_) triram_gate_read_observer_for_test_(value);
        return value;
    }
    return space_.Read8(physicalAddress);
}

void DragonSpiritMcuMemoryMap::Write8(std::uint32_t physicalAddress, std::uint8_t value) {
    if (physicalAddress >= kVoiceRomStart && physicalAddress < kVoiceRomStart + kVoiceRomWindowSize) {
        return;  // ROM ignores the write strobe
    }
    if (physicalAddress == kBankSelectAddress) {
        current_bank_ = DecodeVoiceBank(value);
        return;
    }
    if (physicalAddress == kIrqAckAddress) {
        if (irq_ack_callback_) irq_ack_callback_();
        return;  // 0xF000 lies inside the internal-ROM read range, so it is handled before that check
    }
    if (physicalAddress >= kInternalRomStart && physicalAddress < kInternalRomStart + kInternalRomSize) {
        return;  // rest of the internal ROM window: write strobe ignored
    }
    if (physicalAddress == kTriRamStart) {
        if (triram_gate_write_observer_for_test_) triram_gate_write_observer_for_test_(value);
        if (value != kGateReadyByte) {
            return;  // see kGateReadyByte in the header
        }
    }
    space_.Write8(physicalAddress, value);
}
