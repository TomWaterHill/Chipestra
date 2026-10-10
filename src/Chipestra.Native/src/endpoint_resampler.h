// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "mixer.h"
#include "sample_rate_converter.h"

#include <cstdint>
#include <memory>
#include <vector>

// EndpointResampler - the single place that converts the canonical 192kHz
// stereo mix to a real audio endpoint's sample rate. Used by
// WasapiSharedOutput so the "192kHz -> endpoint" conversion is not
// re-implemented per call site.
//
// Stateful: keeps one SampleRateConverter (libsoxr-backed) per channel plus a
// fractional output-frame accumulator across ProcessBlock() calls, so block
// boundaries join without a discontinuity and the long-run output rate is
// exact (e.g. 256 canonical frames in -> 58 or 59 out for a 44.1kHz endpoint).
// Not thread-safe: the one PlaybackPump thread owns it.
class EndpointResampler {
public:
    explicit EndpointResampler(std::uint32_t endpointRateHz);

    // True when the endpoint already runs at the canonical rate: ProcessBlock()
    // is then an identity copy.
    bool IsPassthrough() const { return passthrough_; }

    std::uint32_t EndpointRateHz() const { return endpoint_rate_hz_; }

    // Resamples `in` (canonical-rate interleaved stereo) and returns a
    // reference to an internal buffer of endpoint-rate interleaved stereo.
    // The reference is valid until the next ProcessBlock()/Reset() call.
    const std::vector<StereoFrame>& ProcessBlock(const StereoFrame* in, std::size_t inFrames);

    // Drops the converter phase and the fractional accumulator - call when
    // playback (re)starts.
    void Reset();

private:
    std::uint32_t endpoint_rate_hz_;
    bool passthrough_;
    std::unique_ptr<SampleRateConverter> left_;
    std::unique_ptr<SampleRateConverter> right_;
    double frame_accum_ = 0.0;
    std::vector<float> in_left_, in_right_, out_left_, out_right_;
    std::vector<StereoFrame> out_;
};
