// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "address_space.h"
#include "memory_region.h"

#include <array>
#include <cstddef>
#include <cstdint>

// The audiocpu (sound Z80) memory map:
//   0x0000-0x3FFF  ROM, fixed (first 16KB of the 128KB "audiocpu" ROM)
//   0x4000-0x7FFF  ROM, banked (16KB window onto one of the eight 16KB eighths
//                  of the same 128KB ROM; single buffer, no duplicate storage)
//   0xC000-0xDFFF  RAM (8KB)
//   0xE000-0xF3FF  scattered I/O (YM2610 registers, TC0140SYT slave port/comm,
//                  pancontrol_w, bankswitch, a few no-op stubs); not owned by
//                  this class - NinjaWarriorsMachine maps its own glue endpoint
//                  via MapDevice()
class NinjaWarriorsSoundMemoryMap {
public:
    static constexpr std::uint64_t kFullRomSize = 0x20000;  // the real, complete 128KB "audiocpu" ROM
    static constexpr std::uint64_t kBankSize = 0x4000;      // 16KB per bank, 8 banks total

    static constexpr std::uint64_t kRomWindowStart = 0x0000;
    static constexpr std::uint64_t kRomWindowSize = 0x8000;  // 0x0000-0x7FFF (fixed 16KB + banked 16KB)

    static constexpr std::uint64_t kRamStart = 0xC000;
    static constexpr std::uint64_t kRamSize = 0x2000;  // 0xC000-0xDFFF

    // The scattered I/O window NinjaWarriorsMachine maps its endpoint into.
    static constexpr std::uint64_t kIoStart = 0xE000;
    static constexpr std::uint64_t kIoSize = 0x1400;  // 0xE000-0xF3FF

    NinjaWarriorsSoundMemoryMap();

    // Copies `size` bytes into the full 128KB ROM image (zero-padding the rest).
    // Throws std::invalid_argument if size > kFullRomSize. Loads the complete
    // ROM, including all eight banked eighths.
    void LoadRom(const std::uint8_t* data, std::size_t size);

    // Selects which 16KB eighth of the ROM is visible at 0x4000-0x7FFF
    // (sound_bankswitch_w at 0xF200). Only the low 3 bits matter; the default
    // after Reset() is bank 0.
    void SetBank(std::uint8_t bank);

    // Maps an owning Machine's own endpoint (the scattered I/O glue) into this
    // space. `endpoint` must outlive this object.
    void MapDevice(std::uint64_t start, std::uint64_t size, AddressSpaceEndpoint* endpoint);

    std::uint8_t Read8(std::uint16_t address) const;
    void Write8(std::uint16_t address, std::uint8_t value);

private:
    // Covers the 0x0000-0x7FFF ROM window (fixed + banked) over one 128KB buffer.
    class RomEndpoint final : public AddressSpaceEndpoint {
    public:
        std::uint8_t Read8(std::uint64_t offset) const override;
        void Write8(std::uint64_t offset, std::uint8_t value) override;

        std::uint8_t* RawRom() { return rom_.data(); }
        void SetBank(std::uint8_t bank) { bank_ = bank & 0x07; }

    private:
        std::array<std::uint8_t, kFullRomSize> rom_{};
        std::uint8_t bank_ = 0;
    };

    // rom_endpoint_/ram_ must outlive space_ (members are destroyed in reverse order).
    RomEndpoint rom_endpoint_;
    MemoryRegion ram_;
    AddressSpace space_;
};
