// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "chipestra_abi.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// One frame of the 192kHz float32 canonical stereo mix. Values are hard-clamped
// to [-1, 1] by the Mixer - see Mixer::MixBlock.
struct StereoFrame {
    float left = 0.0f;
    float right = 0.0f;
};

// Mixer - combines every LogicalChannel's raw samples (written by a Machine's
// Render() into a LogicalChannelSink) into a single canonical stereo mix,
// applying each channel's mute/pan/gain (user LEVEL) and RoutingGain (the
// board's fixed per-device mix weight) and linearly summing the results.
class Mixer {
public:
    // The canonical mix rate is the Native Runtime's tick rate: one output frame
    // per tick.
    static constexpr std::uint32_t kCanonicalSampleRateHz =
        static_cast<std::uint32_t>(kNativeRuntimeTickRateHz);

    // Audio pipeline block size, defined here so nothing else hardcodes it.
    static constexpr std::size_t kBlockSizeFrames = 256;

    // Mixes `frameCount` samples from each of `channels`' buffers in `sink`
    // into `out` (which is resized to `frameCount` and fully overwritten,
    // not accumulated into). A channel with no buffer in `sink`, or with a
    // buffer shorter than `frameCount`, contributes silence for whatever
    // samples it doesn't have - it is not an error, since a Machine that
    // simply has nothing to say on a channel this block is a normal case.
    //
    // As a side effect, updates each mixed LogicalChannel's Peak/RMS (and
    // left/right variants) for the UI's Channel Strip meters. These are measured
    // pre-clamp (each channel's own true contribution); SourcePeak is measured
    // before mute, gain and pan (only RoutingGain applied). Only the combined
    // `out` frames are clamped.
    //
    // Hard-clamps the combined `out[i].left`/`out[i].right` to [-1, 1] after
    // every channel has been summed. This mirrors real chips such as the YM2610,
    // which sum FM and ADPCM generators onto one internal bus and clamp once, so
    // channels sharing one RoutingGain can exceed 1.0 on loud content without
    // any generator's calibration being wrong. The audio endpoint clamps again
    // as a final safety net.
    void MixBlock(const LogicalChannelSink& sink,
                  std::vector<LogicalChannel>& channels,
                  std::size_t frameCount,
                  std::vector<StereoFrame>& out) const;

private:
    // Linear pan law: pan=-1 -> right gain 0, pan=+1 -> left gain 0, pan=0 ->
    // both 0.5.
    static float PanLeftGain(float pan);
    static float PanRightGain(float pan);
};
