// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "address_space.h"
#include "memory_region.h"

#include <cstddef>
#include <cstdint>

// The Data East Vapor Trail sound H6280's physical (21-bit) address space, following the MAME sound_map:
//   0x000000-0x00FFFF  ROM (read-only), the "soundcpu" 64KB program
//   0x100000-0x100001  YM2203 (address/data)
//   0x110000-0x110001  YM2151 (address/data)
//   0x120000            OKIM6295 #1 (command write / status read, single address)
//   0x130000            OKIM6295 #2 (command write / status read, single address)
//   0x140000-0x140001  sound latch (read-only; the 68000 writes it from its own address space, reading
//                       either byte here returns the latched value and clears IRQ1)
//   0x1F0000-0x1F1FFF  RAM (8KB)
//
// Device windows are mapped by VaporTrailMachine via MapDevice(); this class owns only ROM and RAM.
//
// Unmapped reads return 0x00 (open bus), not 0xFF: there are no input ports in this address space, and 0xFF
// is only right for a modeled input reading "nothing pressed".
class VaporTrailMemoryMap {
public:
    static constexpr std::uint64_t kRomStart = 0x000000;
    static constexpr std::uint64_t kRomSize = 0x010000;  // 0x000000-0x00FFFF

    static constexpr std::uint64_t kYm2203Start = 0x100000;
    static constexpr std::uint64_t kYm2203Size = 0x000002;

    static constexpr std::uint64_t kYm2151Start = 0x110000;
    static constexpr std::uint64_t kYm2151Size = 0x000002;

    static constexpr std::uint64_t kOki1Start = 0x120000;
    static constexpr std::uint64_t kOki1Size = 0x000001;

    static constexpr std::uint64_t kOki2Start = 0x130000;
    static constexpr std::uint64_t kOki2Size = 0x000001;

    // Two bytes (0x140000/0x140001): both read back the same latched value.
    static constexpr std::uint64_t kLatchStart = 0x140000;
    static constexpr std::uint64_t kLatchSize = 0x000002;

    static constexpr std::uint64_t kRamStart = 0x1F0000;
    static constexpr std::uint64_t kRamSize = 0x002000;  // 0x1F0000-0x1F1FFF

    VaporTrailMemoryMap();

    // Copies `size` bytes into the ROM region (zero-padding the rest). Throws std::invalid_argument if
    // size > kRomSize.
    void LoadRom(const std::uint8_t* data, std::size_t size);

    // Maps a Machine-owned device endpoint into this space. `endpoint` must outlive this object.
    void MapDevice(std::uint64_t start, std::uint64_t size, AddressSpaceEndpoint* endpoint);

    std::uint8_t Read8(std::uint32_t physicalAddress) const;
    void Write8(std::uint32_t physicalAddress, std::uint8_t value);

private:
    // Declaration order matters: rom_/ram_ must outlive space_ (non-owning mappings).
    MemoryRegion rom_;
    MemoryRegion ram_;
    AddressSpace space_;
};
