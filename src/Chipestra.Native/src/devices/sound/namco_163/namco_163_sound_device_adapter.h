// license:BSD-3-Clause
// copyright-holders:cam900
// Namco 163 internal sound
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/sound/namco_163.cpp  [copyright-holders:cam900]
//   src/devices/sound/namco_163.h  [copyright-holders:cam900]
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#pragma once

#include "device_manager.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// Port of MAME's namco_163_sound_device: the wavetable sound of the Namco 163 cartridge chip, 1 to 8 channels
// of 4-bit samples in a 128-byte internal RAM. No MAME type appears here.
//
// Time model: the chip is advanced by CPU cycles, never by a wall clock. Like the original it makes one
// sample per step and the step handles one channel (the channels take turns, nothing is averaged), at the
// original's stream rate of CPU clock / 15 (119318 Hz). A Machine calls Render() up to the CPU's current cycle
// before every data write, and the number of samples made by a cycle is the original's: the first sample
// whose time (n / 119318 s) is not before that cycle.
//
// Output: for the meters every sample is delivered to the channel that made it and 0 to the other seven, so
// the eight channel streams add up to the chip's output exactly; the value is the sample (-120..120) divided
// by 128. A muted channel makes 0 (its phase still advances).
class Namco163SoundDeviceAdapter final : public IDevice {
public:
    static constexpr std::size_t kChannelCount = 8;
    static constexpr std::uint32_t kCpuClockHz = 1789772;
    static constexpr std::uint32_t kOutputSampleRateHz = kCpuClockHz / 15;  // 119318

    Namco163SoundDeviceAdapter();

    // Power-on state: the RAM is cleared, the address is 0 without auto-increment.
    void Reset();

    // $F800 write: bit 7 = auto-increment after every data access, bits 6-0 = RAM address.
    void WriteAddress(std::uint8_t value);
    // $4800 write / read: the RAM byte at the address (then the address increases if auto-increment is on).
    void WriteData(std::uint8_t value);
    std::uint8_t ReadData();
    // $E000 bit 6: while set the chip makes no sound and does not run.
    void SetDisabled(bool disabled) { disabled_ = disabled; }

    // Channels whose bit is set (bit n = RAM slot n, the channel whose registers are at $40 + 8n) make 0.
    void SetMutedChannels(std::uint8_t mask) { muted_mask_ = mask; }

    // Advances the chip by `cpuCycles` CPU cycles and appends the samples that become due to each of the
    // kChannelCount vectors (see above). Returns the number appended to each.
    std::size_t Render(std::uint64_t cpuCycles, std::array<std::vector<float>, kChannelCount>& channelOut);

    // Test-only observation points.
    const std::array<std::uint8_t, 128>& RamForTest() const { return ram_; }
    std::uint64_t GeneratedSampleCountForTest() const { return samples_; }

private:
    std::array<std::uint8_t, 128> ram_{};
    std::uint8_t reg_addr_ = 0x78;  // RAM offset of the channel that makes the next sample
    std::uint8_t addr_ = 0;
    bool inc_ = false;
    bool disabled_ = false;
    std::uint8_t muted_mask_ = 0;
    std::uint64_t cycles_ = 0;   // CPU cycles since Reset()
    std::uint64_t samples_ = 0;  // samples made since Reset()
};
