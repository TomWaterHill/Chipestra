// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "soxr_sample_rate_converter.h"

#include "mixer.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

SoxrSampleRateConverter::SoxrSampleRateConverter(std::uint32_t inputRateHz, std::uint32_t outputRateHz,
                                                   std::size_t leadingSilenceSamples)
    : input_rate_hz_(inputRateHz), output_rate_hz_(outputRateHz) {
    soxr_io_spec_t io_spec = soxr_io_spec(SOXR_FLOAT32_I, SOXR_FLOAT32_I);
    soxr_quality_spec_t quality_spec = soxr_quality_spec(SOXR_HQ, 0);
    // Single-threaded so the result never depends on the environment
    // (playback must stay deterministic).
    soxr_runtime_spec_t runtime_spec = soxr_runtime_spec(1);
    // A smaller minimum DFT block (256 samples) cuts the start-up latency of a
    // fresh converter, which otherwise fades in over a few hundred ms. Filter
    // quality is unaffected.
    runtime_spec.log2_min_dft_size = 8;

    soxr_error_t error = nullptr;
    resampler_ = soxr_create(static_cast<double>(inputRateHz), static_cast<double>(outputRateHz),
                              1,  // mono - one SampleRateConverter per LogicalChannel
                              &error, &io_spec, &quality_spec, &runtime_spec);

    if (resampler_ == nullptr) {
        throw std::runtime_error(std::string("soxr_create: ") + (error ? error : "unknown error"));
    }

    // Pre-load leading silence; Process() drains it like carried-over output.
    if (leadingSilenceSamples > 0) {
        fifo_.assign(leadingSilenceSamples, 0.0f);
    }
}

SoxrSampleRateConverter::~SoxrSampleRateConverter() {
    if (resampler_) {
        soxr_delete(resampler_);
    }
}

void SoxrSampleRateConverter::Process(const float* input, std::size_t inputCount, float* output,
                                       std::size_t outputCount) {
    // Always forward the input to soxr first: its output is bursty, so a full
    // fifo_ says nothing about whether this block's input has reached soxr.
    // Then drain the front of fifo_.
    std::size_t input_consumed = 0;
    // Generous floor: soxr may hold more pending output than a tight buffer
    // fits, which would show up as occasional shortfalls.
    const std::size_t buffer_size = std::max({inputCount, outputCount, static_cast<std::size_t>(8192)});
    if (temp_buffer_.size() < buffer_size) {
        temp_buffer_.resize(buffer_size);
    }

    while (input_consumed < inputCount) {
        std::size_t idone = 0;
        std::size_t odone = 0;
        const soxr_error_t error =
            soxr_process(resampler_, input + input_consumed, inputCount - input_consumed, &idone,
                         temp_buffer_.data(), temp_buffer_.size(), &odone);
        if (error) {
            throw std::runtime_error(std::string("soxr_process: ") + error);
        }

        if (odone > 0) {
            fifo_.insert(fifo_.end(), temp_buffer_.begin(), temp_buffer_.begin() + static_cast<std::ptrdiff_t>(odone));
        }
        input_consumed += idone;

        if (idone == 0 && odone == 0) {
            break;  // no progress this call - avoid spinning forever (defensive; not expected to trigger)
        }
    }

    // Drain up to outputCount from fifo_. A shortfall is expected only as a
    // one-time start-up transient and is padded with silence.
    const std::size_t available = fifo_.size() - fifo_offset_;
    const std::size_t copy_count = std::min(available, outputCount);
    std::copy(fifo_.begin() + static_cast<std::ptrdiff_t>(fifo_offset_),
              fifo_.begin() + static_cast<std::ptrdiff_t>(fifo_offset_ + copy_count), output);
    fifo_offset_ += copy_count;

    if (copy_count < outputCount) {
        std::fill(output + copy_count, output + outputCount, 0.0f);
    }

    // Compact the consumed prefix only when it is a substantial part of the
    // buffer, so the copy stays occasional.
    if (fifo_offset_ == fifo_.size()) {
        fifo_.clear();
        fifo_offset_ = 0;
    } else if (fifo_offset_ > 4096 && fifo_offset_ > fifo_.size() / 2) {
        fifo_.erase(fifo_.begin(), fifo_.begin() + static_cast<std::ptrdiff_t>(fifo_offset_));
        fifo_offset_ = 0;
    }
}

void SoxrSampleRateConverter::Reset() {
    const soxr_error_t error = soxr_clear(resampler_);
    if (error) {
        throw std::runtime_error(std::string("soxr_clear: ") + error);
    }
    // Also drops the leading-silence padding. ResamplingMixerFeed discards
    // converters instead of resetting them so fresh ones re-apply the
    // compensation.
    fifo_.clear();
    fifo_offset_ = 0;
}

std::size_t SoxrSampleRateConverter::SteadyStateGroupDelaySamples() {
    // Measured directly from a silence->step onset rather than soxr_delay(),
    // which disagrees with the real onset lag at extreme ratios (e.g. 24x
    // upsampling). The feed shape matches real playback. Lead-in and
    // observation window are each 3 seconds of blocks, enough calls to converge
    // at every native rate.
    const std::size_t kPhaseBlocks = static_cast<std::size_t>(
        std::llround(3.0 * static_cast<double>(output_rate_hz_) / static_cast<double>(Mixer::kBlockSizeFrames)));

    // Feed native samples per block by tracking the exact cumulative target
    // (Bresenham-style carry); a fixed rounded chunk would accumulate a rounding
    // backlog and swamp the measurement.
    // Drives Process() with exactly one block of output per call, like real
    // channels, so the measurement matches the real path.
    auto feedBlocks = [&](std::size_t numBlocks, float value, std::vector<float>* collectInto) {
        std::vector<float> inputBuf;
        std::vector<float> outputBuf(Mixer::kBlockSizeFrames);
        std::uint64_t totalFedSoFar = 0;
        for (std::size_t block = 0; block < numBlocks; ++block) {
            const std::uint64_t targetTotal = static_cast<std::uint64_t>(std::llround(
                static_cast<double>(block + 1) * static_cast<double>(Mixer::kBlockSizeFrames) *
                static_cast<double>(input_rate_hz_) / static_cast<double>(output_rate_hz_)));
            const std::size_t thisBlockCount = static_cast<std::size_t>(targetTotal - totalFedSoFar);
            totalFedSoFar = targetTotal;
            inputBuf.assign(thisBlockCount, value);

            Process(thisBlockCount > 0 ? inputBuf.data() : nullptr, thisBlockCount, outputBuf.data(),
                    outputBuf.size());
            if (collectInto != nullptr) {
                collectInto->insert(collectInto->end(), outputBuf.begin(), outputBuf.end());
            }
        }
    };

    feedBlocks(kPhaseBlocks, 0.0f, nullptr);
    std::vector<float> observed;
    feedBlocks(kPhaseBlocks, 1.0f, &observed);

    // First index where the step has clearly arrived.
    for (std::size_t i = 0; i < observed.size(); ++i) {
        if (observed[i] > 0.5f) {
            return i;
        }
    }
    return observed.size();  // never crossed - treat as "delay covers the whole observed window"
}
