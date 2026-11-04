// license:BSD-3-Clause
// copyright-holders:Nicola Salmoria
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/sound/sn76496.cpp  [copyright-holders:Nicola Salmoria]
//   src/devices/sound/sn76496.h  [copyright-holders:Nicola Salmoria]
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#pragma once

#include "device_manager.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// Port of the Sega variant of MAME's sn76496_base_device (segapsg_device): the programmable sound generator of the
// Master System, Game Gear and Mega Drive VDP. Three square-wave tone generators and one noise generator, a 16-bit
// noise shift register with taps C and F, an inverted output, a clock divider of 8, and a frequency of 0 when 0 is
// written. No MAME type appears here.
//
// Time model: the chip is advanced by input clocks, never by a wall clock. The original's stream runs at half the
// input clock and its generators step once every 8 stream samples; this class makes one sample per 2 input clocks
// (the same samples), so a volume write between two generator steps is heard at the same sample as in the original.
// A Machine calls Render() up to the CPU's current clock before every register write.
//
// Output: the four channel streams add up to the chip's output. Each is the channel's own contribution after the
// chip's output inversion, in [-1, 1].
class Sn76489SoundDeviceAdapter final : public IDevice {
public:
    static constexpr std::size_t kChannelCount = 4;
    static constexpr std::uint64_t kInputClocksPerSample = 2;

    Sn76489SoundDeviceAdapter();

    // Power-on state (the tone registers selected as after a reset of the Sega chip).
    void Reset();

    // The single data port.
    void Write(std::uint8_t value);

    // Advances the chip by `inputClocks` input clocks and appends one sample per 2 clocks to each of the four
    // buffers in `channelSamples`. A leftover clock carries over to the next call. Returns the number of samples.
    std::size_t Render(std::uint64_t inputClocks, std::vector<std::vector<float>>& channelSamples);

    // Test-only: samples made since construction/Reset().
    std::uint64_t GeneratedSampleCount() const { return generated_samples_; }

    // The whole chip state, for a Machine that saves it after a start-up run and restores it later.
    struct SavedState {
        std::array<int, 8> reg{};
        int lastRegister = 3;
        std::array<int, 4> volume{};
        int rng = 0;
        int currentClock = 0;
        std::array<int, 4> period{}, count{}, output{};
        std::uint64_t partialClocks = 0, generatedSamples = 0;
    };
    SavedState SaveState() const {
        return {register_, last_register_, volume_, rng_, current_clock_, period_, count_, output_, partial_clocks_, generated_samples_};
    }
    void RestoreState(const SavedState& s) {
        register_ = s.reg; last_register_ = s.lastRegister; volume_ = s.volume; rng_ = s.rng; current_clock_ = s.currentClock;
        period_ = s.period; count_ = s.count; output_ = s.output; partial_clocks_ = s.partialClocks; generated_samples_ = s.generatedSamples;
    }

private:
    void Step();

    std::array<int, 8> register_{};
    int last_register_ = 3;
    std::array<int, 16> vol_table_{};
    std::array<int, 4> volume_{};
    int rng_ = 0;
    int current_clock_ = 0;
    std::array<int, 4> period_{};
    std::array<int, 4> count_{};
    std::array<int, 4> output_{};
    std::uint64_t partial_clocks_ = 0;
    std::uint64_t generated_samples_ = 0;
};
