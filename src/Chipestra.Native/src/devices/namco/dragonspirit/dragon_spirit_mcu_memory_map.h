// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include <algorithm>
#include "address_space.h"
#include "memory_region.h"

#include <cstddef>
#include <cstdint>
#include <functional>

// The Namco System 1 MCU (CUS64 / HD63701V0) physical 16-bit address space. Kept separate from
// DragonSpiritMemoryMap (the audio CPU's map), one map class per CPU.
//
//   0x0000-0x0014  on-chip registers - intercepted inside MameDerivedHd63701Adapter, never reach this class
//   0x0080-0x00FF  internal RAM - likewise intercepted inside the CPU core
//   0x1000-0x1003  DSW read - no modeled DIP switches; returns 0xFF ("not set")
//   0x1400/0x1401  CONTROL0/CONTROL1 reads - 0xFF ("not pressed")
//   0x4000-0xBFFF  banked external "voice" ROM (32KB window x 24 banks = 0xC0000), selected via 0xD800
//   0xC000-0xC7FF  TRIRAM - the same physical array as the audio CPU's 0x7000-0x77FF window (shared by
//                  reference, so a write from either CPU is visible to the other; the voice-trigger
//                  mailbox at offset 0x02D/0x02E relies on this)
//   0xC800-0xCFFF  NVRAM - plain RAM, no persistence
//   0xD000/0xD400  DAC0/DAC1 write-only (routed to the Machine's DAC adapter via MapDevice())
//   0xD800         voice-ROM bank-select WRITE, handled internally
//   0xF000         IRQ1-ack WRITE
//   0xF000-0xFFFF  the MCU's internal 4KB ROM, READ only. On hardware the chip decodes this range itself;
//                  the CPU adapter does not, so this map supplies the ROM data. Writes there are ignored
//                  except the IRQ1-ack at 0xF000.
//
// Unmapped-read default is 0x00 (true open bus), except the DSW/CONTROL0/CONTROL1 input ports (0xFF).
class DragonSpiritMcuMemoryMap {
public:
    static constexpr std::uint64_t kDswStart = 0x1000;
    static constexpr std::uint64_t kDswSize = 0x0004;
    static constexpr std::uint64_t kControl0Address = 0x1400;
    static constexpr std::uint64_t kControl1Address = 0x1401;

    static constexpr std::uint64_t kVoiceRomStart = 0x4000;
    static constexpr std::uint64_t kVoiceRomWindowSize = 0x8000;  // 0x4000-0xBFFF, one 32KB bank
    static constexpr std::size_t kVoiceBankCount = 24;            // 6 chips x 4 sub-banks (mcu_bankswitch_w)
    static constexpr std::size_t kFullVoiceRomSize = kVoiceRomWindowSize * kVoiceBankCount;  // 0xC0000

    static constexpr std::uint64_t kTriRamStart = 0xC000;
    static constexpr std::uint64_t kTriRamSize = 0x0800;

    static constexpr std::uint64_t kNvramStart = 0xC800;
    static constexpr std::uint64_t kNvramSize = 0x0800;  // 0xC800-0xCFFF

    static constexpr std::uint64_t kDac0Address = 0xD000;
    static constexpr std::uint64_t kDac1Address = 0xD400;
    static constexpr std::uint64_t kBankSelectAddress = 0xD800;
    static constexpr std::uint64_t kIrqAckAddress = 0xF000;

    static constexpr std::uint64_t kInternalRomStart = 0xF000;
    static constexpr std::uint64_t kInternalRomSize = 0x1000;  // 0xF000-0xFFFF, "mcu" region

    // `sharedTriRam` must be the same MemoryRegion DragonSpiritMemoryMap owns and must outlive this object.
    explicit DragonSpiritMcuMemoryMap(MemoryRegion& sharedTriRam);

    // Zeroes the MCU's own RAM window (power-on state).
    void ClearNvram() { std::fill_n(nvram_.RawData(), nvram_.Size(), std::uint8_t{0}); }

    // Copies `size` bytes into the 4KB internal ROM (zero-padding the rest). Throws
    // std::invalid_argument if size > kInternalRomSize.
    void LoadInternalRom(const std::uint8_t* data, std::size_t size);

    // Copies `size` bytes into the 0xC0000 voice ROM (zero-padding the rest; the region is zero-filled
    // from construction). Throws std::invalid_argument if size > kFullVoiceRomSize.
    void LoadVoiceRom(const std::uint8_t* data, std::size_t size);

    // Maps a device endpoint (DAC0/DAC1) into this space; `endpoint` must outlive this object.
    void MapDevice(std::uint64_t start, std::uint64_t size, AddressSpaceEndpoint* endpoint);

    // Invoked on every write to kIrqAckAddress; the Machine wires it to clear the MCU's IRQ1 line.
    void SetIrqAckCallback(std::function<void()> callback) { irq_ack_callback_ = std::move(callback); }

    std::uint8_t Read8(std::uint32_t physicalAddress) const;
    void Write8(std::uint32_t physicalAddress, std::uint8_t value);

    // Test-only observation point.
    std::uint32_t CurrentBankForTest() const { return current_bank_; }

    // Test-only: invoked on every voice-ROM read with the absolute offset into the 0xC0000 image and the
    // byte read. Observers should fold values into a derived statistic rather than store raw ROM bytes.
    // `mutable` because Read8() is const; the hook only observes.
    void SetVoiceReadObserverForTest(std::function<void(std::size_t, std::uint8_t)> observer) const {
        voice_read_observer_for_test_ = std::move(observer);
    }

    // Test-only: fires on reads/writes of TRIRAM offset 0 (the "gate byte") made by the MCU itself through
    // this map (not the Machine's periodic re-assert via the audio-CPU-side map).
    void SetTriRamGateReadObserverForTest(std::function<void(std::uint8_t)> observer) const {
        triram_gate_read_observer_for_test_ = std::move(observer);
    }
    void SetTriRamGateWriteObserverForTest(std::function<void(std::uint8_t)> observer) const {
        triram_gate_write_observer_for_test_ = std::move(observer);
    }

private:
    // Write8() drops any write to TRIRAM offset 0 (the "gate byte") whose value is not kGateReadyByte. The
    // MCU's jump-table dispatcher also writes this byte (values bounded to [0x00,0x1E]); on hardware that
    // write was observed to never be visible to the next read (100% of 287,565 captured events), while
    // the Machine substitutes the main CPU's own 0xA6 write. The mechanism behind the hardware behaviour
    // is not understood; this reproduces the observed input/output behaviour.
    static constexpr std::uint8_t kGateReadyByte = 0xA6;  // == DragonSpiritMachine::kMainCpuTriRamReadyByte

    // voice_rom_/internal_rom_/nvram_ must outlive space_ (members are destroyed in reverse order).
    // triram_ is a reference to DragonSpiritMemoryMap's array; its lifetime is the Machine's responsibility.
    MemoryRegion internal_rom_;  // 4KB, read-only, "mcu" region
    MemoryRegion voice_rom_;     // 0xC0000, read-only
    MemoryRegion nvram_;
    MemoryRegion& triram_;  // SAME physical array as DragonSpiritMemoryMap's own triram_
    AddressSpace space_;
    std::uint32_t current_bank_ = 0;
    std::function<void()> irq_ack_callback_;
    mutable std::function<void(std::size_t, std::uint8_t)> voice_read_observer_for_test_;
    mutable std::function<void(std::uint8_t)> triram_gate_read_observer_for_test_;
    mutable std::function<void(std::uint8_t)> triram_gate_write_observer_for_test_;
};
