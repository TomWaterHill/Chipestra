// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "address_space.h"
#include "memory_region.h"

#include <cstdint>
#include <functional>

// The Konami GX400/Bubble System main 68000's address space (gx400_map = base map +
// ROM regions + the gx456 add-on map):
//   0x000000-0x00FFFF  ROM ("maincpu", 64KB, read-only)
//   0x010000-0x01FFFF  RAM (program RAM, 64KB)
//   0x020000-0x027FFF  sound Z80 shared RAM, word-addressed bridge (SharedRamBridge)
//   0x030000-0x03FFFF  RAM (character RAM; video is never rendered, so a plain RAM
//                      region is enough)
//   0x040000-0x04FFFF  unmapped (left open by the real address decode)
//   0x080000-0x0BFFFF  ROM ("maindata", 256KB, read-only)
//   0x05C001           soundlatch write  } dispatched by GradiusMachine's MainIoBus
//   0x05C402-0x05C407  DSW0/DSW1/TEST reads } (not owned here: they must reach
//   0x05C800           watchdog write    } GradiusCommandInterface state)
//   0x05CC00-0x05CC05  IN0/IN1/IN2 reads }
//   0x05E000-0x05E00F  outlatch/intlatch (74LS259 pair) }
//   0x070000-0x073FFF  RAM (work RAM, 16KB, gx456 add-on board)
//
// Independent of GradiusMemoryMap (the sound Z80's space); the layouts share nothing.
class GradiusMainMemoryMap {
public:
    static constexpr std::uint64_t kRomStart = 0x000000;
    static constexpr std::uint64_t kRomSize = 0x010000;  // 64KB

    static constexpr std::uint64_t kProgramRamStart = 0x010000;
    static constexpr std::uint64_t kProgramRamSize = 0x010000;  // 64KB

    static constexpr std::uint64_t kSharedRamBridgeStart = 0x020000;
    static constexpr std::uint64_t kSharedRamBridgeSize = 0x008000;  // 32KB of address space over 16KB of RAM

    static constexpr std::uint64_t kCharRamStart = 0x030000;
    static constexpr std::uint64_t kCharRamSize = 0x010000;  // 64KB

    static constexpr std::uint64_t kMainDataStart = 0x080000;
    static constexpr std::uint64_t kMainDataSize = 0x040000;  // 256KB

    static constexpr std::uint64_t kWorkRamStart = 0x070000;
    static constexpr std::uint64_t kWorkRamSize = 0x004000;  // 16KB

    // The scattered I/O window that GradiusMachine's MainIoBus is mapped into via
    // MapDevice(); covers 0x05C001 through 0x05E00F.
    static constexpr std::uint64_t kIoStart = 0x05C000;
    static constexpr std::uint64_t kIoSize = 0x002010;  // 0x05C000-0x05E00F

    // The shared-RAM callbacks address the sound Z80's 0x4000-0x7FFF window by offset
    // from its base (0-0x3FFF).
    using ReadSharedRamByteFn = std::function<std::uint8_t(std::uint16_t offset)>;
    using WriteSharedRamByteFn = std::function<void(std::uint16_t offset, std::uint8_t value)>;

    GradiusMainMemoryMap(ReadSharedRamByteFn readSharedRam, WriteSharedRamByteFn writeSharedRam);

    // Copies `size` bytes into the start of the ROM region, zero-padding the rest
    // (clamped to the region size).
    void LoadMainProgram(const std::uint8_t* data, std::size_t size);
    void LoadMainData(const std::uint8_t* data, std::size_t size);

    // Maps an endpoint owned by the Machine (MainIoBus) into this space. `endpoint` must
    // outlive this object.
    void MapDevice(std::uint64_t start, std::uint64_t size, AddressSpaceEndpoint* endpoint);

    std::uint8_t Read8(std::uint32_t address) const;
    void Write8(std::uint32_t address, std::uint8_t value);

private:
    // The 68000's word-addressed view of the sound Z80's byte-addressed shared RAM
    // (sound_sharedram_word_r/w): only the odd byte of each 16-bit slot reaches the
    // byte array; the even byte reads 0 and drops writes.
    class SharedRamBridge final : public AddressSpaceEndpoint {
    public:
        SharedRamBridge(ReadSharedRamByteFn read, WriteSharedRamByteFn write)
            : read_(std::move(read)), write_(std::move(write)) {}

        std::uint8_t Read8(std::uint64_t offset) const override {
            if ((offset & 1) == 0) return 0;  // high byte of the 16-bit slot always reads 0
            return read_ ? read_(static_cast<std::uint16_t>(offset / 2)) : 0;
        }
        void Write8(std::uint64_t offset, std::uint8_t value) override {
            if ((offset & 1) == 0) return;  // high byte writes never reach the shared RAM
            if (write_) write_(static_cast<std::uint16_t>(offset / 2), value);
        }

    private:
        ReadSharedRamByteFn read_;
        WriteSharedRamByteFn write_;
    };

    // Members are destroyed in reverse order, so the regions outlive space_.
    MemoryRegion rom_;
    MemoryRegion program_ram_;
    SharedRamBridge shared_ram_bridge_;
    MemoryRegion char_ram_;
    MemoryRegion main_data_;
    MemoryRegion work_ram_;
    AddressSpace space_;
};
