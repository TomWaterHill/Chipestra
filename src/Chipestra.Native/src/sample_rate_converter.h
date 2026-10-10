// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// SampleRateConverter - resamples one mono float32 stream from a device's
// native sample rate to the mixer's canonical rate (Mixer::kCanonicalSampleRateHz,
// 192kHz). One instance is bound to one stream and keeps its state across
// Process() calls so consecutive blocks join without a discontinuity.
class SampleRateConverter {
public:
    virtual ~SampleRateConverter() = default;

    // Resamples the `inputCount` native-rate samples at `input` into exactly
    // `outputCount` canonical-rate samples at `output` (fully overwritten, never
    // accumulated). Deterministic: a converter constructed the same way and fed
    // the same call sequence always produces the same output. `input` may be
    // null iff `inputCount` is 0.
    virtual void Process(const float* input, std::size_t inputCount, float* output,
                         std::size_t outputCount) = 0;

    // Drops all carried phase/history - the next Process() call starts a
    // fresh stream. Used when playback (re)starts or a Machine is reset.
    virtual void Reset() = 0;

    virtual std::uint32_t InputRateHz() const = 0;
    virtual std::uint32_t OutputRateHz() const = 0;

    // Returns this converter's steady-state group delay in output-rate
    // (canonical) samples. Different resample ratios have very different group
    // delays (e.g. ~63-79ms for a 24x upsample vs ~2ms for a mild downsample).
    // ResamplingMixerFeed queries this on a throwaway instance to pad each
    // channel so the total delay is equal across channels.
    //
    // Calling this consumes the converter's state by feeding it calibration
    // data; use it only on a converter created solely to measure delay.
    //
    // Default: 0 (no modeled group delay, so no compensation).
    virtual std::size_t SteadyStateGroupDelaySamples() { return 0; }
};

// LinearSampleRateConverter - linear interpolation, plus a 2-pole (cascaded
// one-pole) IIR low-pass applied at the input rate whenever inputRateHz >
// outputRateHz. The cutoff is set from outputRateHz, so one formula covers any
// downsample ratio with O(1) state; without it, content above the output
// Nyquist folds back into the audible band. When upsampling the filter is
// skipped entirely.
class LinearSampleRateConverter final : public SampleRateConverter {
public:
    LinearSampleRateConverter(std::uint32_t inputRateHz, std::uint32_t outputRateHz);

    void Process(const float* input, std::size_t inputCount, float* output,
                 std::size_t outputCount) override;
    void Reset() override;

    std::uint32_t InputRateHz() const override { return input_rate_hz_; }
    std::uint32_t OutputRateHz() const override { return output_rate_hz_; }

private:
    std::uint32_t input_rate_hz_;
    std::uint32_t output_rate_hz_;
    // Native-rate samples consumed per canonical-rate sample produced
    // (= input_rate / output_rate). < 1 when upsampling (the common case:
    // 62500 -> 192000).
    double step_;
    // Fractional read cursor into the stream, measured so that position 0.0
    // is `history_` (the sample immediately before the current block's
    // input[0]), 1.0 is input[0], 2.0 is input[1], and so on. Kept ~1 sample
    // behind the newest input and carries its sub-sample remainder from one
    // block to the next.
    double position_;
    // Last input sample of the previous block, used to interpolate across
    // the block boundary.
    float history_;
    bool primed_;

    // Anti-aliasing filter state; used only when downsampling.
    bool anti_alias_enabled_;
    double anti_alias_alpha_;  // one-pole IIR coefficient, derived from outputRateHz's own Nyquist
    float anti_alias_state1_ = 0.0f;
    float anti_alias_state2_ = 0.0f;
    // Scratch buffer for the filtered signal (avoids a per-call allocation).
    std::vector<float> filtered_;
};
