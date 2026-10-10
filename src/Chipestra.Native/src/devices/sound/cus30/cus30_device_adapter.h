// license:BSD-3-Clause
// copyright-holders:Nicola Salmoria,Aaron Giles
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/sound/namco.cpp
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#pragma once

#include "device_manager.h"

#include <array>
#include <cstdint>
#include <vector>

// Port of MAME's namco_cus30_device (see THIRD-PARTY-NOTICES.md). Real hardware: an 8-voice
// wavetable PSG whose waveform data is CPU-programmed internal RAM, not an external ROM, so no
// waveform ROM resource is needed.
//
// Address decode (0x000-0x3ff local offset, mapped 1:1 by the owning Machine):
//   0x000-0x0FF  waveform RAM (256 bytes, plain R/W)
//   0x100-0x13F  64 voice registers, 8 bytes/voice (layout: see Write8)
//   0x140-0x3FF  generic shared RAM (704 bytes, plain R/W, no audio side effect)
//
// Stereo: each voice has independent 4-bit left/right volume registers. Stereo is expressed as
// routing, not waveform: Render() outputs ONE mono, volume-independent waveform sample per voice
// (silent only when both volumes are 0), and the owning Machine derives LogicalChannel::Pan/
// RoutingGain from VoiceVolumeLeft/VoiceVolumeRight. This is exact for the chip's formula
// (lout=waveform*lv, rout=waveform*rv; (lout+rout)/(lv+rv) recovers `waveform`).
//
// Deterministic time model: never reads a wall clock. Renders at the chip's own native input
// clock (kInputClocksPerSample = 1), not MAME's 192kHz-oversampled internal scheme. At the native
// rate MAME's waveform position shift reduces to exactly `>>15`, so pitch is identical; the
// native rate is converted to the mix rate downstream like for every other adapter.
//
// Noise mode (per-voice `noise_sw`, set as a side effect of writing the NEXT voice's
// volume-right register): MAME's hold-time formula (`1 << (m_f_fracbits - 16)`) is undefined at
// the native-rate fracbits of 15. A voice in noise mode therefore runs
// kNoiseInnerTicksPerOutputSample (16, MAME's actual oversample factor for the 12,000Hz clock)
// internal-rate sub-ticks per output sample, which keeps the LFSR advance rate exact. The noise
// magnitude (kNoiseMagnitude) is a close but not bit-exact approximation of MAME's
// `0x07*(vol>>1)` per-channel formula.
class Cus30DeviceAdapter final : public IDevice {
public:
    static constexpr std::uint32_t kVoiceCount = 8;
    static constexpr std::size_t kWaveRamSize = 0x100;       // 256 bytes
    static constexpr std::size_t kRegisterBlockSize = 0x40;  // 64 bytes, 8/voice
    static constexpr std::size_t kSharedRamSize = 0x2C0;     // 0x400 - 0x140 = 704 bytes
    static constexpr std::size_t kAddressSpaceSize = kWaveRamSize + kRegisterBlockSize + kSharedRamSize;  // 0x400

    // One output sample per one CUS30 input clock (exact for the waveform path).
    static constexpr std::uint32_t kInputClocksPerSample = 1;

    Cus30DeviceAdapter();

    // Power-on-equivalent reset: zeroes waveform RAM, shared RAM, registers and every per-voice
    // field (the chip has no reset line on the board).
    void Reset();

    // Full 0x000-0x3FF local address decode. `offset` must be < kAddressSpaceSize.
    std::uint8_t Read8(std::uint16_t offset) const;
    void Write8(std::uint16_t offset, std::uint8_t value);

    // Advances every voice by `inputClocks` output samples (kInputClocksPerSample == 1, so this is also
    // the sample count) and appends each of the kVoiceCount voices' mono float sample to
    // `voiceSamples[voice]`. Returns the number of samples appended per voice.
    std::size_t Render(std::uint64_t inputClocks, std::vector<std::vector<float>>& voiceSamples);

    // Raw 4-bit volume registers, for the owning Machine's per-block Pan/RoutingGain derivation.
    std::uint8_t VoiceVolumeLeft(std::uint32_t voice) const { return voices_[voice].volumeLeft; }
    std::uint8_t VoiceVolumeRight(std::uint32_t voice) const { return voices_[voice].volumeRight; }

    // Test-only observation points.
    std::uint32_t FrequencyForTest(std::uint32_t voice) const { return voices_[voice].frequency; }
    std::uint32_t CounterForTest(std::uint32_t voice) const { return voices_[voice].counter; }
    std::uint8_t WaveformSelectForTest(std::uint32_t voice) const { return voices_[voice].waveformSelect; }
    bool NoiseSwForTest(std::uint32_t voice) const { return voices_[voice].noiseSw; }

private:
    struct Voice {
        std::uint32_t frequency = 0;  // 20-bit
        std::uint32_t counter = 0;
        std::uint8_t volumeLeft = 0;
        std::uint8_t volumeRight = 0;
        std::uint8_t waveformSelect = 0;
        bool noiseSw = false;
        // Noise LFSR state.
        int noiseState = 0;
        std::uint32_t noiseSeed = 1;
        std::uint32_t noiseCounter = 0;
        int noiseHold = 0;
    };

    // waveform_r()+namco_update_one() ported to the native-rate counter. Returns a signed nibble,
    // range -8..7.
    std::int32_t WaveformSample(std::uint8_t select, std::uint32_t counter) const;

    // One MAME-internal-rate (192kHz-equivalent) tick of the noise hold/accumulate/LFSR-shift logic
    // (the per-tick output is handled by the caller). kNoiseFracBits=19 (4 oversample doublings +
    // 15) fixes kNoiseHoldTime = 1 << (19-16) = 8.
    static constexpr int kNoiseFracBits = 19;
    static constexpr int kNoiseHoldTime = 1 << (kNoiseFracBits - 16);
    static constexpr int kNoiseInnerTicksPerOutputSample = 1 << (kNoiseFracBits - 15);  // 16
    void AdvanceNoiseOneInnerTick(Voice& voice) const;

    std::array<std::uint8_t, kWaveRamSize> waveRam_{};
    std::array<std::uint8_t, kRegisterBlockSize> registers_{};
    std::array<std::uint8_t, kSharedRamSize> sharedRam_{};
    std::array<Voice, kVoiceCount> voices_{};
};
