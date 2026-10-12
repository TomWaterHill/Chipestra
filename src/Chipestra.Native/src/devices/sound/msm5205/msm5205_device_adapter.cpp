// license:BSD-3-Clause
// copyright-holders:Aaron Giles
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/sound/msm5205.cpp
//   src/devices/sound/msm5205.h
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#include "devices/sound/msm5205/msm5205_device_adapter.h"

#include <algorithm>
#include <cmath>

namespace {

// Step-size index adjustment table, indexed by the low 3 bits of the
// decoded nibble (MAME's `index_shift[8]`).
constexpr int kIndexShift[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

}  // namespace

Msm5205DeviceAdapter::Msm5205DeviceAdapter(VckHandler vckHandler, Mode mode)
    : vck_handler_(std::move(vckHandler)), mode_(mode) {
    // Precompute the 49-step x 16-nibble difference table (MAME's
    // compute_tables()). kNibbleToBit decodes a 4-bit ADPCM nibble into a sign
    // plus three magnitude bits.
    constexpr int kNibbleToBit[16][4] = {
        {1, 0, 0, 0},   {1, 0, 0, 1},   {1, 0, 1, 0},   {1, 0, 1, 1},   {1, 1, 0, 0},   {1, 1, 0, 1},
        {1, 1, 1, 0},   {1, 1, 1, 1},   {-1, 0, 0, 0},  {-1, 0, 0, 1},  {-1, 0, 1, 0},  {-1, 0, 1, 1},
        {-1, 1, 0, 0},  {-1, 1, 0, 1},  {-1, 1, 1, 0},  {-1, 1, 1, 1},
    };
    for (int step = 0; step <= 48; ++step) {
        const int stepval = static_cast<int>(std::floor(16.0 * std::pow(11.0 / 10.0, static_cast<double>(step))));
        for (int nib = 0; nib < 16; ++nib) {
            diff_lookup_[step * 16 + nib] =
                kNibbleToBit[nib][0] * (stepval * kNibbleToBit[nib][1] + stepval / 2 * kNibbleToBit[nib][2] +
                                        stepval / 4 * kNibbleToBit[nib][3] + stepval / 8);
        }
    }
}

void Msm5205DeviceAdapter::Reset() {
    // As in MAME's device_reset(), only signal and step are cleared; the data
    // latch and reset pin are left untouched.
    signal_ = 0;
    step_ = 0;
    partial_clocks_ = 0;
    generated_samples_ = 0;
    slave_events_.clear();
    rendered_clocks_ = 0;
    last_slave_write_clock_ = 0;
}

void Msm5205DeviceAdapter::ResetPin(bool asserted) { reset_pin_ = asserted; }

void Msm5205DeviceAdapter::DataW(std::uint8_t value) { data_ = value & 0x0F; }

void Msm5205DeviceAdapter::DecodeOneSample() {
    // As in MAME's update_adpcm(), the VCK callback fires BEFORE the decode
    // step, so a synchronous NMI handler can feed the freshest nibble into
    // this same sample.
    if (vck_handler_) {
        vck_handler_();
    }
    DecodeOneSampleNoCallback();
}

void Msm5205DeviceAdapter::DecodeOneSampleNoCallback() {
    if (reset_pin_) {
        signal_ = 0;
        step_ = 0;
    } else {
        std::int32_t newSignal = signal_ + diff_lookup_[step_ * 16 + (data_ & 15)];
        newSignal = std::clamp(newSignal, -2048, 2047);
        signal_ = newSignal;

        step_ += kIndexShift[data_ & 7];
        step_ = std::clamp(step_, 0, 48);
    }
}

bool Msm5205DeviceAdapter::SlaveWrite(std::uint64_t atClock, std::uint8_t value) {
    if (mode_ != Mode::kSlave || atClock < rendered_clocks_ || atClock < last_slave_write_clock_) {
        return false;
    }
    last_slave_write_clock_ = atClock;
    slave_events_.push_back({atClock + kSlaveCaptureDelayClocks, static_cast<std::uint8_t>(value & 0x0F),
                             (value & 0x80) != 0});
    return true;
}

std::size_t Msm5205DeviceAdapter::Render(std::uint64_t inputClocks, std::vector<float>& samples) {
    if (mode_ == Mode::kSlave) {
        // Zero-order hold at the input clock: each clock emits the current
        // signal; a captured write decodes at its effective clock.
        constexpr std::int32_t kSlaveDacMask = (1 << (12 - 10)) - 1;
        for (std::uint64_t i = 0; i < inputClocks; ++i, ++rendered_clocks_) {
            while (!slave_events_.empty() && slave_events_.front().effective_clock <= rendered_clocks_) {
                const SlaveEvent event = slave_events_.front();
                slave_events_.pop_front();
                data_ = event.data;
                reset_pin_ = event.reset;
                DecodeOneSampleNoCallback();
            }
            samples.push_back(static_cast<float>(signal_ & ~kSlaveDacMask) / 4096.0f);
            ++generated_samples_;
        }
        return static_cast<std::size_t>(inputClocks);
    }
    const std::uint64_t available = partial_clocks_ + inputClocks;
    const std::size_t sampleCount = static_cast<std::size_t>(available / kInputClocksPerSample);
    partial_clocks_ = available % kInputClocksPerSample;

    for (std::size_t i = 0; i < sampleCount; ++i) {
        DecodeOneSample();
        // Scale 1/4096 over signal_'s [-2048, 2047] range. The plain MSM5205
        // (unlike the MSM6585) has a 10-bit DAC, so the low 2 bits of the
        // 12-bit accumulator are masked off before output, as in MAME.
        constexpr std::int32_t kDacMask = (1 << (12 - 10)) - 1;  // 3, low 2 bits
        samples.push_back(static_cast<float>(signal_ & ~kDacMask) / 4096.0f);
        ++generated_samples_;
    }
    return sampleCount;
}
