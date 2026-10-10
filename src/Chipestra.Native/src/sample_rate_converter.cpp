// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "sample_rate_converter.h"

#include <cmath>
#include <stdexcept>

LinearSampleRateConverter::LinearSampleRateConverter(std::uint32_t inputRateHz, std::uint32_t outputRateHz)
    : input_rate_hz_(inputRateHz), output_rate_hz_(outputRateHz) {
    if (inputRateHz == 0 || outputRateHz == 0) {
        throw std::invalid_argument("LinearSampleRateConverter: rates must be non-zero");
    }
    step_ = static_cast<double>(inputRateHz) / static_cast<double>(outputRateHz);

    // Anti-aliasing pre-filter, needed only when downsampling. The cutoff is
    // 0.45 * outputRateHz (90% of the output Nyquist) so the gentle 2-pole
    // rolloff already attenuates at Nyquist; it is independent of the ratio.
    anti_alias_enabled_ = step_ > 1.0;
    if (anti_alias_enabled_) {
        constexpr double kPi = 3.14159265358979323846;
        constexpr double kNyquistFraction = 0.45;
        const double cutoffHz = kNyquistFraction * static_cast<double>(outputRateHz);
        const double dt = 1.0 / static_cast<double>(inputRateHz);
        anti_alias_alpha_ = 1.0 - std::exp(-2.0 * kPi * cutoffHz * dt);
    } else {
        anti_alias_alpha_ = 1.0;  // unused (Process() skips the filter block entirely)
    }

    Reset();
}

void LinearSampleRateConverter::Reset() {
    // Start the cursor at `history_`, one sample behind the newest input, so
    // every interpolation has both endpoints and block boundaries are
    // click-free. On the first block `primed_` routes index <= 0 to input[0].
    position_ = 0.0;
    history_ = 0.0f;
    primed_ = false;
    anti_alias_state1_ = 0.0f;
    anti_alias_state2_ = 0.0f;
}

void LinearSampleRateConverter::Process(const float* input, std::size_t inputCount, float* output,
                                        std::size_t outputCount) {
    // Anti-aliasing pre-filter at the input rate, before interpolation;
    // skipped when upsampling.
    const float* src = input;
    if (anti_alias_enabled_ && inputCount > 0) {
        filtered_.resize(inputCount);
        // 2 cascaded one-pole low-pass stages (y[n] = y[n-1] + alpha*(x[n]-
        // y[n-1])), each state carried across Process() calls the same way
        // history_/position_ already are, for click-free block boundaries.
        float s1 = anti_alias_state1_;
        float s2 = anti_alias_state2_;
        const float alpha = static_cast<float>(anti_alias_alpha_);
        for (std::size_t i = 0; i < inputCount; ++i) {
            s1 += alpha * (input[i] - s1);
            s2 += alpha * (s1 - s2);
            filtered_[i] = s2;
        }
        anti_alias_state1_ = s1;
        anti_alias_state2_ = s2;
        src = filtered_.data();
    }

    // Virtual stream index: 0 -> history_ (previous block's last sample),
    // 1 -> src[0], k -> src[k-1]. Past the end we hold the last sample
    // (we don't have the next block yet); linear interpolation's error from
    // that is at most one sample per block boundary.
    const auto sample_at = [&](long long idx) -> float {
        if (idx <= 0) {
            return primed_ ? history_ : (inputCount > 0 ? src[0] : 0.0f);
        }
        if (static_cast<std::size_t>(idx) > inputCount) {
            return inputCount > 0 ? src[inputCount - 1] : (primed_ ? history_ : 0.0f);
        }
        return src[idx - 1];
    };

    for (std::size_t k = 0; k < outputCount; ++k) {
        const long long i0 = static_cast<long long>(std::floor(position_));
        const double frac = position_ - static_cast<double>(i0);
        const float s0 = sample_at(i0);
        const float s1 = sample_at(i0 + 1);
        output[k] = static_cast<float>((1.0 - frac) * static_cast<double>(s0) +
                                       frac * static_cast<double>(s1));
        position_ += step_;
    }

    // Slide the window forward past the samples this block supplied. Any
    // fractional remainder carries so the next block continues in phase. If the
    // input samples than outputCount*step_ implied, position_ would go
    // negative here - clamp it; the mismatch is expected to be well under one
    // sample and self-corrects as the producer's own fractional carry
    // catches up.
    position_ -= static_cast<double>(inputCount);
    if (position_ < 0.0) {
        position_ = 0.0;
    }
    if (inputCount > 0) {
        history_ = src[inputCount - 1];
        primed_ = true;
    }
}
