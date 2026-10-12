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

void Msm5205SlaveDecodesOncePerWriteAtTheWriteTime() {
    std::printf("Msm5205SlaveDecodesOncePerWriteAtTheWriteTime\n");

    using Mode = Msm5205DeviceAdapter::Mode;
    Msm5205DeviceAdapter slave(nullptr, Mode::kSlave);
    Check(slave.SlaveWrite(100, 0x07), "slave: a write at a future clock is accepted");

    std::vector<float> out;
    const std::size_t produced = slave.Render(300, out);
    Check(produced == 300 && out.size() == 300, "slave: one output sample per input clock");

    const std::uint64_t switchAt = 100 + Msm5205DeviceAdapter::kSlaveCaptureDelayClocks;
    for (std::uint64_t i = 0; i < switchAt; ++i) {
        Check(out[i] == 0.0f, "slave: the output holds its previous value until the write time (+capture delay)");
    }
    Check(out[switchAt] > 0.0f, "slave: the value switches exactly at the write time plus the capture delay");
    for (std::size_t i = switchAt + 1; i < out.size(); ++i) {
        Check(out[i] == out[switchAt], "slave: zero-order hold - one write decodes exactly one sample");
    }

    // The single decode matches the master-mode decode of the same nibble.
    Msm5205DeviceAdapter master;
    master.DataW(0x07);
    std::vector<float> masterOut;
    master.Render(Msm5205DeviceAdapter::kInputClocksPerSample, masterOut);
    Check(masterOut.size() == 1 && masterOut[0] == out[switchAt], "slave: same ADPCM result as the master-mode decode");
}

void Msm5205SlaveResetBitAndDataNibble() {
    std::printf("Msm5205SlaveResetBitAndDataNibble\n");

    Msm5205DeviceAdapter chip(nullptr, Msm5205DeviceAdapter::Mode::kSlave);
    chip.SlaveWrite(0, 0x07);
    chip.SlaveWrite(10, 0x07);
    chip.SlaveWrite(20, 0x17);  // bits 4-6 are ignored: only bit 7 and the low nibble matter
    std::vector<float> out;
    chip.Render(100, out);
    Check(chip.SignalForTest() > 0 && chip.StepForTest() > 0, "slave reset: three writes grew the signal and the step");
    Check(out[40] > out[17] && out[17] > 0.0f, "slave reset: each write advances the held value");

    chip.SlaveWrite(120, 0x87);  // RESET pin high with a data nibble: silence
    out.clear();
    chip.Render(100, out);
    Check(chip.SignalForTest() == 0 && chip.StepForTest() == 0, "slave reset: bit 7 forces signal=0 and step=0");
    Check(out.back() == 0.0f, "slave reset: the output is silent after the reset write");

    chip.SlaveWrite(250, 0x07);  // reset released by the next write
    out.clear();
    chip.Render(100, out);
    Check(chip.SignalForTest() > 0, "slave reset: a following write with bit 7 clear decodes normally again");
}

void Msm5205SlaveWriteTimingIsIndependentOfRenderChunking() {
    std::printf("Msm5205SlaveWriteTimingIsIndependentOfRenderChunking\n");

    auto make = [](Msm5205DeviceAdapter& chip) {
        chip.SlaveWrite(5, 0x05);
        chip.SlaveWrite(70, 0x0D);
        chip.SlaveWrite(70, 0x07);  // same clock: both decode, in order
        chip.SlaveWrite(333, 0x03);
    };
    Msm5205DeviceAdapter whole(nullptr, Msm5205DeviceAdapter::Mode::kSlave);
    make(whole);
    std::vector<float> a;
    whole.Render(500, a);

    Msm5205DeviceAdapter chunked(nullptr, Msm5205DeviceAdapter::Mode::kSlave);
    make(chunked);
    std::vector<float> b;
    for (std::uint64_t done = 0; done < 500;) {
        const std::uint64_t n = (done % 3 == 0) ? 7 : 61;
        chunked.Render(n < 500 - done ? n : 500 - done, b);
        done += n < 500 - done ? n : 500 - done;
    }
    Check(a.size() == 500 && a == b, "slave timing: identical output regardless of how Render() is chunked");
    Check(chunked.RenderedClocks() == 500, "slave timing: RenderedClocks() counts every rendered input clock");

    Check(!chunked.SlaveWrite(499, 0x01), "slave timing: a write before the already-rendered time is rejected");
    Check(chunked.SlaveWrite(500, 0x01), "slave timing: a write at exactly the rendered time is accepted");

    Msm5205DeviceAdapter master;
    Check(!master.SlaveWrite(0, 0x01), "slave timing: SlaveWrite() is rejected in master mode");
}
