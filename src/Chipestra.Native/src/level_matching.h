// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "mixer.h"

// Per-block state of the level matching: the gain follows its target in steps of kRampDbPerBlock, so that
// switching it (also while playing) never makes a click. Used by the playback thread only. While the setting is
// off and the gain is back at 0 dB, NextBlock returns false: the Mixer is then called without level matching and
// the output is exactly the output without this feature.
class LevelMatchingRamp {
public:
    static constexpr float kRampDbPerBlock = 0.4f;   // 12 dB in 40 ms at 256 frames of 192 kHz

    // Jumps to the target (used when playback starts).
    void Reset(bool enabled, float gainDb);

    // Moves one block toward the target. Returns false when nothing is to be applied to this block.
    bool NextBlock(bool enabled, float gainDb, Mixer::LevelMatching& out);

    static float DbToGain(float db);

private:
    float currentDb_ = 0.0f;
};
