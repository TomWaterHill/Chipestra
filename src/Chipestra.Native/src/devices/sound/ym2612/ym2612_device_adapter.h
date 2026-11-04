// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "clock.h"
#include "device_manager.h"

#include <cstdint>
#include <memory>
#include <vector>

// Chipestra-owned Sound Device wrapper around Aaron Giles' ymfm OPN2 core (thirdparty/ymfm, vendored from
// MAME, BSD-3-Clause; see THIRD-PARTY-NOTICES.md). No ymfm or MAME type appears in this header.
//
// Time model: never reads a wall clock. Audio generation advances only when Render() is called; one output sample
// is OPERATORS*prescale = 144 input clocks. Timers and the busy flag are kept in ticks of 1 / (3579545 * 7670453) of a
// second (the Mega Drive's Z80 clock times the chip's clock): they run on from the end of the last sample made, or from
// the time a Machine gives with SetAccessTime() before a write or a status read, so a driver that polls them sees
// them to the tick.
class Ym2612DeviceAdapter final : public IDevice {
public:
    // Six FM channels plus the DAC. In the chip the DAC replaces the output of FM channel 6 while it is enabled.
    static constexpr std::uint32_t kFmChannelCount = 6;
    static constexpr std::uint32_t kDacChannel = 6;
    static constexpr std::uint32_t kChannelCount = 7;

    // Mega Drive wiring: master clock 53693175 Hz / 7 (integer division).
    static constexpr ClockRate kInputClockRate{7'670'453};
    static constexpr std::uint64_t kInputClocksPerSample = 144;
    static constexpr std::uint32_t kOutputSampleRateHz = 7'670'453 / 144;  // 53267

    // `z80ClockHz`: the clock of the Z80 that accesses the chip (the master clock divided by 15 and truncated), which
    // sets the length of the ticks of SetAccessTime(); the chip clock itself is only a count of input clocks here.
    explicit Ym2612DeviceAdapter(std::uint64_t z80ClockHz = 3'579'545);
    ~Ym2612DeviceAdapter() override;

    Ym2612DeviceAdapter(const Ym2612DeviceAdapter&) = delete;
    Ym2612DeviceAdapter& operator=(const Ym2612DeviceAdapter&) = delete;

    // Rebuilds the chip (register file, timers and DAC state back to power-on).
    void Reset();

    // The reset the Mega Drive's Z80 reset line gives the chip: the register file and timers are cleared, but the
    // chip keeps running (its internal counters and the output clock carry on).
    void SoftReset();

    // The time of the next Write() or ReadStatus() in ticks (see above) since the chip was started. Without it an
    // access is taken to happen at the end of the last sample made.
    void SetAccessTime(std::uint64_t masterTicks);

    // Ports 0..3: address, data, upper address, upper data.
    void Write(std::uint32_t port, std::uint8_t value);
    std::uint8_t ReadStatus();

    // Advances the chip by `inputClocks` and appends per-channel mono waveforms in [-1, 1] to
    // `channelSamples` (kChannelCount buffers: FM 1-6, then the DAC). The pan bits (register 0xB4+) are not
    // folded in; the Machine maps ChannelOutputsLeft/Right() onto the logical channel pan. Partial samples
    // carry to the next call. The channels' left (right) outputs add up to the chip's left (right) output.
    std::size_t Render(std::uint64_t inputClocks, std::vector<std::vector<float>>& channelSamples);

    // Advances the chip like Render() but appends the chip's own stereo output (normalised by 32768).
    std::size_t RenderStereo(std::uint64_t inputClocks, std::vector<float>& left, std::vector<float>& right);

    bool ChannelOutputsLeft(std::uint32_t channel) const;
    bool ChannelOutputsRight(std::uint32_t channel) const;

    std::uint64_t GeneratedSampleCount() const;

    // The whole chip state (register file, envelope and phase counters, timers, output clock position), for a Machine
    // that saves it after a start-up run and restores it later.
    struct SavedState {
        std::vector<std::uint8_t> chip;
        std::uint64_t nowTicks = 0, busyEndTicks = 0, generatedSamples = 0, partialClocks = 0;
        bool timerActive[2] = {false, false};
        std::uint64_t timerDeadline[2] = {0, 0};  // ticks
    };
    SavedState SaveState();
    void RestoreState(SavedState& state);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
