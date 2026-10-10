// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include <algorithm>
#include "address_space.h"
#include "memory_region.h"

#include <cstddef>
#include <cstdint>
#include <functional>

// The Namco System 1 audio CPU (MC6809E) physical 16-bit address space:
//   0x0000-0x3FFF  banked ROM window (8 x 0x4000 banks, selected via a write to 0xC000/0xC001)
//   0x4000-0x4001  YM2151
//   0x5000-0x53FF  CUS30
//   0x7000-0x77FF  TRIRAM (shared with the un-emulated main CPU; commands are written here directly)
//   0x8000-0x9FFF  RAM (8KB)
//   0xA000         no-op read (open-bus default)
//   0xC000-0xC001  bank-select WRITE (bank = (data & 0x70) >> 4)
//   0xD001         watchdog WRITE (not emulated)
//   0xE000         IRQ-ack WRITE (clears the VBLANK IRQ line)
//   0xC000-0xFFFF  fixed ROM window, READ only, always bank 0
//
// The write-side addresses fall inside the fixed ROM read span; any other write there is ignored.
// The CUS30 and TRIRAM address mirrors are not replicated: the command protocol only uses the
// canonical addresses, so anything outside the ranges above is open bus.
//
// Unmapped-read default is 0x00 (true open bus); this space models no input port.
class DragonSpiritMemoryMap {
public:
    static constexpr std::uint64_t kBankedRomStart = 0x0000;
    static constexpr std::uint64_t kBankedRomWindowSize = 0x4000;  // 0x0000-0x3FFF, one 16KB bank
    static constexpr std::size_t kBankCount = 8;
    static constexpr std::size_t kFullSoundRomSize = kBankedRomWindowSize * kBankCount;  // 0x20000

    static constexpr std::uint64_t kYm2151Start = 0x4000;
    static constexpr std::uint64_t kYm2151Size = 0x0002;

    static constexpr std::uint64_t kCus30Start = 0x5000;
    static constexpr std::uint64_t kCus30Size = 0x0400;

    static constexpr std::uint64_t kTriRamStart = 0x7000;
    static constexpr std::uint64_t kTriRamSize = 0x0800;

    static constexpr std::uint64_t kRamStart = 0x8000;
    static constexpr std::uint64_t kRamSize = 0x2000;  // 0x8000-0x9FFF

    static constexpr std::uint64_t kFixedRomStart = 0xC000;
    static constexpr std::uint64_t kFixedRomSize = 0x4000;  // 0xC000-0xFFFF, always bank 0

    static constexpr std::uint64_t kBankSelectAddress0 = 0xC000;
    static constexpr std::uint64_t kBankSelectAddress1 = 0xC001;
    static constexpr std::uint64_t kWatchdogAddress = 0xD001;
    static constexpr std::uint64_t kIrqAckAddress = 0xE000;

    DragonSpiritMemoryMap();

    // Copies `size` bytes into the full 8-bank sound ROM (zero-padding the rest); throws
    // std::invalid_argument if size > kFullSoundRomSize.
    void LoadSoundRom(const std::uint8_t* data, std::size_t size);

    // Maps a device endpoint (YM2151/CUS30) into this space; `endpoint` must outlive this object.
    void MapDevice(std::uint64_t start, std::uint64_t size, AddressSpaceEndpoint* endpoint);

    // Invoked on every write to kIrqAckAddress; the Machine wires it to clear its VBLANK-IRQ line.
    void SetIrqAckCallback(std::function<void()> callback) { irq_ack_callback_ = std::move(callback); }

    std::uint8_t Read8(std::uint32_t physicalAddress) const;
    void Write8(std::uint32_t physicalAddress, std::uint8_t value);

    // Test-only observation point.
    std::uint32_t CurrentBankForTest() const { return current_bank_; }

    // Zeroes the audio CPU's work RAM and the shared TRIRAM (power-on state), so a mid-session
    // Reset() sounds like a fresh Initialize().
    void ClearRam() {
        std::fill_n(ram_.RawData(), ram_.Size(), std::uint8_t{0});
        std::fill_n(triram_.RawData(), triram_.Size(), std::uint8_t{0});
    }

    MemoryRegion& TriRamForSharing() { return triram_; }

private:
    // sound_rom_/ram_ must outlive space_ (members are destroyed in reverse declaration order).
    MemoryRegion sound_rom_;  // full 8-bank 0x20000 image; bank window/fixed window both index into it
    MemoryRegion ram_;
    MemoryRegion triram_;
    AddressSpace space_;
    std::uint32_t current_bank_ = 0;
    std::function<void()> irq_ack_callback_;
};
