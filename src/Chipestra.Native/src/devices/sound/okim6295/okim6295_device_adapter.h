// license:BSD-3-Clause
// copyright-holders:Mirko Buffoni,Aaron Giles
// copyright-holders:Andrew Gardner,Aaron Giles
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/sound/okim6295.cpp  [copyright-holders:Mirko Buffoni,Aaron Giles]
//   src/devices/sound/okiadpcm.cpp  [copyright-holders:Andrew Gardner,Aaron Giles]
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#pragma once

#include "device_manager.h"

#include <cstdint>
#include <vector>

// Reimplementation of MAME's okim6295_device (OKI MSM6295, 4-voice ADPCM sample player); see
// THIRD-PARTY-NOTICES.md. No MAME type appears in this header or the .cpp. The MAME class is tightly coupled to
// device_t/device_rom_interface/sound_stream, so the small command protocol and ADPCM decode are reimplemented
// directly.
//
// Protocol: a command byte with bit 7 set starts a 2-byte sequence (low 7 bits = one of 128 "phrase" numbers, an
// index into an 8-byte table at the start of the ROM giving that phrase's 18-bit start/stop byte offsets); the
// second byte's upper 4 bits select which of the 4 voices start playing it (more than one bit set starts several
// voices with the same sample, a hardware quirk MAME preserves), its low 4 bits select the output attenuation
// (16-step table, only the first 9 steps are audible). A byte with bit 7 clear and no phrase pending is a stop
// command (bits 3-6 select which voice(s) to silence). Read() reports, one bit per voice, which voices are playing.
//
// Sample rate: one ADPCM sample is decoded every `divisor` input clocks, where `divisor` is 132 or 165 depending on
// pin 7 at construction (a strapping pin, fixed per board). The adapter has no clock-rate constant of its own: two
// chips on one board can share a divisor but run at different clocks, so the owning Machine computes the native
// sample rate as `actualClockHz / Divisor()`.
//
// Deterministic time model: never reads a wall clock, only advances an input-clock counter inside Render().
//
// Implements IDevice so a Machine's DeviceManager owns it uniformly.
class Okim6295DeviceAdapter final : public IDevice {
public:
    static constexpr std::uint32_t kVoiceCount = 4;

    // okim6295_device::pin7_state (see the sample-rate note above).
    enum class Pin7 { Low, High };

    static constexpr std::uint32_t kDivisorPin7Low = 165;
    static constexpr std::uint32_t kDivisorPin7High = 132;

    // `rom` is the chip's external sample ROM (phrase table + raw 4-bit ADPCM data), used as-is; an out-of-range
    // read (a malformed/truncated resource) returns 0 instead of crashing the Machine.
    explicit Okim6295DeviceAdapter(std::vector<std::uint8_t> rom, Pin7 pin7 = Pin7::High);

    // Power-on-equivalent reset, matching okim6295_device::device_reset() including its asymmetry: only the four
    // voices' `playing` flag is cleared. A phrase-number byte already latched by a prior Write() (the first half of
    // an incomplete 2-byte sequence) is NOT cleared.
    void Reset();

    // Status register read: bits 4-7 always 1 (MAME: "naname expects bits 4-7 to be 1"), bit v set while voice v
    // is playing.
    std::uint8_t Read() const;

    // Command register write (protocol: see the class comment).
    void Write(std::uint8_t command);

    // Advances the chip by `inputClocks` input clocks and appends each of the kVoiceCount voices' mono float
    // sample (divided by the volume table's 0x20 full scale and the ADPCM signal's 2048 full scale, so each sample
    // is in [-1, 1)) to `voiceSamples`. Partial samples carry to the next call. Returns the number of output
    // samples appended to each buffer this call.
    std::size_t Render(std::uint64_t inputClocks, std::vector<std::vector<float>>& voiceSamples);

    // Input clocks per output sample for this instance's pin 7 configuration (132 or 165); the owning Machine
    // needs it for both its `ClockConverter` and `LogicalChannel::NativeSampleRate`.
    std::uint32_t Divisor() const { return pin7_ == Pin7::High ? kDivisorPin7High : kDivisorPin7Low; }

    // Test-only observation points - not part of production playback.
    bool PlayingForTest(std::uint32_t voice) const { return voices_[voice].playing; }
    std::uint32_t SampleIndexForTest(std::uint32_t voice) const { return voices_[voice].sample_index; }
    std::uint32_t SampleCountForTest(std::uint32_t voice) const { return voices_[voice].sample_count; }
    std::int32_t SignalForTest(std::uint32_t voice) const { return voices_[voice].signal; }
    std::int32_t StepForTest(std::uint32_t voice) const { return voices_[voice].step; }

private:
    struct Voice {
        bool playing = false;
        std::uint32_t base_offset = 0;
        std::uint32_t sample_index = 0;
        std::uint32_t sample_count = 0;
        std::int32_t signal = 0;  // matches oki_adpcm_state::m_signal, range [-2048, 2047]
        std::int32_t step = 0;    // matches oki_adpcm_state::m_step, range [0, 48]
        float volume = 0.0f;      // matches the 16-entry volume table, already normalized to [0, 1]
    };

    std::uint8_t ReadRom(std::uint32_t address) const;
    std::int32_t DecodeNibble(Voice& voice, std::uint8_t nibble) const;

    std::vector<std::uint8_t> rom_;
    Pin7 pin7_;
    Voice voices_[kVoiceCount]{};
    int pending_phrase_ = -1;  // matches m_command: -1 = idle, 0-127 = first half of a 2-byte command received

    std::int32_t diff_lookup_[49 * 16] = {};  // matches oki_adpcm_state::s_diff_lookup, computed once at construction
    std::uint64_t partial_input_clocks_ = 0;
};
