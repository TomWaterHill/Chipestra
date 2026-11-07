// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "chipestra_abi.h"

#include <cstdint>

// Console analog-stage low-pass filters.
//
// A console's analog output circuit is modelled by up to two stages:
//   - stage A: a second-order low-pass on the FM channels only;
//   - stage B: a first-order low-pass on every filtered channel (FM and PSG), i.e. on the mix.
// Both are linear, so applying stage B to each channel before mixing equals applying it to the mix.
//
// Cutoffs (-3 dB points):
//   Model 1: stage B only, 3380 Hz (the first-order output filter of the Model 1 VA2 board).
//   Model 2: stage A 6725 Hz (Butterworth, Q = 1/sqrt(2)) on FM, stage B 3789 Hz. These two values
//            are the published defaults of another emulator's "Model 2" preset; no component values
//            of the original circuit were found.
// Adding another console model is a matter of adding its stage constants in StagesFor().
namespace analog_filter {

constexpr double kModel1OutputCutoffHz = 3380.0;
constexpr double kModel2FmCutoffHz = 6725.0;
constexpr double kModel2OutputCutoffHz = 3789.0;

// One second-order section (a first-order stage has b2 = a2 = 0), transposed direct form II.
struct Coefficients {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
};

// First-order low-pass, bilinear transform with the cutoff pre-warped to `cutoffHz`.
Coefficients DesignFirstOrderLowPass(double cutoffHz, double sampleRateHz);
// Second-order Butterworth low-pass, bilinear transform with the cutoff pre-warped to `cutoffHz`.
Coefficients DesignButterworthLowPass2(double cutoffHz, double sampleRateHz);

struct SectionState {
    double z1 = 0.0, z2 = 0.0;
    double Process(const Coefficients& c, double x) {
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return y;
    }
};

// The stages of one filter mode.
struct Stages {
    bool hasFm = false;  // stage A present
    Coefficients fm;
    bool hasOutput = false;  // stage B present
    Coefficients output;
};

// Stages of `mode` (a ChipestraAnalogFilterMode value) designed for `sampleRateHz`. Off (and any
// unknown value) has no stages.
Stages StagesFor(std::uint32_t mode, double sampleRateHz);

// The running state of one channel through one mode's stages.
struct ChannelState {
    SectionState fm;
    SectionState output;
    double Process(const Stages& stages, AnalogFilterGroupId group, double x) {
        double v = x;
        if (stages.hasFm && group == AnalogFilterGroupId::Fm) v = fm.Process(stages.fm, v);
        if (stages.hasOutput) v = output.Process(stages.output, v);
        return v;
    }
};

}  // namespace analog_filter
