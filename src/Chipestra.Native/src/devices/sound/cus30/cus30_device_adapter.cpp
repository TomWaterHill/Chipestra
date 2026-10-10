// license:BSD-3-Clause
// copyright-holders:Nicola Salmoria,Aaron Giles
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/sound/namco.cpp
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#include "cus30_device_adapter.h"

Cus30DeviceAdapter::Cus30DeviceAdapter() { Reset(); }

void Cus30DeviceAdapter::Reset() {
    waveRam_.fill(0);
    registers_.fill(0);
    sharedRam_.fill(0);
    for (Voice& voice : voices_) {
        voice = Voice{};
        voice.noiseSeed = 1;  // non-zero LFSR seed, as in MAME
    }
}

std::uint8_t Cus30DeviceAdapter::Read8(std::uint16_t offset) const {
    if (offset < kWaveRamSize) {
        return waveRam_[offset];
    }
    if (offset < kWaveRamSize + kRegisterBlockSize) {
        const std::uint16_t reg = static_cast<std::uint16_t>(offset - kWaveRamSize);
        const std::uint32_t ch = reg >> 3;
        // Register+5 returns the live waveform position (as in MAME's cus30_r). The Machine's
        // bus-access-granularity sync has already brought the counter up to the current cycle.
        if ((reg & 7) == 5 && ch < kVoiceCount) {
            return static_cast<std::uint8_t>((voices_[ch].counter >> 15) & 0x1F);
        }
        return registers_[reg];
    }
    if (offset < kAddressSpaceSize) {
        return sharedRam_[offset - (kWaveRamSize + kRegisterBlockSize)];
    }
    return 0;
}

void Cus30DeviceAdapter::Write8(std::uint16_t offset, std::uint8_t value) {
    if (offset < kWaveRamSize) {
        waveRam_[offset] = value;
        return;
    }
    if (offset < kWaveRamSize + kRegisterBlockSize) {
        const std::uint16_t reg = static_cast<std::uint16_t>(offset - kWaveRamSize);
        // Skip if unchanged, as in MAME's cus30_w; this gates the side effects below (frequency
        // recompute, the cross-voice noise_sw write).
        if (registers_[reg] == value) return;
        registers_[reg] = value;

        const std::uint32_t ch = reg >> 3;
        Voice& voice = voices_[ch];
        // Per-voice register map:
        //   +0x00 left volume | +0x01 waveform-select(hi nibble)+freq bits[19:16](lo nibble)
        //   +0x02 freq[15:8]  | +0x03 freq[7:0] | +0x04 right volume + noise_sw for voice (ch+1)&7
        switch (reg & 7) {
            case 0x00:
                voice.volumeLeft = value & 0x0F;
                break;
            case 0x01:
                voice.waveformSelect = (value >> 4) & 0x0F;
                [[fallthrough]];
            case 0x02:
            case 0x03:
                voice.frequency = (static_cast<std::uint32_t>(registers_[ch * 8 + 0x01] & 0x0F) << 16) +
                                   (static_cast<std::uint32_t>(registers_[ch * 8 + 0x02]) << 8) +
                                   static_cast<std::uint32_t>(registers_[ch * 8 + 0x03]);
                break;
            case 0x04: {
                voice.volumeRight = value & 0x0F;
                // Cross-voice side effect (as in MAME): bit 7 of THIS voice's right-volume write
                // sets noise_sw on the NEXT voice, wrapping 7->0.
                voices_[(ch + 1) & 7].noiseSw = (value & 0x80) != 0;
                break;
            }
            default:
                break;  // 5/6/7: stored only, no per-voice effect (register 5's read side is in Read8)
        }
        return;
    }
    if (offset < kAddressSpaceSize) {
        sharedRam_[offset - (kWaveRamSize + kRegisterBlockSize)] = value;
    }
}

std::int32_t Cus30DeviceAdapter::WaveformSample(std::uint8_t select, std::uint32_t counter) const {
    const std::uint32_t position = (counter >> 15) & 0x1F;                              // 0..31
    const std::uint32_t pos = (static_cast<std::uint32_t>(select) << 5) + position;     // 0..511
    const std::uint8_t byteVal = waveRam_[(pos >> 1) & 0xFF];
    const bool highNibble = (pos & 1) == 0;  // as MAME's waveform_r: BIT(~pos,0)
    const std::uint8_t rawNibble = highNibble ? static_cast<std::uint8_t>(byteVal >> 4) : (byteVal & 0x0F);
    return static_cast<std::int32_t>(rawNibble) - 8;
}

void Cus30DeviceAdapter::AdvanceNoiseOneInnerTick(Voice& voice) const {
    if (voice.noiseHold > 0) {
        --voice.noiseHold;
        return;
    }
    voice.noiseHold = kNoiseHoldTime;
    const std::uint32_t f = voice.frequency & 0xFF;
    const std::uint32_t delta = f << 4;
    voice.noiseCounter += delta;
    int cnt = static_cast<int>(voice.noiseCounter >> 12);
    voice.noiseCounter &= 0x0FFF;
    for (; cnt > 0; --cnt) {
        if ((voice.noiseSeed + 1) & 2) voice.noiseState ^= 1;
        if (voice.noiseSeed & 1) voice.noiseSeed ^= 0x28000;
        voice.noiseSeed >>= 1;
    }
}

std::size_t Cus30DeviceAdapter::Render(std::uint64_t inputClocks, std::vector<std::vector<float>>& voiceSamples) {
    // Derived from MAME's `0x07*(vol>>1)` noise data per side, recovered via (lout+rout)/volSum;
    // only approximately constant (~3.5/8) because of the `>>1` truncation.
    constexpr float kNoiseMagnitude = 3.5f / 8.0f;

    voiceSamples.resize(kVoiceCount);
    for (std::uint32_t v = 0; v < kVoiceCount; ++v) {
        Voice& voice = voices_[v];
        std::vector<float>& out = voiceSamples[v];
        for (std::uint64_t i = 0; i < inputClocks; ++i) {
            const bool active = (voice.volumeLeft + voice.volumeRight) > 0;
            float sample = 0.0f;
            if (voice.noiseSw) {
                // The LFSR freezes while silent too (as in MAME).
                if (active) {
                    for (int t = 0; t < kNoiseInnerTicksPerOutputSample; ++t) {
                        AdvanceNoiseOneInnerTick(voice);
                    }
                    sample = voice.noiseState ? kNoiseMagnitude : -kNoiseMagnitude;
                }
            } else if (active) {
                const std::int32_t nibble = WaveformSample(voice.waveformSelect, voice.counter);
                voice.counter += voice.frequency;  // frozen while silent, matching real hardware
                sample = static_cast<float>(nibble) / 8.0f;
            }
            out.push_back(sample);
        }
    }
    return static_cast<std::size_t>(inputClocks);
}
