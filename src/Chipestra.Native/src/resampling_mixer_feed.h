// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "sample_rate_converter.h"
#include "chipestra_abi.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

// ResamplingMixerFeed - the pipeline stage between a Machine's native-rate
// Render() output and the Mixer. A Machine writes each LogicalChannel's samples
// into a LogicalChannelSink at that channel's own native rate
// (LogicalChannel::NativeSampleRate); this stage resamples every channel to the
// canonical mix rate (Mixer::kCanonicalSampleRateHz) so the Mixer only ever
// sees one rate.
//
// It owns one SampleRateConverter per channel and keeps that state across
// blocks, so block boundaries stay continuous. A channel whose NativeSampleRate
// is 0 or already equal to the canonical rate is passed through unchanged
// (still trimmed/zero-padded to exactly the requested frame count).
class ResamplingMixerFeed {
public:
    // Factory for the converter used per channel. Defaults to
    // SoxrSampleRateConverter; injectable so a different resampler can be
    // dropped in (or a test can substitute one) without changing this class.
    // `leadingSilenceSamples` is the group-delay compensation: output-rate samples
    // of silence the constructed converter should emit before any real content,
    // so its content is delayed to match the channel with the largest group
    // delay. A converter type that doesn't support this can ignore it.
    using ConverterFactory = std::function<std::unique_ptr<SampleRateConverter>(
        std::uint32_t inRate, std::uint32_t outRate, std::size_t leadingSilenceSamples)>;

    explicit ResamplingMixerFeed(ConverterFactory factory = {});

    // Resamples every channel in `channels` from its NativeSampleRate to the
    // canonical rate and returns a sink holding exactly `canonicalFrameCount`
    // samples per channel. The returned reference is owned by this object and
    // stays valid until the next Resample()/Reset() call.
    const LogicalChannelSink& Resample(const LogicalChannelSink& nativeSink,
                                       const std::vector<LogicalChannel>& channels,
                                       std::size_t canonicalFrameCount);

    // Drops every channel's converter entirely (not just its phase) - call when
    // playback (re)starts or the Machine is reset, so the next block begins a
    // fresh stream. Converters are dropped rather than reset because the
    // group-delay compensation is pre-loaded only at construction; the next use
    // must go back through the factory to have it re-applied.
    void Reset();

private:
    SampleRateConverter& ConverterFor(ChannelId id, std::uint32_t nativeRate, std::size_t leadingSilenceSamples);

    // Per-native-rate steady-state group delay, memoized because measuring it
    // feeds a calibration signal through a throwaway converter. Caching by rate
    // alone (not per channel) lets channels with the same rate share the result.
    std::size_t GroupDelaySamplesFor(std::uint32_t nativeRate);

    ConverterFactory factory_;
    LogicalChannelSink canonical_;
    std::unordered_map<ChannelId, std::unique_ptr<SampleRateConverter>> converters_;
    std::unordered_map<std::uint32_t, std::size_t> delay_samples_by_native_rate_;
    std::vector<float> scratch_;
};
