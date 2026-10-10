// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "sample_rate_converter.h"

#include <cstdint>
#include <soxr.h>
#include <vector>

// SampleRateConverter implemented on libsoxr (LGPL-2.1-or-later, dynamically
// linked; see THIRD-PARTY-NOTICES.md).
//
// soxr_process() is a streaming API and does not generally produce exactly the
// requested output count, while Process() must return exactly outputCount
// samples per call. Surplus output is therefore buffered in fifo_ and carried
// to the next call; the consumed prefix is compacted periodically.
class SoxrSampleRateConverter final : public SampleRateConverter {
public:
    // `leadingSilenceSamples`: output-rate samples of silence pre-loaded before
    // any real audio, delaying this channel's content uniformly. Used to equalize
    // total pipeline delay against a channel with a larger group delay. 0 means
    // no compensation.
    explicit SoxrSampleRateConverter(std::uint32_t inputRateHz, std::uint32_t outputRateHz,
                                      std::size_t leadingSilenceSamples = 0);
    ~SoxrSampleRateConverter() override;

    void Process(const float* input, std::size_t inputCount, float* output, std::size_t outputCount) override;
    void Reset() override;
    std::uint32_t InputRateHz() const override { return input_rate_hz_; }
    std::uint32_t OutputRateHz() const override { return output_rate_hz_; }

    // Measured by feeding calibration data through Process(), so it consumes this
    // instance's state. Only call it on a throwaway instance (without leading
    // silence) made solely to measure delay.
    std::size_t SteadyStateGroupDelaySamples() override;

private:
    soxr_t resampler_ = nullptr;
    std::uint32_t input_rate_hz_;
    std::uint32_t output_rate_hz_;
    std::vector<float> temp_buffer_;  // reused soxr_process() output scratch buffer
    std::vector<float> fifo_;         // samples soxr_process() has produced but Process() hasn't handed out yet
    std::size_t fifo_offset_ = 0;     // next unread position in fifo_
};
