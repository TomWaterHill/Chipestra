// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "resampling_mixer_feed.h"

#include "mixer.h"
#include "soxr_sample_rate_converter.h"

#include <algorithm>
#include <utility>

// Default converter: SoxrSampleRateConverter (libsoxr, LGPL-2.1-or-later,
// dynamically linked; see THIRD-PARTY-NOTICES.md), which has real
// anti-aliasing when a chip's native rate is above the canonical 192kHz.
ResamplingMixerFeed::ResamplingMixerFeed(ConverterFactory factory)
    : factory_(factory ? std::move(factory)
                       : [](std::uint32_t inRate, std::uint32_t outRate,
                            std::size_t leadingSilenceSamples) -> std::unique_ptr<SampleRateConverter> {
                             return std::make_unique<SoxrSampleRateConverter>(inRate, outRate, leadingSilenceSamples);
                         }) {}

SampleRateConverter& ResamplingMixerFeed::ConverterFor(ChannelId id, std::uint32_t nativeRate,
                                                        std::size_t leadingSilenceSamples) {
    auto it = converters_.find(id);
    if (it != converters_.end() && it->second->InputRateHz() == nativeRate) {
        return *it->second;
    }
    std::unique_ptr<SampleRateConverter> converter =
        factory_(nativeRate, Mixer::kCanonicalSampleRateHz, leadingSilenceSamples);
    SampleRateConverter& ref = *converter;
    converters_[id] = std::move(converter);
    return ref;
}

std::size_t ResamplingMixerFeed::GroupDelaySamplesFor(std::uint32_t nativeRate) {
    if (nativeRate == 0 || nativeRate == Mixer::kCanonicalSampleRateHz) {
        return 0;  // passthrough: no converter, no delay
    }
    auto it = delay_samples_by_native_rate_.find(nativeRate);
    if (it != delay_samples_by_native_rate_.end()) {
        return it->second;
    }
    // Throwaway probe built through the same factory_ as real channels, so this
    // stays independent of any specific converter implementation. A converter
    // that doesn't model group delay contributes 0 (no compensation). Never used
    // for real audio.
    std::unique_ptr<SampleRateConverter> probe = factory_(nativeRate, Mixer::kCanonicalSampleRateHz, 0);
    const std::size_t delay = probe->SteadyStateGroupDelaySamples();
    delay_samples_by_native_rate_[nativeRate] = delay;
    return delay;
}

const LogicalChannelSink& ResamplingMixerFeed::Resample(const LogicalChannelSink& nativeSink,
                                                        const std::vector<LogicalChannel>& channels,
                                                        std::size_t canonicalFrameCount) {
    canonical_.ClearAll();

    // Group-delay compensation: find the largest group delay among this
    // Machine's channels so every other channel can be padded up to match it,
    // keeping chips with different resample ratios in sync. Cheap after the
    // first call (memoized); a passthrough channel contributes 0.
    std::size_t targetDelaySamples = 0;
    for (const LogicalChannel& channel : channels) {
        targetDelaySamples = std::max(targetDelaySamples, GroupDelaySamplesFor(channel.NativeSampleRate));
    }

    for (const LogicalChannel& channel : channels) {
        if (!canonical_.HasChannel(channel.Id)) {
            canonical_.RegisterChannel(channel.Id);
        }

        const float* input = nullptr;
        std::size_t inputCount = 0;
        if (nativeSink.HasChannel(channel.Id)) {
            const LogicalChannelBuffer& buffer = nativeSink.ChannelBuffer(channel.Id);
            input = buffer.Data();
            inputCount = buffer.Size();
        }

        scratch_.assign(canonicalFrameCount, 0.0f);

        const std::uint32_t nativeRate = channel.NativeSampleRate;
        if (nativeRate == 0 || nativeRate == Mixer::kCanonicalSampleRateHz) {
            // Already canonical (or rate unknown): straight copy, trimmed or
            // zero-padded to exactly canonicalFrameCount.
            //
            // Known gap: a passthrough channel gets no delay compensation even
            // if another channel of the same Machine needs it, because there is
            // no converter to seed leading silence into. Only an already-canonical
            // channel (e.g. Gyruss's DAC) takes this path, and the delay spread
            // on that Machine is small.
            const std::size_t copy = std::min(inputCount, canonicalFrameCount);
            for (std::size_t i = 0; i < copy; ++i) {
                scratch_[i] = input[i];
            }
        } else {
            // Preload the full targetDelaySamples on every channel (not just the
            // difference to its own delay): a converter's output onset is
            // max(preload, ownDelay), not their sum, so preloading only the
            // difference collapses back to the channel's own delay when that
            // delay is close to the target. targetDelaySamples >= every
            // channel's own delay, so this aligns all channels.
            ConverterFor(channel.Id, nativeRate, targetDelaySamples)
                .Process(input, inputCount, scratch_.data(), canonicalFrameCount);
        }

        canonical_.WriteSamples(channel.Id, scratch_.data(), scratch_.size());
    }

    return canonical_;
}

void ResamplingMixerFeed::Reset() {
    // Drops the converters instead of resetting them (see the header) so the
    // next Resample() reconstructs them with the group-delay compensation
    // re-seeded. The delay cache is kept: a rate's measured delay doesn't change
    // across a Reset(), and re-measuring on every track switch would be costly.
    converters_.clear();
}
