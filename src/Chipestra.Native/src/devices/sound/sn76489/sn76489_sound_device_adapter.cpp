// license:BSD-3-Clause
// copyright-holders:Nicola Salmoria
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/sound/sn76496.cpp  [copyright-holders:Nicola Salmoria]
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#include "devices/sound/sn76489/sn76489_sound_device_adapter.h"

namespace {

// The Sega variant's constructor arguments in the original.
constexpr int kFeedbackMask = 0x8000;
constexpr int kWhiteNoiseTap1 = 0x01;
constexpr int kWhiteNoiseTap2 = 0x08;
constexpr int kClockDivider = 8;
constexpr int kMaxOutput = 0x7fff;

}  // namespace

Sn76489SoundDeviceAdapter::Sn76489SoundDeviceAdapter() { Reset(); }

void Sn76489SoundDeviceAdapter::Reset() {
    last_register_ = 3;
    for (int i = 0; i < 8; i += 2) {
        register_[i] = 0;
        register_[i + 1] = 0xf;
    }
    for (int i = 0; i < 4; ++i) {
        output_[i] = 0;
        count_[i] = 0;
        period_[i] = 0;
    }
    rng_ = kFeedbackMask;
    output_[3] = rng_ & 1;
    current_clock_ = kClockDivider - 1;

    // Volume table, 2 dB per step; four channels share the output range.
    double out = kMaxOutput / 4;
    for (int i = 0; i < 15; ++i) {
        vol_table_[i] = out > kMaxOutput / 4 ? kMaxOutput / 4 : static_cast<int>(out);
        out /= 1.258925412;
    }
    vol_table_[15] = 0;
    for (int i = 0; i < 4; ++i) {
        volume_[i] = vol_table_[register_[i * 2 + 1]];
    }
    partial_clocks_ = 0;
    generated_samples_ = 0;
}

void Sn76489SoundDeviceAdapter::Write(std::uint8_t data) {
    int r;
    if (data & 0x80) {
        r = (data & 0x70) >> 4;
        last_register_ = r;
        register_[r] = (register_[r] & 0x3f0) | (data & 0x0f);
    } else {
        r = last_register_;
    }

    const int c = r >> 1;
    switch (r) {
    case 0:
    case 2:
    case 4:
        if ((data & 0x80) == 0) {
            register_[r] = (register_[r] & 0x0f) | ((data & 0x3f) << 4);
        }
        period_[c] = register_[r];  // the Sega chips take a frequency of 0 as 0
        if (r == 4) {
            // The noise shift rate follows tone 3 when so selected.
            if ((register_[6] & 0x03) == 0x03) {
                period_[3] = period_[2] << 1;
            }
        }
        break;
    case 1:
    case 3:
    case 5:
    case 7:
        volume_[c] = vol_table_[data & 0x0f];
        if ((data & 0x80) == 0) {
            register_[r] = (register_[r] & 0x3f0) | (data & 0x0f);
        }
        break;
    case 6: {
        if ((data & 0x80) == 0) {
            register_[r] = (register_[r] & 0x3f0) | (data & 0x0f);
        }
        const int n = register_[6];
        period_[3] = ((n & 3) == 3) ? (period_[2] << 1) : (1 << (5 + (n & 3)));
        rng_ = kFeedbackMask;
        break;
    }
    default:
        break;
    }
}

void Sn76489SoundDeviceAdapter::Step() {
    if (current_clock_ > 0) {
        --current_clock_;
        return;
    }
    current_clock_ = kClockDivider - 1;

    for (int i = 0; i < 3; ++i) {
        --count_[i];
        if (count_[i] <= 0) {
            output_[i] ^= 1;
            count_[i] = period_[i];
        }
    }

    --count_[3];
    if (count_[3] <= 0) {
        // In white-noise mode both taps feed back; in periodic mode the lower tap is held at 0.
        const bool bit_a = (rng_ & kWhiteNoiseTap1) != 0;
        const bool bit_b = ((rng_ & kWhiteNoiseTap2) != 0) && ((register_[6] & 0x04) != 0);
        if (bit_a != bit_b) {
            rng_ >>= 1;
            rng_ |= kFeedbackMask;
        } else {
            rng_ >>= 1;
        }
        output_[3] = rng_ & 1;
        count_[3] = period_[3];
    }
}

std::size_t Sn76489SoundDeviceAdapter::Render(std::uint64_t inputClocks,
                                              std::vector<std::vector<float>>& channelSamples) {
    const std::uint64_t available = partial_clocks_ + inputClocks;
    const std::size_t sampleCount = static_cast<std::size_t>(available / kInputClocksPerSample);
    partial_clocks_ = available % kInputClocksPerSample;
    for (std::size_t i = 0; i < sampleCount; ++i) {
        Step();
        for (std::size_t c = 0; c < kChannelCount; ++c) {
            const int v = output_[c] != 0 ? volume_[c] : 0;
            channelSamples[c].push_back(-static_cast<float>(v) / 32768.0f);
        }
        ++generated_samples_;
    }
    return sampleCount;
}
