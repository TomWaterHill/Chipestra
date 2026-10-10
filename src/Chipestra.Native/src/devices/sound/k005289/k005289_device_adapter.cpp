// license:BSD-3-Clause
// copyright-holders:Bryan McPhail
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/sound/k005289.cpp
//   src/devices/sound/k005289.h
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#include "k005289_device_adapter.h"

#include <algorithm>

K005289DeviceAdapter::K005289DeviceAdapter(std::vector<std::uint8_t> waveformProm) {
    waveform_prom_.assign(kWaveformPromTotalBytes, 0);
    const std::size_t copyCount = std::min(waveformProm.size(), kWaveformPromTotalBytes);
    std::copy_n(waveformProm.begin(), copyCount, waveform_prom_.begin());
}

void K005289DeviceAdapter::Reset() {
    for (Voice& v : voice_) {
        v = Voice{};
    }
    partial_input_clocks_ = 0;
}

void K005289DeviceAdapter::ControlA(std::uint8_t data) {
    voice_[0].volume = data & 0x0Fu;
    voice_[0].waveform = static_cast<std::uint16_t>((voice_[0].waveform & ~0xE0u) | (data & 0xE0u));
}

void K005289DeviceAdapter::ControlB(std::uint8_t data) {
    voice_[1].volume = data & 0x0Fu;
    voice_[1].waveform = static_cast<std::uint16_t>((voice_[1].waveform & ~0xE0u) | (data & 0xE0u));
}

void K005289DeviceAdapter::Ld1(std::uint16_t offset, std::uint8_t) {
    voice_[0].pitch = static_cast<std::uint16_t>(0x0FFFu - offset);
}

void K005289DeviceAdapter::Ld2(std::uint16_t offset, std::uint8_t) {
    voice_[1].pitch = static_cast<std::uint16_t>(0x0FFFu - offset);
}

void K005289DeviceAdapter::Tg1(std::uint8_t) {
    voice_[0].frequency = voice_[0].pitch;
}

void K005289DeviceAdapter::Tg2(std::uint8_t) {
    voice_[1].frequency = voice_[1].pitch;
}

void K005289DeviceAdapter::TickOneSample() {
    for (Voice& v : voice_) {
        // s16 counter as in MAME's voice_t; the signed `--counter < 0`
        // comparison matters: frequency 0 makes the counter go 0 -> -1 every
        // sample, so the waveform address advances every sample (fastest rate).
        if (--v.counter < 0) {
            v.waveform = static_cast<std::uint16_t>((v.waveform & ~0x1Fu) | ((v.waveform + 1) & 0x1Fu));
            v.counter = static_cast<std::int16_t>(v.frequency);
        }
    }
}

std::size_t K005289DeviceAdapter::Render(std::uint64_t inputClocks, std::vector<std::vector<float>>& voiceSamples) {
    partial_input_clocks_ += inputClocks;
    const std::uint64_t sampleCount = partial_input_clocks_ / kInputClocksPerSample;
    partial_input_clocks_ -= sampleCount * kInputClocksPerSample;

    for (std::uint64_t i = 0; i < sampleCount; ++i) {
        TickOneSample();
        for (std::uint32_t voice = 0; voice < kVoiceCount; ++voice) {
            const Voice& v = voice_[voice];
            // `waveform` spans the full 0-255 byte (low 5 bits from the
            // counter above, high 3 bits from ControlA/ControlB), so this
            // indexes into this voice's 256-byte PROM half.
            const std::size_t promIndex = (static_cast<std::size_t>(voice) << 8) | v.waveform;
            const std::uint8_t nibble =
                promIndex < waveform_prom_.size() ? static_cast<std::uint8_t>(waveform_prom_[promIndex] & 0x0Fu) : 0;
            // MAME: `((prom & 0xf) - 8) * volume` scaled by 1/512 (max +/-0.234); not
            // normalised to +/-1.0, which would overstate the chip by 12.6 dB.
            voiceSamples[voice].push_back((static_cast<float>(nibble) - 8.0f) * static_cast<float>(v.volume) / 512.0f);
        }
    }
    return static_cast<std::size_t>(sampleCount);
}
