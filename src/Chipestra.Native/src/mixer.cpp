// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "mixer.h"

#include <algorithm>
#include <cmath>

float Mixer::PanLeftGain(float pan) {
    return (1.0f - pan) * 0.5f;
}

float Mixer::PanRightGain(float pan) {
    return (1.0f + pan) * 0.5f;
}

void Mixer::MixBlock(const LogicalChannelSink& sink,
                      std::vector<LogicalChannel>& channels,
                      std::size_t frameCount,
                      std::vector<StereoFrame>& out) const {
    out.assign(frameCount, StereoFrame{});

    for (LogicalChannel& channel : channels) {
        const LogicalChannelBuffer* buffer =
            sink.HasChannel(channel.Id) ? &sink.ChannelBuffer(channel.Id) : nullptr;

        const float leftGain = PanLeftGain(channel.Pan);
        const float rightGain = PanRightGain(channel.Pan);
        // RoutingGain is the board's fixed per-device mix weight; Gain is the
        // user's Channel Strip LEVEL. Both multiply the raw sample.
        const float channelGain = channel.Gain * channel.RoutingGain;

        float peak = 0.0f;
        float sourcePeak = 0.0f;
        double sumSquares = 0.0;
        // Peak/RMS of the post-pan left/right contributions, so the Channel Strip
        // can show true per-channel L/R bars.
        float peakLeft = 0.0f;
        float peakRight = 0.0f;
        double sumSquaresLeft = 0.0;
        double sumSquaresRight = 0.0;

        for (std::size_t i = 0; i < frameCount; ++i) {
            float raw = (buffer != nullptr && i < buffer->Size()) ? (*buffer)[i] : 0.0f;
            const float sourceAbs = std::fabs(raw * channel.RoutingGain);
            if (sourceAbs > sourcePeak) {
                sourcePeak = sourceAbs;
            }
            float sample = channel.Mute ? 0.0f : raw * channelGain;

            float sampleLeft = sample * leftGain;
            float sampleRight = sample * rightGain;
            out[i].left += sampleLeft;
            out[i].right += sampleRight;

            float absSample = sample < 0.0f ? -sample : sample;
            if (absSample > peak) {
                peak = absSample;
            }
            sumSquares += static_cast<double>(sample) * static_cast<double>(sample);

            float absLeft = sampleLeft < 0.0f ? -sampleLeft : sampleLeft;
            if (absLeft > peakLeft) {
                peakLeft = absLeft;
            }
            sumSquaresLeft += static_cast<double>(sampleLeft) * static_cast<double>(sampleLeft);

            float absRight = sampleRight < 0.0f ? -sampleRight : sampleRight;
            if (absRight > peakRight) {
                peakRight = absRight;
            }
            sumSquaresRight += static_cast<double>(sampleRight) * static_cast<double>(sampleRight);
        }

        channel.Peak = peak;
        channel.SourcePeak = sourcePeak;
        channel.RMS = frameCount > 0
                          ? static_cast<float>(std::sqrt(sumSquares / static_cast<double>(frameCount)))
                          : 0.0f;
        channel.PeakLeft = peakLeft;
        channel.PeakRight = peakRight;
        channel.RMSLeft = frameCount > 0
                               ? static_cast<float>(std::sqrt(sumSquaresLeft / static_cast<double>(frameCount)))
                               : 0.0f;
        channel.RMSRight = frameCount > 0
                                ? static_cast<float>(std::sqrt(sumSquaresRight / static_cast<double>(frameCount)))
                                : 0.0f;
    }

    // Master hard clamp, applied once after all channels are summed (per-channel
    // Peak/RMS above are measured pre-clamp).
    for (StereoFrame& frame : out) {
        frame.left = std::clamp(frame.left, -1.0f, 1.0f);
        frame.right = std::clamp(frame.right, -1.0f, 1.0f);
    }
}
