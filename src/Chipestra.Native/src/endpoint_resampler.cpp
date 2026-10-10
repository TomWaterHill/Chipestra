// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "endpoint_resampler.h"

#include "soxr_sample_rate_converter.h"

EndpointResampler::EndpointResampler(std::uint32_t endpointRateHz)
    : endpoint_rate_hz_(endpointRateHz),
      passthrough_(endpointRateHz == 0 || endpointRateHz == Mixer::kCanonicalSampleRateHz) {
    if (!passthrough_) {
        left_ = std::make_unique<SoxrSampleRateConverter>(Mixer::kCanonicalSampleRateHz, endpointRateHz);
        right_ = std::make_unique<SoxrSampleRateConverter>(Mixer::kCanonicalSampleRateHz, endpointRateHz);
    }
}

const std::vector<StereoFrame>& EndpointResampler::ProcessBlock(const StereoFrame* in, std::size_t inFrames) {
    if (passthrough_) {
        out_.assign(in, in + inFrames);
        return out_;
    }

    // Output frame count for this block, carrying the sub-frame remainder so
    // the average rate is exactly endpoint_rate / canonical_rate.
    frame_accum_ += static_cast<double>(inFrames) * static_cast<double>(endpoint_rate_hz_) /
                    static_cast<double>(Mixer::kCanonicalSampleRateHz);
    const auto outFrames = static_cast<std::size_t>(frame_accum_);
    frame_accum_ -= static_cast<double>(outFrames);

    in_left_.resize(inFrames);
    in_right_.resize(inFrames);
    for (std::size_t i = 0; i < inFrames; ++i) {
        in_left_[i] = in[i].left;
        in_right_[i] = in[i].right;
    }
    out_left_.assign(outFrames, 0.0f);
    out_right_.assign(outFrames, 0.0f);
    left_->Process(in_left_.data(), inFrames, out_left_.data(), outFrames);
    right_->Process(in_right_.data(), inFrames, out_right_.data(), outFrames);

    out_.resize(outFrames);
    for (std::size_t i = 0; i < outFrames; ++i) {
        out_[i].left = out_left_[i];
        out_[i].right = out_right_[i];
    }
    return out_;
}

void EndpointResampler::Reset() {
    frame_accum_ = 0.0;
    if (left_ != nullptr) {
        left_->Reset();
        right_->Reset();
    }
}
