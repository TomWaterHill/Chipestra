// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "address_space.h"
#include "memory_region.h"

#include <cstdint>
#include <functional>
#include <vector>

// The real Bubble System main 68000's address space (bubsys_state::main_map =
// gx400_base_map + bubble-memory-specific regions):
//   0x000000-0x000FFF  RAM ("bubsys_shared", 4KB) shared with the K005297 bubble-memory
//                      controller. On real power-on it is empty and the 68000 is held in
//                      /RESET while the bubble memory warms up; the 005297 then copies the
//                      boot program here and releases /RESET. Like MAME's bubsys_init(),
//                      this class skips the warm-up delay and does the copy once, from
//                      Initialize(), via LoadBootProgramAndPrimeControlRam().
//   0x010000-0x01FFFF  RAM (program RAM), as on GX400
//   0x020000-0x027FFF  sound Z80 shared RAM bridge, word-addressed (SharedRamBridge), as on
//                      GX400
//   0x030000-0x03FFFF  RAM (character RAM), as on GX400
//   0x040000-0x040007  K005297 control registers (bubsys_control_ram, 4x uint16 words):
//                      BubbleMemoryControllerBus
//   0x05C001 etc.      scattered I/O (soundlatch/DSW/TEST/IN0-2/watchdog/outlatch/intlatch),
//                      owned by BubbleSystemMachine's MainIoBus
//   0x070000-0x073FFF  RAM (work RAM, gx456 add-on board), as on GX400
//
// Independent of GradiusMainMemoryMap: RAM instead of ROM at $0 and the K005297 page
// transfer make this a structurally different boot model.
class BubbleSystemMainMemoryMap {
public:
    static constexpr std::uint64_t kBubsysSharedRamStart = 0x000000;
    static constexpr std::uint64_t kBubsysSharedRamSize = 0x001000;  // 4KB

    static constexpr std::uint64_t kProgramRamStart = 0x010000;
    static constexpr std::uint64_t kProgramRamSize = 0x010000;  // 64KB

    static constexpr std::uint64_t kSharedRamBridgeStart = 0x020000;
    static constexpr std::uint64_t kSharedRamBridgeSize = 0x008000;  // 32KB of address space over 16KB of RAM

    static constexpr std::uint64_t kCharRamStart = 0x030000;
    static constexpr std::uint64_t kCharRamSize = 0x010000;  // 64KB

    static constexpr std::uint64_t kBubbleControlStart = 0x040000;
    static constexpr std::uint64_t kBubbleControlSize = 0x000008;  // 4x uint16 registers

    // Same position/size as GradiusMainMemoryMap's I/O window (shared gx400_base_map).
    static constexpr std::uint64_t kIoStart = 0x05C000;
    static constexpr std::uint64_t kIoSize = 0x002010;  // 0x05C000-0x05E00F

    static constexpr std::uint64_t kWorkRamStart = 0x070000;
    static constexpr std::uint64_t kWorkRamSize = 0x004000;  // 16KB

    // Offsets are relative to the window's own base, as in GradiusMainMemoryMap.
    using ReadSharedRamByteFn = std::function<std::uint8_t(std::uint16_t offset)>;
    using WriteSharedRamByteFn = std::function<void(std::uint16_t offset, std::uint8_t value)>;

    // `onPageReadComplete` fires synchronously when a K005297 "read page" command finishes
    // copying (bubsys_005297_w's set_input_line(5, HOLD_LINE)); the owning Machine uses it
    // to raise IRQ5. This class never touches the CPU itself.
    BubbleSystemMainMemoryMap(ReadSharedRamByteFn readSharedRam, WriteSharedRamByteFn writeSharedRam,
                               std::function<void()> onPageReadComplete);

    // bubsys_init()'s shortcut: copies `size` bytes of `bootProgram` into bubsys_shared_ram
    // at offset 0 (clamped to its size; the 68000 reads its reset SP/PC from there), then
    // sets control-RAM word 3 to 0x240 (the "005297 finished, safe to run" flag; the 68000
    // resets if the value is not correct).
    void LoadBootProgramAndPrimeControlRam(const std::uint8_t* bootProgram, std::size_t size);

    // Stores the cartridge image that BubbleMemoryControllerBus reads pages from, already in
    // MAME's internal byte order. An empty or zero-filled image does NOT make boot safe: the
    // boot program loads a second-stage loader from bubble-memory pages into program RAM and
    // executes it, so without cartridge data the 68000 runs away (as MAME's BIOS-only `bubsys`
    // set does). Sound-only playback without a cartridge needs the 68000 not to run at all.
    void LoadBubbleMemory(const std::uint8_t* data, std::size_t size);

    // Maps an endpoint owned by the Machine (MainIoBus) into this space. `endpoint` must
    // outlive this object.
    void MapDevice(std::uint64_t start, std::uint64_t size, AddressSpaceEndpoint* endpoint);

    std::uint8_t Read8(std::uint32_t address) const;
    void Write8(std::uint32_t address, std::uint8_t value);

private:
    // Same semantics as GradiusMainMemoryMap's SharedRamBridge (sound_sharedram_word_r/w);
    // duplicated rather than shared.
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

    // The K005297's control-register window (bubsys_005297_w). A write completing control-RAM
    // word 1 (byte offsets 2-3) with value 1 ("read") copies 0x80 bytes from the bubble-memory
    // image at `page*0x90` (page = control-RAM word 0 & 0x7FF) into bubsys_shared_ram at byte
    // offset 0xF00, loads the trailing 2 bytes of that 0x90-byte block into control-RAM word 0
    // (the cartridge's page-chain linkage), then fires on_page_read_complete_ (IRQ5). Values
    // 0 ("NOP") and 2 ("write") are no-ops, as in nemesis.cpp.
    class BubbleMemoryControllerBus final : public AddressSpaceEndpoint {
    public:
        BubbleMemoryControllerBus(const std::vector<std::uint8_t>& bubbleMemory, std::uint8_t* bubsysSharedRamBytes,
                                   std::function<void()> onPageReadComplete)
            : bubble_memory_(bubbleMemory),
              bubsys_shared_ram_bytes_(bubsysSharedRamBytes),
              on_page_read_complete_(std::move(onPageReadComplete)) {}

        std::uint8_t Read8(std::uint64_t offset) const override {
            return offset < 8 ? control_ram_[offset] : 0;
        }
        void Write8(std::uint64_t offset, std::uint8_t value) override;

    private:
        void HandleControlWordOneWrite();

        const std::vector<std::uint8_t>& bubble_memory_;
        std::uint8_t* bubsys_shared_ram_bytes_;
        std::function<void()> on_page_read_complete_;
        std::uint8_t control_ram_[8] = {};
    };

    // Members are destroyed in reverse order, so the regions and bus objects outlive space_.
    MemoryRegion bubsys_shared_ram_;
    MemoryRegion program_ram_;
    SharedRamBridge shared_ram_bridge_;
    MemoryRegion char_ram_;
    std::vector<std::uint8_t> bubble_memory_;
    BubbleMemoryControllerBus bubble_memory_controller_bus_;
    MemoryRegion work_ram_;
    AddressSpace space_;
};
