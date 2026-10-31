// license:BSD-3-Clause
// copyright-holders:Couriersud
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/sound/ay8910.cpp  [copyright-holders:Couriersud]
//   src/devices/sound/ay8910.h  [copyright-holders:Couriersud]
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#pragma once

#include "device_manager.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// Port of MAME's sunsoft_5b_sound_device: the sound chip of the Sunsoft 5B cartridge board, a YM2149 with its
// internal clock divider (PSG_TYPE_YM, PSG_HAS_INTERNAL_DIVIDER) and one summed output. Three tone generators, one
// noise generator and one 32-step envelope generator; the three channels are mixed through one resistor network
// (MAME's mix_3D() table, normalized so that the largest output is 3.0). No MAME type appears here.
//
// Time model: the chip is advanced by CPU cycles, never by a wall clock. It makes one sample every 16 CPU cycles (the
// input clock is the CPU clock and the internal divider halves the usual /8), at 111860.75 Hz. Like the original it
// makes the samples whose time is before the current CPU cycle: after `c` cycles it has made ceil(c / 16) of them. A
// Machine calls Render() up to the CPU's current cycle before every register write.
//
// The original's stream is declared with an integer rate (111860 Hz, the CPU clock / 16 cut down), so against the CPU
// clock it makes samples a little slower (0.0007%) than this class's 16-cycle rule; a caller that wants the original's
// sample count at every write uses RenderToSampleCount() with ceil(cycle * 111860 / 1789772).
//
// Output: the mixed value is split over the three channels for the meters, in proportion to each channel's
// conductance in the resistor network, so the three channel streams add up to the chip's output exactly (the chip's
// silent output is a small constant, 0.0066, which is part of the sum). A muted channel is taken out of the mix (its
// volume counts as 0 and its envelope bit as off) and the chip's output is calculated again without it; the other
// two channels get their share of that output, and the muted channel's own buffer keeps the share it has in the
// unmuted chip, so a Mixer that applies the mute (and the level meters) still see what the channel is doing.
class Sunsoft5bSoundDeviceAdapter final : public IDevice {
public:
    static constexpr std::size_t kChannelCount = 3;
    static constexpr std::uint32_t kCpuClockHz = 1789772;
    static constexpr std::uint32_t kCpuCyclesPerSample = 16;
    // The stream rate is 111860.75 Hz; this is the nearest integer.
    static constexpr std::uint32_t kOutputSampleRateHz = 111861;
    // The rate the original declares for its stream (the CPU clock / 16 cut down to a whole number). A Machine that
    // wants the original's sample count calls RenderToSampleCount() with SamplesBeforeCycle().
    static constexpr std::uint32_t kStreamRateHz = 111860;
    static constexpr std::uint64_t SamplesBeforeCycle(std::uint64_t cpuCycle) {
        return (cpuCycle * kStreamRateHz + kCpuClockHz - 1) / kCpuClockHz;
    }

    Sunsoft5bSoundDeviceAdapter();

    // Power-on state: every register 0 (the envelope shape register included), the noise register 1, no register
    // selected (data writes are ignored until the first address write).
    void Reset();

    // $C000-$DFFF write: selects the register with the low four bits of the value.
    void WriteAddress(std::uint8_t value);
    // $E000-$FFFF write: the selected register.
    void WriteData(std::uint8_t value);

    // Channels whose bit is set (bit n = channel n, A to C) are taken out of the mix.
    void SetMutedChannels(std::uint8_t mask) { muted_mask_ = mask & 7; }

    // Advances the chip by `cpuCycles` CPU cycles and appends the samples that become due to each of the three
    // vectors (see above). Returns the number appended to each.
    std::size_t Render(std::uint64_t cpuCycles, std::array<std::vector<float>, kChannelCount>& channelOut);

    // Makes samples until `totalSamples` have been made since Reset() (nothing if that many exist already). For a
    // caller that counts samples itself, for example from a stream rate that is not exactly the CPU clock / 16.
    std::size_t RenderToSampleCount(std::uint64_t totalSamples, std::array<std::vector<float>, kChannelCount>& channelOut);

    // The chip's output for fixed levels (0-15, or 0-31 with the envelope bit set for a channel) of the three
    // channels: MAME's table value.
    static float MixedOutput(const int (&level)[3], const bool (&envelope)[3]);

    // Test-only observation points.
    std::uint8_t RegisterForTest(std::uint8_t reg) const { return regs_[reg & 15]; }
    std::uint32_t TonePeriodForTest(std::size_t channel) const { return tone_[channel].period; }
    bool ToneOutputForTest(std::size_t channel) const { return tone_[channel].output != 0; }
    std::uint32_t EnvelopeVolumeForTest() const { return envelope_.volume; }
    std::int32_t EnvelopeStepForTest() const { return envelope_.step; }
    std::uint32_t NoiseRngForTest() const { return rng_; }
    std::uint64_t GeneratedSampleCountForTest() const { return samples_; }

private:
    struct Tone {
        std::uint32_t period = 0;
        std::uint8_t volume = 0;
        std::int32_t count = 0;
        std::uint8_t output = 0;
    };
    struct Envelope {
        std::uint32_t period = 0;
        std::int32_t count = 0;
        std::int8_t step = 0;
        std::uint32_t volume = 0;
        std::uint8_t hold = 0, alternate = 0, attack = 0, holding = 0;
    };

    void WriteRegister(std::uint8_t reg, std::uint8_t value);
    void TickOneSample();
    void MakeSample(std::array<std::vector<float>, kChannelCount>& channelOut);

    std::uint8_t regs_[16] = {};
    std::uint8_t register_latch_ = 0;
    bool active_ = false;
    Tone tone_[kChannelCount]{};
    Envelope envelope_{};
    std::uint32_t rng_ = 1;
    std::int32_t count_noise_ = 0;
    std::uint8_t prescale_noise_ = 0;
    bool vol_enabled_[kChannelCount] = {};
    std::uint8_t muted_mask_ = 0;
    std::uint64_t cycles_ = 0;   // CPU cycles since Reset()
    std::uint64_t samples_ = 0;  // samples made since Reset()
};
