// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "address_space.h"
#include "memory_region.h"

#include <cstddef>
#include <cstdint>

// The Konami GX400/Bubble System sound Z80's address space (gx400_sound_map):
//   0x0000-0x1FFF  ROM (read-only, 8KB)
//   0x4000-0x7FFF  RAM shared with the main 68000. The sound driver polls a location
//                  here during boot and only proceeds once the main CPU writes the
//                  expected handshake value, so the main CPU must be emulated.
//   0x8000-0x87FF  RAM ("voiceram", VLM5030 speech data). The VLM5030 reads it through a
//                  callback into GradiusMachine, not through this class (the chip's
//                  own address bus is a separate, narrower space).
//   0xA000-0xAFFF  K005289 ld1_w decode window (glue endpoint owned by GradiusMachine)
//   0xC000-0xCFFF  K005289 ld2_w decode window (same)
//   0xE000-0xE4FF  scattered I/O (soundlatch read, AY8910 x2, K005289 trigger, VLM5030
//                  speech_w); the real addresses interleave byte by byte, so
//                  GradiusMachine owns a glue AddressSpaceEndpoint for this window
//
// Independent of OutRunMemoryMap/XBoardMemoryMap; the layouts share nothing.
class GradiusMemoryMap {
public:
    static constexpr std::uint64_t kRomStart = 0x0000;
    static constexpr std::uint64_t kRomSize = 0x2000;  // 0x0000-0x1FFF

    static constexpr std::uint64_t kSharedRamStart = 0x4000;
    static constexpr std::uint64_t kSharedRamSize = 0x4000;  // 0x4000-0x7FFF

    static constexpr std::uint64_t kVoiceRamStart = 0x8000;
    static constexpr std::uint64_t kVoiceRamSize = 0x0800;  // 0x8000-0x87FF

    static constexpr std::uint64_t kK005289Ld1Start = 0xA000;
    static constexpr std::uint64_t kK005289Ld1Size = 0x1000;  // 0xA000-0xAFFF

    static constexpr std::uint64_t kK005289Ld2Start = 0xC000;
    static constexpr std::uint64_t kK005289Ld2Size = 0x1000;  // 0xC000-0xCFFF

    // The scattered I/O window GradiusMachine maps its endpoint into via MapDevice();
    // covers 0xE000 through 0xE405.
    static constexpr std::uint64_t kIoStart = 0xE000;
    static constexpr std::uint64_t kIoSize = 0x0500;  // 0xE000-0xE4FF

    GradiusMemoryMap();

    // Copies `size` bytes of `data` into the start of the ROM region
    // (zero-padding the rest). Throws std::invalid_argument if size > kRomSize.
    void LoadRom(const std::uint8_t* data, std::size_t size);

    // Maps an endpoint owned by the Machine (I/O glue, K005289 windows, VLM5030 window)
    // into this space. `endpoint` must outlive this object.
    void MapDevice(std::uint64_t start, std::uint64_t size, AddressSpaceEndpoint* endpoint);

    std::uint8_t Read8(std::uint16_t address) const;
    void Write8(std::uint16_t address, std::uint8_t value);

private:
    // Members are destroyed in reverse order, so the regions outlive space_.
    MemoryRegion rom_;
    MemoryRegion shared_ram_;
    MemoryRegion voice_ram_;
    AddressSpace space_;
};
