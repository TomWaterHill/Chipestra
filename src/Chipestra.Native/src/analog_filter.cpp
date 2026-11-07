// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "analog_filter.h"

#include <cmath>

namespace analog_filter {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

Coefficients DesignFirstOrderLowPass(double cutoffHz, double sampleRateHz) {
    const double k = std::tan(kPi * cutoffHz / sampleRateHz);
    const double norm = 1.0 / (1.0 + k);
    Coefficients c;
    c.b0 = k * norm;
    c.b1 = c.b0;
    c.a1 = (k - 1.0) * norm;
    return c;
}

Coefficients DesignButterworthLowPass2(double cutoffHz, double sampleRateHz) {
    const double k = std::tan(kPi * cutoffHz / sampleRateHz);
    const double q = 1.0 / std::sqrt(2.0);
    const double norm = 1.0 / (1.0 + k / q + k * k);
    Coefficients c;
    c.b0 = k * k * norm;
    c.b1 = 2.0 * c.b0;
    c.b2 = c.b0;
    c.a1 = 2.0 * (k * k - 1.0) * norm;
    c.a2 = (1.0 - k / q + k * k) * norm;
    return c;
}

Stages StagesFor(std::uint32_t mode, double sampleRateHz) {
    Stages s;
    switch (mode) {
        case CHIPESTRA_ANALOG_FILTER_MODEL1:
            s.hasOutput = true;
            s.output = DesignFirstOrderLowPass(kModel1OutputCutoffHz, sampleRateHz);
            break;
        case CHIPESTRA_ANALOG_FILTER_MODEL2:
            s.hasFm = true;
            s.fm = DesignButterworthLowPass2(kModel2FmCutoffHz, sampleRateHz);
            s.hasOutput = true;
            s.output = DesignFirstOrderLowPass(kModel2OutputCutoffHz, sampleRateHz);
            break;
        default:
            break;
    }
    return s;
}

}  // namespace analog_filter
