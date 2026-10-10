// license:BSD-3-Clause
// copyright-holders:Mirko Buffoni,Aaron Giles
// copyright-holders:Andrew Gardner,Aaron Giles
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/sound/okim6295.cpp  [copyright-holders:Mirko Buffoni,Aaron Giles]
//   src/devices/sound/okiadpcm.cpp  [copyright-holders:Andrew Gardner,Aaron Giles]
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#include "okim6295_device_adapter.h"

#include <algorithm>
#include <cmath>

namespace {

// Volume table (MAME's s_volume_table): 16 steps, ~3.2dB each for the first 9, silent beyond that, kept as MAME's
// raw hex numerators over the 0x20 full-scale denominator.
constexpr float kVolumeNumerator[16] = {0x20, 0x16, 0x10, 0x0b, 0x08, 0x06, 0x04, 0x03, 0x02, 0, 0, 0, 0, 0, 0, 0};

// oki_adpcm_state::s_index_shift (okiadpcm.cpp).
constexpr std::int8_t kIndexShift[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

// oki_adpcm_state::compute_tables()'s own nibble-to-signed-bits map.
constexpr std::int8_t kNibbleToBits[16][4] = {
    {1, 0, 0, 0},  {1, 0, 0, 1},  {1, 0, 1, 0},  {1, 0, 1, 1},  {1, 1, 0, 0},  {1, 1, 0, 1},  {1, 1, 1, 0},
    {1, 1, 1, 1},  {-1, 0, 0, 0}, {-1, 0, 0, 1}, {-1, 0, 1, 0}, {-1, 0, 1, 1}, {-1, 1, 0, 0}, {-1, 1, 0, 1},
    {-1, 1, 1, 0}, {-1, 1, 1, 1},
};

}  // namespace

Okim6295DeviceAdapter::Okim6295DeviceAdapter(std::vector<std::uint8_t> rom, Pin7 pin7)
    : rom_(std::move(rom)), pin7_(pin7) {
    // oki_adpcm_state::compute_tables(), computed once per instance (784 entries) rather than as a process-wide
    // static, so instances share no mutable state.
    for (int step = 0; step <= 48; ++step) {
        const int stepval = static_cast<int>(std::floor(16.0 * std::pow(11.0 / 10.0, static_cast<double>(step))));
        for (int nib = 0; nib < 16; ++nib) {
            const std::int8_t* bits = kNibbleToBits[nib];
            diff_lookup_[step * 16 + nib] =
                bits[0] * (stepval * bits[1] + stepval / 2 * bits[2] + stepval / 4 * bits[3] + stepval / 8);
        }
    }
}

void Okim6295DeviceAdapter::Reset() {
    // Only `playing` is cleared (see the header comment).
    for (Voice& voice : voices_) {
        voice.playing = false;
    }
}

std::uint8_t Okim6295DeviceAdapter::Read() const {
    std::uint8_t result = 0xf0;  // "naname expects bits 4-7 to be 1" (okim6295.cpp's own comment)
    for (std::uint32_t v = 0; v < kVoiceCount; ++v) {
        if (voices_[v].playing) {
            result |= static_cast<std::uint8_t>(1u << v);
        }
    }
    return result;
}

void Okim6295DeviceAdapter::Write(std::uint8_t command) {
    if (pending_phrase_ != -1) {
        // Second half: bits 4-7 select voice(s) (MAME: "not possible to start multiple voices at the same time"
        // per the manual, but the code itself loops over every set bit regardless - preserved here as-is), bits
        // 0-3 select the attenuation.
        int voicemask = command >> 4;
        const std::uint32_t base = static_cast<std::uint32_t>(pending_phrase_) * 8;
        const std::uint32_t start = ((static_cast<std::uint32_t>(ReadRom(base + 0)) << 16) |
                                     (static_cast<std::uint32_t>(ReadRom(base + 1)) << 8) | ReadRom(base + 2)) &
                                    0x3ffff;
        const std::uint32_t stop = ((static_cast<std::uint32_t>(ReadRom(base + 3)) << 16) |
                                    (static_cast<std::uint32_t>(ReadRom(base + 4)) << 8) | ReadRom(base + 5)) &
                                   0x3ffff;
        for (std::uint32_t v = 0; v < kVoiceCount; ++v, voicemask >>= 1) {
            if (!(voicemask & 1)) {
                continue;
            }
            Voice& voice = voices_[v];
            if (voice.playing) {
                continue;  // "Requested to play sample on non-stopped voice" (MAME: ignored, logged only)
            }
            if (start < stop) {
                voice.playing = true;
                voice.base_offset = start;
                voice.sample_index = 0;
                voice.sample_count = 2 * (stop - start + 1);
                voice.signal = 0;
                voice.step = 0;
                voice.volume = kVolumeNumerator[command & 0x0f] / 0x20;
            }
            // start >= stop: "Requested to play invalid sample" (MAME: ignored, logged only)
        }
        pending_phrase_ = -1;
    } else if (command & 0x80) {
        pending_phrase_ = command & 0x7f;
    } else {
        int voicemask = command >> 3;
        for (std::uint32_t v = 0; v < kVoiceCount; ++v, voicemask >>= 1) {
            if (voicemask & 1) {
                voices_[v].playing = false;
            }
        }
    }
}

std::uint8_t Okim6295DeviceAdapter::ReadRom(std::uint32_t address) const {
    return address < rom_.size() ? rom_[address] : 0;
}

std::int32_t Okim6295DeviceAdapter::DecodeNibble(Voice& voice, std::uint8_t nibble) const {
    voice.signal += diff_lookup_[voice.step * 16 + (nibble & 15)];
    voice.signal = std::clamp(voice.signal, -2048, 2047);
    voice.step = std::clamp(voice.step + kIndexShift[nibble & 7], 0, 48);
    return voice.signal;
}

std::size_t Okim6295DeviceAdapter::Render(std::uint64_t inputClocks, std::vector<std::vector<float>>& voiceSamples) {
    const std::uint32_t divisor = Divisor();
    const std::uint64_t available = partial_input_clocks_ + inputClocks;
    const std::size_t sampleCount = static_cast<std::size_t>(available / divisor);
    partial_input_clocks_ = available % divisor;

    for (std::size_t i = 0; i < sampleCount; ++i) {
        for (std::uint32_t v = 0; v < kVoiceCount; ++v) {
            Voice& voice = voices_[v];
            float out = 0.0f;
            if (voice.playing) {
                // okim_voice::generate_adpcm's own nibble selection: high nibble first, then low nibble.
                const std::uint8_t byte = ReadRom(voice.base_offset + voice.sample_index / 2);
                const int shift = ((voice.sample_index & 1) << 2) ^ 4;
                const std::uint8_t nibble = static_cast<std::uint8_t>(byte >> shift) & 0x0f;
                const std::int32_t decoded = DecodeNibble(voice, nibble);
                out = (static_cast<float>(decoded) * voice.volume) / 2048.0f;
                if (++voice.sample_index >= voice.sample_count) {
                    voice.playing = false;
                }
            }
            voiceSamples[v].push_back(out);
        }
    }
    return sampleCount;
}
