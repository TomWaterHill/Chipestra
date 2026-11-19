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
// Model 1: stage B only, a first-order low-pass at 3380 Hz (the output filter of the Model 1 VA2 board).
//
// Model 2 (the VA3 board), from the circuit of the Sega Genesis Model 2 VA3 service manual (the sound
// network IC9, part number 315-5684, and its external parts):
//   - stage A, FM only: a unity-gain Sallen-Key low-pass built from two internal 10 kOhm resistors and
//     the external capacitors C22 = 5600 pF (feedback) and C21 = 1200 pF (to the reference):
//       f0 = 1 / (2 pi R sqrt(C1 C2)) = 6140 Hz,  Q = sqrt(C1 C2) / (2 C2) = 1.08.
//     A Q above 1/sqrt(2) gives a peak of about +1.7 dB near 4.6 kHz.
//   - stage B, FM and PSG: the feedback resistor of the mixing amplifier (internal 27 kOhm) with the
//     external C55 = 1800 pF in parallel:  f = 1 / (2 pi 27 kOhm 1800 pF) = 3275 Hz.
//     The same 27 kOhm and 1800 pF are the values of the Model 1 VA7 first-order stage.
// The component values are read from the schematic and the block diagram of that manual; the tolerance
// of the parts is not known. An earlier Chipestra version used 6725 Hz (Butterworth) and 3789 Hz, the
// published defaults of another emulator's "Model 2" preset, before the circuit values were available.
// Boards earlier than the VA3 (VA0 to VA1.8) may use a different circuit.
// Adding another console model is a matter of adding its stage constants in StagesFor().
namespace analog_filter {

constexpr double kModel1OutputCutoffHz = 3380.0;
constexpr double kModel2FmCutoffHz = 6140.0;
constexpr double kModel2FmQ = 1.08;
constexpr double kModel2OutputCutoffHz = 3275.0;

// One second-order section (a first-order stage has b2 = a2 = 0), transposed direct form II.
struct Coefficients {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
};

// First-order low-pass, bilinear transform with the cutoff pre-warped to `cutoffHz`.
Coefficients DesignFirstOrderLowPass(double cutoffHz, double sampleRateHz);
// Second-order low-pass with natural frequency `cutoffHz` and quality factor `q`, bilinear transform with
// the frequency pre-warped. q = 1/sqrt(2) is the Butterworth response.
Coefficients DesignSecondOrderLowPass(double cutoffHz, double q, double sampleRateHz);

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
