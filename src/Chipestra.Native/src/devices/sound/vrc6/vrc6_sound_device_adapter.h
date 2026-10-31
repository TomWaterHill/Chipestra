// license:BSD-3-Clause
// copyright-holders:R. Belmont
// Konami VRC6 additional sound channels
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/sound/vrc6.cpp  [copyright-holders:R. Belmont]
//   src/devices/sound/vrc6.h  [copyright-holders:R. Belmont]
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#pragma once

#include "device_manager.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// Port of MAME's vrc6snd_device: the two pulse channels and the sawtooth channel of the Konami VRC VI
// cartridge chip. No MAME type appears here.
//
// Time model: the chip is advanced by CPU cycles, never by a wall clock. One output sample is produced per
// CPU cycle (the original's stream runs at its clock, 21.477272 MHz / 12, about 1.79 MHz). A Machine calls
// Render() up to the CPU's current cycle before every register write, as the original updates its stream
// first in every write.
//
// Output: each channel's value divided by 128 (pulse 0-15, sawtooth 0-31; the three together are at most
// 61 / 128). The values are unsigned, so the host removes the DC offset. The original adds the chip to the
// CPU's output at gain 1.0 on a mono output. While the global halt bit ($9003 bit 0) is set the original does
// not touch its stream (silence) and the generators do not advance.
class Vrc6SoundDeviceAdapter final : public IDevice {
public:
    static constexpr std::size_t kChannelCount = 3;  // pulse 1, pulse 2, sawtooth
    static constexpr std::size_t kPulse1 = 0;
    static constexpr std::size_t kPulse2 = 1;
    static constexpr std::size_t kSaw = 2;

    // Input clock of the real chip (NTSC): 21.477272 MHz / 12.
    static constexpr std::uint32_t kClockHz = 1789772;

    Vrc6SoundDeviceAdapter();

    // Power-on state (the original's device reset).
    void Reset();

    // `offset` is the original's register offset: 0x000-0x003 pulse 1 control, frequency low, frequency high
    // (bit 7 enable) and the global frequency control ($9003); 0x100-0x102 pulse 2; 0x200-0x202 sawtooth
    // (rate, frequency low, frequency high). Other offsets do nothing.
    void Write(unsigned offset, std::uint8_t value);

    // The cartridge bus side: $9000-$9003, $A000-$A002 and $B000-$B002 by CPU address, for the
    // address-line order of mapper 24 (register number = address bits 1..0). Other addresses do nothing.
    void WriteIo(std::uint16_t address, std::uint8_t value);

    // Channels whose bit is set (bit 0 pulse 1, bit 1 pulse 2, bit 2 sawtooth) make 0. The generators keep
    // running, so unmuting continues where the channel would have been.
    void SetMutedChannels(std::uint8_t mask) { muted_mask_ = mask; }

    // Advances the chip by `cpuCycles` CPU cycles and appends one value per cycle to each of the
    // kChannelCount vectors in `channelOut`. Returns the number of samples appended to each.
    std::size_t Render(std::uint64_t cpuCycles, std::array<std::vector<float>, kChannelCount>& channelOut);

    // Test-only observation points - not part of production playback.
    int PulseDutyForTest(std::size_t pulse) const { return pulse_duty_[pulse]; }
    int MasterFreqShiftForTest() const { return master_freq_; }
    int SawAccumulatorForTest() const { return saw_accum_; }

private:
    int PulsePeriod(std::size_t pulse) const;
    int SawPeriod() const;
    void TickPulse(std::size_t pulse);
    void TickSaw();

    std::uint8_t freq_ctrl_ = 0;
    std::uint8_t pulse_ctrl_[2] = {};
    std::uint8_t saw_rate_ = 0;
    int master_freq_ = 0;
    std::uint8_t pulse_frql_[2] = {};
    std::uint8_t pulse_frqh_[2] = {};
    std::uint8_t pulse_duty_[2] = {};
    std::uint8_t saw_frql_ = 0;
    std::uint8_t saw_frqh_ = 0;
    std::uint8_t saw_clock_ = 0;
    std::uint8_t saw_accum_ = 0;
    int ticks_[3] = {};
    std::uint8_t output_[3] = {};
    std::uint8_t muted_mask_ = 0;
};
