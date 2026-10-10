// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

// GyrussAyRcFilter - the switchable RC low-pass that each ay1/ay2 channel passes
// through before it reaches the mixer, written to match the circuit described
// in MAME's src/mame/konami/gyruss.cpp (DISCRETE_RCFILTER_SW on the ay1/ay2
// channels, switched by filter_w from the chip's Port B):
//   - a series resistor of AY8910_INTERNAL_RESISTANCE + 1000 = 1356 ohm, then a
//     node that reaches ground through an analog switch (modelled at 40 ohm) and
//     a capacitor; the node voltage is the filter output;
//   - Port B carries two bits per channel: bit 0 switches in 0.047 uF, bit 1
//     switches in 0.22 uF, both bits switch in both; no bit means no filter;
//   - the cut-offs are about 2.4 kHz (0.047 uF), 518 Hz (0.22 uF) and 433 Hz
//     (both).
// The filter is the exact discrete-time equivalent (zero-order hold) of that
// network at the sample rate it is given, so the cut-off does not depend on the
// sample rate. A capacitor that is switched out keeps its charge.
class GyrussAyRcFilter {
public:
    static constexpr double kSeriesResistanceOhms = 1356.0;
    static constexpr double kSwitchResistanceOhms = 40.0;
    static constexpr double kLowCapacitorFarads = 0.047e-6;
    static constexpr double kHighCapacitorFarads = 0.22e-6;

    explicit GyrussAyRcFilter(double sampleRateHz) { Configure(sampleRateHz); }

    // bit 0: 0.047 uF, bit 1: 0.22 uF; other bits are ignored.
    void SetSwitches(std::uint8_t bits) { switches_ = bits & 3u; }
    std::uint8_t Switches() const { return switches_; }

    // Discharges both capacitors and switches everything out.
    void Reset() {
        switches_ = 0;
        ResetState();
    }
    // Discharges both capacitors (the switch setting is kept).
    void ResetState() { cap_.fill(0.0); }

    void Process(float* samples, std::size_t count) {
        if (switches_ == 0) return;
        const Model& m = models_[switches_];
        if (m.states == 1) {
            const std::size_t k = (switches_ == 1) ? 0 : 1;
            double x = cap_[k];
            for (std::size_t i = 0; i < count; ++i) {
                const double u = samples[i];
                samples[i] = static_cast<float>(m.a * u + m.b * x);
                x = m.ad[0][0] * x + m.bd[0] * u;
            }
            cap_[k] = x;
        } else {
            double x0 = cap_[0];
            double x1 = cap_[1];
            for (std::size_t i = 0; i < count; ++i) {
                const double u = samples[i];
                samples[i] = static_cast<float>(m.a * u + m.b * (x0 + x1));
                const double n0 = m.ad[0][0] * x0 + m.ad[0][1] * x1 + m.bd[0] * u;
                const double n1 = m.ad[1][0] * x0 + m.ad[1][1] * x1 + m.bd[1] * u;
                x0 = n0;
                x1 = n1;
            }
            cap_[0] = x0;
            cap_[1] = x1;
        }
    }

private:
    struct Model {
        int states = 0;
        double a = 1.0;  // output = a * input + b * (sum of the switched-in capacitor voltages)
        double b = 0.0;
        double ad[2][2] = {};
        double bd[2] = {};
    };

    using Mat3 = std::array<std::array<double, 3>, 3>;

    static Mat3 Multiply(const Mat3& x, const Mat3& y) {
        Mat3 r{};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                for (int k = 0; k < 3; ++k) r[i][j] += x[i][k] * y[k][j];
        return r;
    }

    // exp(m) by scaling and squaring around a Taylor series.
    static Mat3 Exponential(Mat3 m) {
        double norm = 0.0;
        for (const auto& row : m) {
            double s = 0.0;
            for (double v : row) s += std::fabs(v);
            if (s > norm) norm = s;
        }
        int squarings = 0;
        while (norm > 0.5) {
            norm *= 0.5;
            ++squarings;
        }
        const double scale = std::ldexp(1.0, -squarings);
        for (auto& row : m)
            for (double& v : row) v *= scale;
        Mat3 result{};
        Mat3 term{};
        for (int i = 0; i < 3; ++i) {
            result[i][i] = 1.0;
            term[i][i] = 1.0;
        }
        for (int n = 1; n <= 16; ++n) {
            term = Multiply(term, m);
            for (auto& row : term)
                for (double& v : row) v /= n;
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j) result[i][j] += term[i][j];
        }
        for (int s = 0; s < squarings; ++s) result = Multiply(result, result);
        return result;
    }

    void Configure(double sampleRateHz) {
        const double dt = 1.0 / sampleRateHz;
        const double r = kSeriesResistanceOhms;
        const double rs = kSwitchResistanceOhms;
        const double caps[2] = {kLowCapacitorFarads, kHighCapacitorFarads};
        for (std::uint8_t sw = 1; sw <= 3; ++sw) {
            Model& model = models_[sw];
            const int n = (sw == 3) ? 2 : 1;
            const int first = (sw == 2) ? 1 : 0;
            model.states = n;
            model.a = rs / (rs + n * r);
            model.b = r / (rs + n * r);
            // dx_k/dt = (node - x_k) / (rs * C_k), node = a * u + b * sum(x)
            Mat3 m{};
            for (int k = 0; k < n; ++k) {
                const double tau = rs * caps[first + k];
                for (int j = 0; j < n; ++j) m[k][j] = (model.b - (j == k ? 1.0 : 0.0)) / tau * dt;
                m[k][n] = model.a / tau * dt;
            }
            const Mat3 e = Exponential(m);
            for (int k = 0; k < n; ++k) {
                for (int j = 0; j < n; ++j) model.ad[k][j] = e[k][j];
                model.bd[k] = e[k][n];
            }
        }
    }

    std::array<Model, 4> models_{};
    std::array<double, 2> cap_{};
    std::uint8_t switches_ = 0;
};
