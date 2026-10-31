// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "device_manager.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

// Chipestra-owned wrapper around ymfm's ds1001 (Konami VRC7 / DS1001, an OPLL derivative), see
// THIRD-PARTY-NOTICES.md. No ymfm type appears in this header.
//
// The chip has 6 FM channels. The 15 built-in instruments live in a 144-byte table inside the chip; it is
// passed in at construction (the ds1001_instruments.bin device ROM, in the layout the original emulator
// reads it) and the adapter cannot be built without it. The user supplies that ROM; nothing here carries
// its contents.
//
// Registers: the OPLL channels 7-9 and the rhythm mode do not exist on the VRC7, so writes to them are
// dropped.
//
// Time model: never reads a wall clock. The input clock is the chip clock (3579545 Hz, twice the CPU clock
// of the Famicom); one output sample takes 72 input clocks. A Machine calls Render() up to the current time
// before every register write.
//
// Output: each channel as value / 32768. The sum of the six channels is exactly the chip's mixed output
// (the original divides by nine and truncates the sum; the rounding is shared out between the channels so
// that their sum stays equal to that value).
class Vrc7SoundDeviceAdapter final : public IDevice {
public:
    static constexpr std::size_t kChannelCount = 6;
    static constexpr std::size_t kInstrumentTableSize = 144;

    // CRC-32 of the original device ROM (for the resource that supplies the table).
    static constexpr std::uint32_t kInstrumentTableCrc32 = 0x9d699efc;

    // 21477272 / 6 Hz, truncated as the original does.
    static constexpr std::uint32_t kInputClockHz = 3579545;
    static constexpr std::uint32_t kInputClocksPerSample = 72;
    // The original's stream rate: the clock divided by 72, truncated (the chip really makes 49715.9 samples a
    // second).
    static constexpr std::uint32_t kOutputSampleRateHz = kInputClockHz / kInputClocksPerSample;

    // Throws std::invalid_argument unless `instrumentTable` has exactly kInstrumentTableSize bytes.
    explicit Vrc7SoundDeviceAdapter(const std::vector<std::uint8_t>& instrumentTable);
    ~Vrc7SoundDeviceAdapter() override;

    Vrc7SoundDeviceAdapter(const Vrc7SoundDeviceAdapter&) = delete;
    Vrc7SoundDeviceAdapter& operator=(const Vrc7SoundDeviceAdapter&) = delete;

    // Power-on state (the registers cleared; the instrument table is kept).
    void Reset();

    void WriteAddress(std::uint8_t value);
    void WriteData(std::uint8_t value);

    // Channels whose bit is set (bit n = channel n) make 0. The chip keeps running, so unmuting continues
    // where the channel would have been.
    void SetMutedChannels(std::uint8_t mask);

    // Advances the chip by `inputClocks` input clocks and appends one sample per 72 clocks to each of the
    // kChannelCount vectors. A partial sample carries to the next call. Returns the number appended to each.
    std::size_t Render(std::uint64_t inputClocks, std::array<std::vector<float>, kChannelCount>& channelOut);

    // Test-only: output samples made since construction/Reset().
    std::uint64_t GeneratedSampleCountForTest() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
