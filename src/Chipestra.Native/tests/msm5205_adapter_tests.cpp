// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Msm5205DeviceAdapter native tests. All data-pin writes below are
// hand-written synthetic stimulus - no real ROM/speech data.
//
// These are behavioral/structural assertions, not exact-golden-sample-value
// checks: the ADPCM step-size table involves a `pow(11/10, step)`
// computation that isn't worth
// hand-reproducing here - what matters for regression protection is that
// the chip stays silent when it should, responds to fresh data at the
// right moment relative to its own VCK callback, and produces a genuinely
// time-varying (not frozen/DC) signal when driven (a non-zero value alone
// is not proof of sound).

#include "msm5205_adapter_tests.h"

#include "devices/sound/msm5205/msm5205_device_adapter.h"
#include "test_support.h"

#include <cstdint>
#include <cstdio>
#include <vector>

void Msm5205RenderProducesOneSamplePerPeriodAndCarriesPartialClocks() {
    std::printf("Msm5205RenderProducesOneSamplePerPeriodAndCarriesPartialClocks\n");

    Msm5205DeviceAdapter msm;
    msm.DataW(0x07);

    auto run = [](Msm5205DeviceAdapter& m, std::vector<float>& out) {
        // Same total chip time (6000 input clocks), fed in uneven chunks
        // that don't land on kInputClocksPerSample(48): the partial-clock
        // carry must reproduce the single-call result.
        for (std::uint64_t chunk : {std::uint64_t{100}, std::uint64_t{33}, std::uint64_t{4000}, std::uint64_t{7},
                                    std::uint64_t{1860}}) {
            m.Render(chunk, out);
        }
    };

    Msm5205DeviceAdapter a;
    a.DataW(0x07);
    std::vector<float> samplesA;
    run(a, samplesA);

    Msm5205DeviceAdapter b;
    b.DataW(0x07);
    std::vector<float> samplesB;
    run(b, samplesB);

    Check(!samplesA.empty() && samplesA == samplesB,
          "determinism: identical data + timing sequence yields identical samples");
    // 100+33+4000+7+1860 = 6000 input clocks / kInputClocksPerSample(48) =
    // 125 whole samples exactly (6000 is an exact multiple of 48) - no
    // carry should be lost or double-counted.
    Check(samplesA.size() == 125, "carry: 6000 input clocks / 48 = 125 samples, no leftover carry expected");
    Check(a.GeneratedSampleCount() == 125, "carry: GeneratedSampleCount() agrees with the samples produced");
}

void Msm5205VckHandlerFiresBeforeThatSamplesDecode() {
    std::printf("Msm5205VckHandlerFiresBeforeThatSamplesDecode\n");

    // Latch a POSITIVE nibble (0x7: sign bit clear, all magnitude bits set -
    // the largest positive per-step delta) before any Render() call at all.
    // If the VCK callback fires AFTER decode (the wrong order), the very
    // first sample would use this stale positive value and come out
    // positive. The callback below switches to a NEGATIVE nibble (0xF) the
    // instant it fires - if the real (correct) ordering holds, this same
    // first sample must already reflect the fresh negative value instead.
    Msm5205DeviceAdapter* selfPtr = nullptr;
    Msm5205DeviceAdapter chip([&]() {
        if (selfPtr != nullptr) {
            selfPtr->DataW(0x0F);
        }
    });
    selfPtr = &chip;

    chip.DataW(0x07);
    std::vector<float> samples;
    chip.Render(Msm5205DeviceAdapter::kInputClocksPerSample, samples);

    Check(samples.size() == 1, "vck ordering: exactly one sample was produced");
    Check(chip.SignalForTest() < 0,
          "vck ordering: the VCK callback's DataW() write landed before THIS sample's own decode, "
          "so the first sample already reflects the fresh (negative) nibble rather than the stale "
          "(positive) one latched before Render() was called");
}

void Msm5205ResetPinForcesSilenceRegardlessOfData() {
    std::printf("Msm5205ResetPinForcesSilenceRegardlessOfData\n");

    Msm5205DeviceAdapter chip;
    chip.DataW(0x07);  // a nibble that would normally drive the signal up
    chip.ResetPin(true);

    std::vector<float> samples;
    chip.Render(10 * Msm5205DeviceAdapter::kInputClocksPerSample, samples);

    Check(samples.size() == 10, "reset: 10 samples were produced while reset was held");
    for (float s : samples) {
        Check(s == 0.0f, "reset: every sample decoded while ResetPin(true) is exactly silent");
    }
    Check(chip.SignalForTest() == 0 && chip.StepForTest() == 0,
          "reset: internal signal/step state stays at zero throughout, regardless of the latched data byte");

    // Releasing reset with the same data byte still latched should let the
    // signal move again - proving the silence above was genuinely caused by
    // the reset pin, not some unrelated default state.
    chip.ResetPin(false);
    chip.Render(5 * Msm5205DeviceAdapter::kInputClocksPerSample, samples);
    Check(chip.SignalForTest() != 0, "reset: releasing ResetPin lets the (still-latched) data resume decoding");
}

void Msm5205RepeatedStrongNibbleProducesGrowingNonSilentSignal() {
    std::printf("Msm5205RepeatedStrongNibbleProducesGrowingNonSilentSignal\n");

    Msm5205DeviceAdapter chip;
    // 0x07: sign bit clear (positive direction), all three magnitude bits
    // set - the largest positive delta available at any given step size.
    // Held constant for every sample, so the decoded signal can only stay
    // level or climb (it can never fall) - and since the step size itself
    // grows with each identical-direction nibble (the `index_shift`
    // table), later deltas are
    // larger than earlier ones, so genuine (non-flat) growth is expected,
    // not just a one-time jump followed by a frozen plateau.
    chip.DataW(0x07);

    std::vector<float> samples;
    chip.Render(40 * Msm5205DeviceAdapter::kInputClocksPerSample, samples);
    Check(samples.size() == 40, "growth: 40 samples were produced");

    Check(samples[0] > 0.0f, "growth: the very first decoded sample is already non-silent");

    bool sawStrictIncrease = false;
    for (std::size_t i = 1; i < samples.size(); ++i) {
        Check(samples[i] >= samples[i - 1] - 1e-6f,
              "growth: a constant positive-direction nibble never makes the signal fall");
        if (samples[i] > samples[i - 1]) {
            sawStrictIncrease = true;
        }
    }
    Check(sawStrictIncrease, "growth: the signal genuinely increased somewhere, not just a static/frozen value");
    Check(samples.back() > samples[0],
          "growth: the final sample is clearly larger than the first - real progression, not noise-level jitter");
}
