// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "address_space.h"
#include "memory_region.h"

#include <cstddef>
#include <cstdint>

// The SEGA Out Run sound Z80's memory address space (ROM and RAM). It matches
// XBoardMemoryMap but is kept as an independent class rather than shared:
//   0x0000-0xEFFF  ROM (read-only)
//   0xF000-0xF0FF  SegaPCM registers (mirrored 0x0700) - mapped by
//                  OutRunMachine via MapDevice(); this class only owns ROM/RAM
//   0xF800-0xFFFF  RAM
//
// The command latch is not part of this class: the hardware reads it through
// the Z80's I/O space (port 0x40), wired via IZ80Core::SetIoCallbacks.
class OutRunMemoryMap {
public:
    static constexpr std::uint64_t kRomStart = 0x0000;
    static constexpr std::uint64_t kRomSize = 0xF000;   // 0x0000-0xEFFF
    static constexpr std::uint64_t kRamStart = 0xF800;
    static constexpr std::uint64_t kRamSize = 0x0800;   // 0xF800-0xFFFF

    OutRunMemoryMap();

    // Copies `size` bytes of `data` into the start of the ROM region
    // (zero-padding the rest). Throws std::invalid_argument if size > kRomSize.
    void LoadRom(const std::uint8_t* data, std::size_t size);

    // SegaPCM register window: 0xF000-0xF0FF mirrored to 0xF700, i.e. the whole
    // 0xF000-0xF7FF range, routed to a device's AddressSpaceEndpoint. `endpoint`
    // must outlive this object.
    static constexpr std::uint64_t kSegaPcmStart = 0xF000;
    static constexpr std::uint64_t kSegaPcmSize = 0x0800;  // 0xF000-0xF7FF (0x100 + 0x0700 mirror)
    void MapDevice(std::uint64_t start, std::uint64_t size, AddressSpaceEndpoint* endpoint);

    std::uint8_t Read8(std::uint16_t address) const;
    void Write8(std::uint16_t address, std::uint8_t value);

private:
    // Declaration order matters: rom_/ram_ must outlive space_ (members are
    // destroyed in reverse declaration order).
    MemoryRegion rom_;
    MemoryRegion ram_;
    AddressSpace space_;
};
