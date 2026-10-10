// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "address_space.h"
#include "memory_region.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// The sound Z80's memory map:
//   0x0000-0x5FFF  ROM, fixed (no bank switching)
//   0x6000-0x63FF  RAM (1KB)
//   0x8000         soundlatch read (a single address, dispatched by
//                  GyrussMachine's IoBus rather than by this class)
//   everything else is unmapped
class GyrussSoundMemoryMap {
public:
    static constexpr std::uint64_t kRomStart = 0x0000;
    static constexpr std::uint64_t kRomSize = 0x6000;  // 24KB, fixed, no banking

    static constexpr std::uint64_t kRamStart = 0x6000;
    static constexpr std::uint64_t kRamSize = 0x0400;  // 1KB

    // The single soundlatch-read address GyrussMachine's own IoBus maps in.
    static constexpr std::uint64_t kSoundLatchAddress = 0x8000;

    GyrussSoundMemoryMap();

    // Copies `size` bytes of `data` into the ROM window (zero-padding the
    // rest). Throws std::invalid_argument if size > kRomSize.
    void LoadRom(const std::uint8_t* data, std::size_t size);

    // Maps a Machine-owned endpoint (the soundlatch-read glue) into this space.
    // `endpoint` must outlive this object.
    void MapDevice(std::uint64_t start, std::uint64_t size, AddressSpaceEndpoint* endpoint);

    std::uint8_t Read8(std::uint16_t address) const;
    void Write8(std::uint16_t address, std::uint8_t value);

private:
    class RomEndpoint final : public AddressSpaceEndpoint {
    public:
        RomEndpoint() : rom_(kRomSize, 0) {}
        std::uint8_t Read8(std::uint64_t offset) const override {
            return offset < rom_.size() ? rom_[offset] : 0;
        }
        void Write8(std::uint64_t, std::uint8_t) override {}  // ROM: writes discarded

        std::uint8_t* RawRom() { return rom_.data(); }
        std::size_t RawRomSize() const { return rom_.size(); }

    private:
        std::vector<std::uint8_t> rom_;
    };

    // Declaration order matters: rom_endpoint_/ram_ must outlive space_
    // (members are destroyed in reverse declaration order).
    RomEndpoint rom_endpoint_;
    MemoryRegion ram_;
    AddressSpace space_;
};
