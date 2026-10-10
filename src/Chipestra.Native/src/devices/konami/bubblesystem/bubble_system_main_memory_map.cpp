// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "bubble_system_main_memory_map.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace {

// Unmapped reads return 0x00 (not 0xFF) and writes are dropped; only the DSW/TEST/IN0-2
// ports return 0xFF, and those belong to BubbleSystemMachine's MainIoBus.
std::uint8_t OnUnmappedRead(std::uint64_t) {
    return 0x00;
}

void OnUnmappedWrite(std::uint64_t, std::uint8_t) {}

}  // namespace

void BubbleSystemMainMemoryMap::BubbleMemoryControllerBus::Write8(std::uint64_t offset, std::uint8_t value) {
    if (offset >= 8) return;
    control_ram_[offset] = value;
    // bubsys_005297_w's `offset == 1` is in WORD units: word 1 is byte offsets 2-3 here
    // (68000 big-endian). MameDerivedM68000Adapter::Write16 issues the high byte (offset 2)
    // then the low byte (offset 3) as separate Write8 calls; evaluating only on offset 3 makes
    // sure both halves are in place before the command is checked.
    if (offset == 3) {
        HandleControlWordOneWrite();
    }
}

void BubbleSystemMainMemoryMap::BubbleMemoryControllerBus::HandleControlWordOneWrite() {
    const std::uint16_t command =
        (static_cast<std::uint16_t>(control_ram_[2]) << 8) | static_cast<std::uint16_t>(control_ram_[3]);
    if (command != 1) return;  // only "read page" (1) does anything
    const std::uint16_t pageReg =
        (static_cast<std::uint16_t>(control_ram_[0]) << 8) | static_cast<std::uint16_t>(control_ram_[1]);
    const std::uint32_t page = static_cast<std::uint32_t>(pageReg & 0x7FF);
    const std::uint64_t src = static_cast<std::uint64_t>(page) * 0x90;
    if (src + 0x82 > bubble_memory_.size()) return;  // page beyond the image: no-op
    // MAME memcpy()s raw bytes into a host-native uint16_t array, which the 68000 bus then
    // reads as host words, so every byte pair is swapped relative to the region's byte order.
    // This class's shared RAM is big-endian byte-addressed, so the swap is applied explicitly.
    for (std::size_t i = 0; i < 0x80; i += 2) {
        bubsys_shared_ram_bytes_[0xF00 + i] = bubble_memory_[src + i + 1];
        bubsys_shared_ram_bytes_[0xF00 + i + 1] = bubble_memory_[src + i];
    }
    // The trailing 2 bytes of the 0x90-byte block become the next page register.
    // bubsys_005297_w computes `src[+0x80] | (src[+0x81] << 8)` as a host-native uint16_t
    // (low byte first). control_ram_[0]/[1] here are big-endian byte-addressed (high byte
    // first), so the bytes are stored reversed.
    control_ram_[0] = bubble_memory_[src + 0x81];
    control_ram_[1] = bubble_memory_[src + 0x80];
    if (on_page_read_complete_) on_page_read_complete_();
}

BubbleSystemMainMemoryMap::BubbleSystemMainMemoryMap(ReadSharedRamByteFn readSharedRam,
                                                       WriteSharedRamByteFn writeSharedRam,
                                                       std::function<void()> onPageReadComplete)
    : bubsys_shared_ram_(kBubsysSharedRamSize, /*read_only=*/false),
      program_ram_(kProgramRamSize, /*read_only=*/false),
      shared_ram_bridge_(std::move(readSharedRam), std::move(writeSharedRam)),
      char_ram_(kCharRamSize, /*read_only=*/false),
      bubble_memory_(),
      bubble_memory_controller_bus_(bubble_memory_, bubsys_shared_ram_.RawData(), std::move(onPageReadComplete)),
      work_ram_(kWorkRamSize, /*read_only=*/false),
      space_(OnUnmappedRead, OnUnmappedWrite) {
    space_.Map(kBubsysSharedRamStart, kBubsysSharedRamSize, &bubsys_shared_ram_);
    space_.Map(kProgramRamStart, kProgramRamSize, &program_ram_);
    space_.Map(kSharedRamBridgeStart, kSharedRamBridgeSize, &shared_ram_bridge_);
    space_.Map(kCharRamStart, kCharRamSize, &char_ram_);
    space_.Map(kBubbleControlStart, kBubbleControlSize, &bubble_memory_controller_bus_);
    space_.Map(kWorkRamStart, kWorkRamSize, &work_ram_);
    // MainIoBus is mapped later via MapDevice(); BubbleSystemMachine owns it.
}

void BubbleSystemMainMemoryMap::LoadBootProgramAndPrimeControlRam(const std::uint8_t* bootProgram,
                                                                    std::size_t size) {
    const std::size_t loaded = std::min(size, static_cast<std::size_t>(kBubsysSharedRamSize));
    std::uint8_t* ram = bubsys_shared_ram_.RawData();
    for (std::size_t i = 0; i < loaded; ++i) ram[i] = bootProgram[i];
    for (std::size_t i = loaded; i < kBubsysSharedRamSize; ++i) ram[i] = 0;

    // Control-RAM word 3 = byte offsets 6-7 = 0x240 big-endian (bubsys_init()'s shortcut).
    bubble_memory_controller_bus_.Write8(6, 0x02);
    bubble_memory_controller_bus_.Write8(7, 0x40);
}

void BubbleSystemMainMemoryMap::LoadBubbleMemory(const std::uint8_t* data, std::size_t size) {
    bubble_memory_.assign(data, data + size);
}

void BubbleSystemMainMemoryMap::MapDevice(std::uint64_t start, std::uint64_t size, AddressSpaceEndpoint* endpoint) {
    space_.Map(start, size, endpoint);
}

std::uint8_t BubbleSystemMainMemoryMap::Read8(std::uint32_t address) const {
    return space_.Read8(address);
}

void BubbleSystemMainMemoryMap::Write8(std::uint32_t address, std::uint8_t value) {
    space_.Write8(address, value);
}
