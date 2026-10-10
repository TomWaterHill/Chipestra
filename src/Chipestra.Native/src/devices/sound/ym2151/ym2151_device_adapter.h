// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "clock.h"
#include "device_manager.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

// Chipestra-owned Sound Device wrapper around Aaron Giles' ymfm OPM core (thirdparty/ymfm, vendored from
// MAME, BSD-3-Clause; see THIRD-PARTY-NOTICES.md). No ymfm or MAME type appears in this header: the chip
// and the ymfm_interface implementation live behind `Impl` in the .cpp (pimpl).
//
// Time model: never reads a wall clock. Timers and audio generation advance a chip-input-clock counter only
// when Render() is called; one output sample is OPERATORS*prescale = 64 input clocks. ymfm hands timer
// deadlines in those same units, so timer expiry and sample generation stay consistent.
//
// Implements IDevice so a Machine's DeviceManager owns it.
class Ym2151DeviceAdapter final : public IDevice {
public:
    // YM2151 has 8 four-operator FM channels; each becomes one LogicalChannel.
    static constexpr std::uint32_t kChannelCount = 8;

    // X-Board wiring: input clock = 16MHz / 4 = 4MHz. The native FM output rate is that divided by
    // OPERATORS*prescale (32*2 = 64).
    static constexpr ClockRate kInputClockRate{4'000'000};
    static constexpr std::uint32_t kOutputSampleRateHz = 4'000'000 / 64;  // 62500

    // Input clocks per generated output sample (ymfm: OPERATORS(32) * DEFAULT_PRESCALE(2)).
    static constexpr std::uint64_t kInputClocksPerSample = 64;

    // Called (from inside Render/register-write processing) when the chip's IRQ line changes level.
    // `asserted` mirrors the ~IRQ pin: true = a Timer A/B overflow enabled in the mode register is
    // pending. A Machine wires this to its CPU's maskable interrupt line.
    using IrqHandler = std::function<void(bool asserted)>;

    explicit Ym2151DeviceAdapter(IrqHandler irq_handler);
    ~Ym2151DeviceAdapter() override;

    Ym2151DeviceAdapter(const Ym2151DeviceAdapter&) = delete;
    Ym2151DeviceAdapter& operator=(const Ym2151DeviceAdapter&) = delete;

    // Power-on-equivalent reset (register file cleared, timers stopped, IRQ deasserted). An asserted IRQ
    // line is driven low through the handler exactly once.
    void Reset();

    // Register access: address latch (A0=0) and data (A0=1); reads return the status byte regardless of A0.
    void WriteAddress(std::uint8_t value);
    void WriteData(std::uint8_t value);
    std::uint8_t ReadStatus();

    // Advances the chip by `inputClocks` input clocks and appends per-channel mono float samples to
    // `channelSamples` (a kChannelCount-long vector of buffers; entry c gets FM channel c). Samples are
    // each channel's un-attenuated waveform in [-1, 1]; the L/R routing (register 0x20 bits 6/7) is not
    // folded in, so the Machine reads ChannelOutputsLeft/Right() and maps it onto the LogicalChannel pan.
    // Partial samples carry to the next call.
    std::size_t Render(std::uint64_t inputClocks,
                       std::vector<std::vector<float>>& channelSamples);

    // Per-channel L/R bus enables from the latest write to register 0x20+ch (bit 6 = left, bit 7 = right).
    // Both false until the driver programs it. On/off per bus, not a proportional pan.
    bool ChannelOutputsLeft(std::uint32_t channel) const;
    bool ChannelOutputsRight(std::uint32_t channel) const;

    // Test-only: output samples generated since construction/Reset().
    std::uint64_t GeneratedSampleCount() const;

    // Diagnostic: output samples where some channel's left and right differed.
    std::uint64_t StereoSampleCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    // Latched register address and the per-channel L/R bus enables decoded from writes to 0x20-0x27.
    std::uint8_t latched_address_ = 0;
    bool ch_out_left_[kChannelCount] = {};
    bool ch_out_right_[kChannelCount] = {};
};
