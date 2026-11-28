// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "level_matching.h"

#include <cmath>

float LevelMatchingRamp::DbToGain(float db) {
    return db == 0.0f ? 1.0f : std::pow(10.0f, db / 20.0f);
}

void LevelMatchingRamp::Reset(bool enabled, float gainDb) {
    currentDb_ = enabled ? gainDb : 0.0f;
}

bool LevelMatchingRamp::NextBlock(bool enabled, float gainDb, Mixer::LevelMatching& out) {
    const float target = enabled ? gainDb : 0.0f;
    if (!enabled && currentDb_ == 0.0f) {
        return false;
    }
    const float start = currentDb_;
    float next = start;
    if (target > start) {
        next = (target - start > kRampDbPerBlock) ? start + kRampDbPerBlock : target;
    } else if (target < start) {
        next = (start - target > kRampDbPerBlock) ? start - kRampDbPerBlock : target;
    }
    currentDb_ = next;
    out.gainStart = DbToGain(start);
    out.gainEnd = DbToGain(next);
    out.limiter = true;   // also while the gain returns to 0 dB after the setting was switched off
    return true;
}
