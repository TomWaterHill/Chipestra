// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include <cmath>
#include <cstddef>

// GyrussMixerLowPass - the first-order low-pass at the output of each Gyruss
// mixer bus, matching MAME's src/mame/konami/gyruss.cpp (konami_right_mixer_desc /
// konami_left_mixer_desc: resistor mixer, 200 ohm feedback resistor, 0.1 uF
// feedback capacitor). The corner is set by the parallel resistance of the
// feedback resistor and the bus's input resistors times 0.1 uF:
//   right bus (ay1 x3 2.2k, ay3 and ay4 1.1k each): 122.2 ohm -> about 13.0 kHz
//   left bus  (ay2 x3 2.2k, ay5 1.1k, DAC 4.7k):    133.6 ohm -> about 11.9 kHz
// It is linear and acts on the bus sum, so every input of a bus gets the same
// filter; here it is applied to each channel before the Mixer adds them.
// (The mixer's 1 uF output capacitor, a DC-removal high-pass with a 100 kOhm
// load, is covered by the machine's own DC-blocking filter.)
class GyrussMixerLowPass {
public:
    static constexpr double kFeedbackOhms = 200.0;
    static constexpr double kFeedbackFarads = 0.1e-6;

    // Parallel resistance of the feedback resistor and the bus's inputs.
    static constexpr double RightBusOhms() { return 1.0 / (1.0 / kFeedbackOhms + 3.0 / 2200.0 + 2.0 / 1100.0); }
    static constexpr double LeftBusOhms() { return 1.0 / (1.0 / kFeedbackOhms + 3.0 / 2200.0 + 1.0 / 1100.0 + 1.0 / 4700.0); }

    static double CutoffHz(double busOhms) {
        return 1.0 / (2.0 * 3.14159265358979323846 * busOhms * kFeedbackFarads);
    }

    GyrussMixerLowPass() = default;
    GyrussMixerLowPass(double busOhms, double sampleRateHz) { Configure(busOhms, sampleRateHz); }

    void Configure(double busOhms, double sampleRateHz) {
        alpha_ = 1.0 - std::exp(-1.0 / (busOhms * kFeedbackFarads * sampleRateHz));
    }
    void Reset() { state_ = 0.0; }

    void Process(float* samples, std::size_t count) {
        double y = state_;
        for (std::size_t i = 0; i < count; ++i) {
            y += (static_cast<double>(samples[i]) - y) * alpha_;
            samples[i] = static_cast<float>(y);
        }
        state_ = y;
    }

private:
    double alpha_ = 1.0;  // 1.0 = pass-through until configured
    double state_ = 0.0;
};
