// license:BSD-3-Clause
// copyright-holders:Miguel Angel Horna
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/sound/multipcm.cpp
//   src/devices/sound/multipcm.h
//   src/devices/sound/gew.cpp
//   src/devices/sound/gew.h
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#pragma once

#include "device_manager.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

// The Yamaha YMW-258-F "GEW8" (Sega 315-5560 MultiPCM): 28 voices of 8-bit or 12-bit linear PCM with an envelope generator,
// a pitch LFO and an amplitude LFO. The register semantics and the tables are those of MAME's multipcm/gew devices; no MAME
// type appears in this header or in the .cpp. The sample ROM is reached through a callback (the board decides how its address
// lines are wired and banked).
//
// Time model: never reads a wall clock. Audio advances only inside Render(); one output sample is kClockDivider input
// clocks (every voice advances once per sample).
class MultiPcmDeviceAdapter final : public IDevice {
public:
    static constexpr std::uint32_t kVoiceCount = 28;
    static constexpr std::uint32_t kClockDivider = 224;

    // Reads one byte of the chip's 22-bit sample address space.
    using SampleReadFn = std::function<std::uint8_t(std::uint32_t address)>;

    // `clockHz` sets the output rate (clock / 224) and the pitch steps.
    MultiPcmDeviceAdapter(std::uint32_t clockHz, SampleReadFn sampleRead);
    ~MultiPcmDeviceAdapter() override;

    MultiPcmDeviceAdapter(const MultiPcmDeviceAdapter&) = delete;
    MultiPcmDeviceAdapter& operator=(const MultiPcmDeviceAdapter&) = delete;

    // All voices stopped.
    void Reset();

    // The chip's three byte-wide ports: offset 0 = data, 1 = voice select, 2 = register select.
    void Write(std::uint32_t offset, std::uint8_t data);
    std::uint8_t Read() const { return 0; }

    // Advances the chip by `clocks` input clocks and appends each voice's mono waveform (envelope, tremolo and the
    // volume register applied, the left/right balance not) to `voiceSamples` (kVoiceCount buffers). The chip's left
    // output is the sum over voices of sample * VoiceGainLeft(v) and the right output the sum of sample * VoiceGainRight(v),
    // where the sample in the buffer is stored already multiplied by (VoiceGainLeft + VoiceGainRight): the Machine maps that
    // onto the LogicalChannel's pan and routing gain. Returns the number of output samples appended to each buffer.
    std::size_t Render(std::uint64_t clocks, std::vector<std::vector<float>>& voiceSamples);

    // The left and right factors of the voice at its last rendered sample (the chip's pan and volume tables;
    // 1.0 is the largest). 0 for a voice that is not playing.
    float VoiceGainLeft(std::uint32_t voice) const;
    float VoiceGainRight(std::uint32_t voice) const;
    bool VoicePlaying(std::uint32_t voice) const;
    // The voice's current total level (0 = loudest, 127 = the register's largest attenuation, about -60 dB), including the
    // step the chip is still moving toward after a write. 127 for a voice that is not playing.
    std::uint32_t VoiceTotalLevel(std::uint32_t voice) const;

    std::uint64_t GeneratedSampleCount() const;
    float OutputRateHz() const;

    // The whole chip state (the registers and the playback state of all voices), for a Machine that saves the state
    // after a start-up run and restores it later.
    struct SavedState;
    std::shared_ptr<SavedState> SaveState() const;
    void RestoreState(const SavedState& state);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
