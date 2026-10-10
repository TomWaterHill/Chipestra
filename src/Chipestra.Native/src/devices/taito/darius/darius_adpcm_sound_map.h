// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "address_space.h"
#include "memory_region.h"

#include <cstddef>
#include <cstdint>

// The second Z80 ("adpcm", ADPCM playback) address space:
//   0x0000-0xFFFF  ROM, entirely (the full 64KB "adpcm" ROM image - program
//                  and ADPCM sample data intermixed, no banking, no RAM/stack)
//
// I/O ports 0x00-0x03 are not modeled here; DariusMachine wires them directly
// to its ADPCM command latch, NMI enable flag and Msm5205DeviceAdapter.
class DariusAdpcmSoundMap {
public:
    static constexpr std::uint64_t kRomSize = 0x10000;  // the full 64KB "adpcm" ROM, mapped 1:1

    DariusAdpcmSoundMap();

    // Copies `size` bytes of `data` into the 64KB ROM image (zero-padding the
    // rest). Truncates rather than throwing if size > kRomSize.
    void LoadRom(const std::uint8_t* data, std::size_t size);

    std::uint8_t Read8(std::uint16_t address) const;
    void Write8(std::uint16_t address, std::uint8_t value);

private:
    // rom_ must outlive space_ (members are destroyed in reverse order).
    MemoryRegion rom_;
    AddressSpace space_;
};
