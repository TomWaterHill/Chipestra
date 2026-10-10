// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Ay8910DeviceAdapter native tests. All register writes below are
// hand-written synthetic stimulus - no real ROM data.
//
// Expected periods/waveforms are derived independently from the public
// AY-3-8910 register spec (tone period = 12-bit fine+coarse, one tone-
// generator toggle every `period` output samples; envelope = 16-step
// generator pacing at 2x the tone/noise sample rate, shape 0x0C = repeating
// rising sawtooth per the datasheet's envelope-shape table). One
// Ay8910DeviceAdapter output sample == kInputClocksPerSample (8) input
// clocks.

#include <cmath>
#include "ay8910_adapter_tests.h"

#include "devices/sound/ay8910/ay8910_device_adapter.h"
#include "test_support.h"

#include <cstdint>
#include <vector>

namespace {

// AY_* register indices (ay8910_device_adapter.cpp's own RegisterIndex enum
// is private - tests drive the chip through the same public bus interface
// the owning Machine would, so they need their own copy of the numbering).
constexpr std::uint8_t kRegAFine = 0x00;
constexpr std::uint8_t kRegACoarse = 0x01;
constexpr std::uint8_t kRegNoisePeriod = 0x06;
constexpr std::uint8_t kRegEnable = 0x07;
constexpr std::uint8_t kRegAVol = 0x08;
constexpr std::uint8_t kRegEnvFine = 0x0B;
constexpr std::uint8_t kRegEnvCoarse = 0x0C;
constexpr std::uint8_t kRegEnvShape = 0x0D;

std::vector<std::vector<float>> MakeChannelBuffers() {
    return std::vector<std::vector<float>>(Ay8910DeviceAdapter::kChannelCount);
}

void WriteReg(Ay8910DeviceAdapter& ay, std::uint8_t reg, std::uint8_t value) {
    ay.WriteAddress(reg);
    ay.WriteData(value);
}

}  // namespace

void Ay8910RegisterSemanticsUpdateInternalState() {
    std::printf("Ay8910RegisterSemanticsUpdateInternalState\n");

    Ay8910DeviceAdapter ay;

    // Tone A period: 12-bit fine+coarse, coarse masked to 4 bits.
    WriteReg(ay, kRegAFine, 0x34);
    WriteReg(ay, kRegACoarse, 0xF2);  // upper nibble (0xF0) must be masked off
    Check(ay.RegisterForTest(kRegAFine) == 0x34, "register semantics: AFINE register byte stored as written");
    Check(ay.RegisterForTest(kRegACoarse) == 0xF2, "register semantics: ACOARSE register byte stored as written (unmasked)");
    Check(ay.TonePeriodForTest(0) == 0x234, "register semantics: tone A period = fine | ((coarse & 0x0F) << 8)");

    // Volume register mirrors directly into the tone generator's own state.
    WriteReg(ay, kRegAVol, 0x0B);
    Check(ay.RegisterForTest(kRegAVol) == 0x0B, "register semantics: AVOL register byte stored as written");

    // Noise period: 5-bit register, read on demand (no derived state).
    WriteReg(ay, kRegNoisePeriod, 0xFF);
    Check(ay.NoisePeriodForTest() == 0x1F, "register semantics: noise period masked to 5 bits");

    // Envelope period: 16-bit fine+coarse (no masking, unlike tone).
    WriteReg(ay, kRegEnvFine, 0x78);
    WriteReg(ay, kRegEnvCoarse, 0x56);
    Check(ay.EnvelopePeriodForTest() == 0x5678, "register semantics: envelope period = fine | (coarse << 8)");

    // Envelope shape: writing it recomputes attack/hold/alternate and the
    // *immediate* volume (step ^ attack) synchronously, before any Render().
    WriteReg(ay, kRegEnvShape, 0x0C);  // C=1,At=1,Al=0,H=0 -> repeating rising sawtooth
    Check(ay.EnvelopeVolumeForTest() == 0, "register semantics: EASHAPE write recomputes volume immediately (step=attack=0x0F, XOR=0)");
    Check(ay.EnvelopeStepForTest() == 0x0F, "register semantics: EASHAPE write resets step to the 16-step mask");

    // A register value outside the 4-bit register address space deactivates
    // the latch; a subsequent data write must then be silently ignored.
    ay.WriteAddress(0x10);  // >= 0x10 -> inactive on a plain AY-3-8910
    ay.WriteData(0xAA);
    Check(ay.RegisterForTest(kRegAFine) == 0x34, "register semantics: data write while inactive (invalid address latched) is ignored");
}

void Ay8910PortWriteCallbackFiresOnEnableEdge() {
    std::printf("Ay8910PortWriteCallbackFiresOnEnableEdge\n");

    std::vector<std::uint8_t> portAWrites;
    std::vector<std::uint8_t> portBWrites;
    Ay8910DeviceAdapter ay([&](std::uint8_t v) { portAWrites.push_back(v); },
                            [&](std::uint8_t v) { portBWrites.push_back(v); });

    // The constructor's own Reset() forces last_enable_=0xC0 before zeroing
    // AY_ENABLE (ay8910.cpp's ay8910_reset_ym(): "m_last_enable = 0xc0; //
    // force a write"), so both port callbacks legitimately fire once with
    // 0xFF (both ports pulled to input/high-Z) as a side effect of
    // construction, before this test's own stimulus. Discard that so the
    // assertions below observe only the edges this test explicitly drives.
    portAWrites.clear();
    portBWrites.clear();

    // Bit 6 = port A direction (1 = output). The edge from input->output
    // latches the current PORTA register value out immediately.
    constexpr std::uint8_t kRegPortA = 0x0E;
    WriteReg(ay, kRegPortA, 0x77);  // port register value, latched once output mode turns on below
    WriteReg(ay, kRegEnable, 0x40);
    Check(portAWrites.size() == 1 && portAWrites.back() == 0x77,
          "port callback: ENABLE bit 6 rising edge latches the current PORTA value out");

    // A further PORTA write while still in output mode fires again.
    WriteReg(ay, kRegPortA, 0x99);
    Check(portAWrites.size() == 2 && portAWrites.back() == 0x99, "port callback: PORTA write while output-enabled fires again");

    // The falling edge (back to input mode) fires once more with 0xFF (high impedance).
    WriteReg(ay, kRegEnable, 0x00);
    Check(portAWrites.size() == 3 && portAWrites.back() == 0xFF, "port callback: ENABLE bit 6 falling edge latches 0xFF (high impedance)");

    Check(portBWrites.empty(), "port callback: port B callback never fired (bit 7 never touched)");
}

void Ay8910ToneGeneratorOutputMatchesProgrammedPeriod() {
    std::printf("Ay8910ToneGeneratorOutputMatchesProgrammedPeriod\n");

    Ay8910DeviceAdapter ay;
    auto buffers = MakeChannelBuffers();

    // Isolate channel A's tone generator: tone A contributing (ENABLE bit 0
    // clear), noise A excluded (ENABLE bit 3 set) - channels B/C stay at
    // their power-on volume 0, so they can't contribute regardless of their
    // own enable bits.
    WriteReg(ay, kRegEnable, 0x08);
    WriteReg(ay, kRegAVol, 0x0F);  // fixed volume 15, envelope mode off (bit 4 clear)
    WriteReg(ay, kRegAFine, 10);
    WriteReg(ay, kRegACoarse, 0);  // tone A period = 10 samples

    ay.Render(25 * Ay8910DeviceAdapter::kInputClocksPerSample, buffers);
    Check(buffers[0].size() == 25, "tone generator: Render() produced one sample per kInputClocksPerSample input clocks");

    // Per this file's own header comment: output starts low, the 10th tick
    // (sample index 9) flips it high, the 20th tick (index 19) flips it back.
    Check(buffers[0][0] == buffers[0][8], "tone generator: samples 0-8 are the same (pre-toggle) level");
    Check(buffers[0][9] == buffers[0][18], "tone generator: samples 9-18 are the same (post-toggle) level");
    Check(buffers[0][8] != buffers[0][9], "tone generator: the programmed period (10 samples) toggled the output exactly once by sample 9");
    Check(buffers[0][19] == buffers[0][0], "tone generator: sample 19 toggled back to the original level (period confirmed both edges)");
}

void Ay8910NoiseGeneratorAdvancesThePseudoRandomSequence() {
    std::printf("Ay8910NoiseGeneratorAdvancesThePseudoRandomSequence\n");

    Ay8910DeviceAdapter ay;
    auto buffers = MakeChannelBuffers();

    // Isolate channel A's noise generator: tone A excluded (bit 0 set),
    // noise A contributing (bit 3 clear). Fastest possible noise rate
    // (period register minimum, 0 == 1 per the shared tone/noise "period 0
    // behaves as period 1" rule).
    WriteReg(ay, kRegEnable, 0x01);
    WriteReg(ay, kRegAVol, 0x0F);
    WriteReg(ay, kRegNoisePeriod, 0);

    const std::uint32_t seedRng = ay.NoiseRngForTest();

    // Render one output sample at a time; NoiseOutputForTest() (the exact
    // bit TickOneSample() just gated this sample with) must agree with the
    // sample actually written, every single sample - not just in aggregate.
    bool sawHigh = false;
    bool sawLow = false;
    float highLevel = 0.0f;
    float lowLevel = 0.0f;
    bool haveHighLevel = false;
    bool haveLowLevel = false;
    for (int i = 0; i < 64; ++i) {
        for (std::vector<float>& buf : buffers) buf.clear();
        ay.Render(Ay8910DeviceAdapter::kInputClocksPerSample, buffers);
        const float sample = buffers[0].back();
        if (ay.NoiseOutputForTest()) {
            sawHigh = true;
            if (!haveHighLevel) { highLevel = sample; haveHighLevel = true; }
            Check(sample == highLevel, "noise generator: every sample gated 'on' by the LFSR uses the same volume-table entry");
        } else {
            sawLow = true;
            if (!haveLowLevel) { lowLevel = sample; haveLowLevel = true; }
            Check(sample == lowLevel, "noise generator: every sample gated 'off' by the LFSR uses the same volume-table entry");
        }
    }
    Check(ay.NoiseRngForTest() != seedRng, "noise generator: the 17-bit LFSR actually advanced after 64 samples");
    Check(sawHigh && sawLow, "noise generator: both LFSR output levels were observed across 64 samples (not stuck)");
    Check(highLevel != lowLevel, "noise generator: the two gated levels are audibly distinct");
}

void Ay8910EnvelopeGeneratorFollowsAKnownShape() {
    std::printf("Ay8910EnvelopeGeneratorFollowsAKnownShape\n");

    Ay8910DeviceAdapter ay;
    auto buffers = MakeChannelBuffers();

    // Channel A in envelope mode (AVOL bit 4 = M), noise excluded so the
    // envelope's own volume alone gates the output.
    WriteReg(ay, kRegEnable, 0x08);
    WriteReg(ay, kRegAVol, 0x10);  // M=1 (envelope mode), fixed-volume bits ignored
    WriteReg(ay, kRegEnvFine, 6);
    WriteReg(ay, kRegEnvCoarse, 0);  // envelope period = 6 -> 6*2 = 12 samples per step
    WriteReg(ay, kRegEnvShape, 0x0C);  // repeating rising sawtooth (this file's own header comment)

    Check(ay.EnvelopeVolumeForTest() == 0, "envelope generator: volume starts at 0 right after the shape write");

    ay.Render(11 * Ay8910DeviceAdapter::kInputClocksPerSample, buffers);
    Check(ay.EnvelopeVolumeForTest() == 0, "envelope generator: volume unchanged one sample before the first step boundary (11 < 12)");

    ay.Render(1 * Ay8910DeviceAdapter::kInputClocksPerSample, buffers);
    Check(ay.EnvelopeVolumeForTest() == 1, "envelope generator: volume steps to 1 exactly at the 12th sample");

    ay.Render(12 * Ay8910DeviceAdapter::kInputClocksPerSample, buffers);
    Check(ay.EnvelopeVolumeForTest() == 2, "envelope generator: volume steps to 2 exactly 12 samples later (one full period)");

    // Advance through the remaining 14 decrements of this cycle (step 13 ->
    // 0, then the wrap step=-1 -> 15) to confirm the sawtooth actually
    // repeats rather than saturating or stopping (hold=0 per shape 0x0C).
    // Decrement K happens at sample K*12 and leaves step=15-K (mod 16 at the
    // wrap); we're at K=2 (sample 24, step 13), so 14 more decrements
    // (K=3..16) = 14*12 = 168 more samples reaches the K=16 wrap.
    ay.Render(14 * 12 * Ay8910DeviceAdapter::kInputClocksPerSample, buffers);
    Check(ay.EnvelopeVolumeForTest() == 0, "envelope generator: the 16-step sawtooth wraps back to 0 and keeps repeating (hold=0)");
}

void Ay8910ChannelOutputReachesPerChannelBuffers() {
    std::printf("Ay8910ChannelOutputReachesPerChannelBuffers\n");

    Ay8910DeviceAdapter ay;
    auto buffers = MakeChannelBuffers();

    WriteReg(ay, kRegEnable, 0x08);  // channel A: tone on, noise excluded
    WriteReg(ay, kRegAVol, 0x0F);
    WriteReg(ay, kRegAFine, 6);
    WriteReg(ay, kRegACoarse, 0);
    // Channels B/C are left at their power-on volume (0) - inert regardless
    // of their own tone/noise/envelope state.

    const std::uint64_t clocks = 200 * Ay8910DeviceAdapter::kInputClocksPerSample;
    const std::size_t produced = ay.Render(clocks, buffers);
    Check(produced == 200, "channel output: Render() produced one sample per kInputClocksPerSample input clocks");
    for (std::uint32_t ch = 0; ch < Ay8910DeviceAdapter::kChannelCount; ++ch) {
        Check(buffers[ch].size() == 200, "channel output: every channel's buffer received every sample");
    }

    bool channelAToggled = false;
    for (std::size_t i = 1; i < buffers[0].size(); ++i) {
        if (buffers[0][i] != buffers[0][0]) {
            channelAToggled = true;
            break;
        }
    }
    Check(channelAToggled, "channel output: the programmed channel A produced an oscillating (audible) signal");

    for (std::uint32_t ch = 1; ch < Ay8910DeviceAdapter::kChannelCount; ++ch) {
        bool constant = true;
        for (float sample : buffers[ch]) {
            if (sample != buffers[ch][0]) {
                constant = false;
                break;
            }
        }
        Check(constant, "channel output: an un-programmed channel (volume 0) stayed at a constant (inert) level");
    }
}

void Ay8910LegacyNormalizedMixHasNoDcOffsetAndMatchesMameLevels() {
    std::printf("Ay8910LegacyNormalizedMixHasNoDcOffsetAndMatchesMameLevels\n");

    // ay1 in nemesis.cpp (AY8910_LEGACY_OUTPUT | AY8910_SINGLE_OUTPUT): silence
    // is exactly 0 (normalised, unlike the default resistor-ladder model's ~0.059
    // "off" voltage) and one channel at full volume reaches ~1.47 (build_3D_table()
    // normalised 3.0 scale, evaluated for (15,0,0) with the AY-3-8910 constants).
    Ay8910DeviceAdapter ay;
    ay.SetLegacyNormalizedMixOutput(true);
    auto silent = MakeChannelBuffers();
    ay.Render(50 * Ay8910DeviceAdapter::kInputClocksPerSample, silent);
    float silentMax = 0.0f;
    for (const auto& buf : silent) for (float s : buf) silentMax = std::max(silentMax, std::fabs(s));
    Check(silentMax == 0.0f, "legacy mix: all channels at volume 0 output exactly 0 (no DC offset)");

    // Channel A: tone on, noise excluded, short period -> oscillates between 0 and
    // the full-volume value; take the peak over the run.
    WriteReg(ay, kRegEnable, 0x08);
    WriteReg(ay, kRegAVol, 0x0F);
    WriteReg(ay, kRegAFine, 6);
    WriteReg(ay, kRegACoarse, 0);
    auto loud = MakeChannelBuffers();
    ay.Render(200 * Ay8910DeviceAdapter::kInputClocksPerSample, loud);
    float peak = 0.0f;
    for (float s : loud[0]) peak = std::max(peak, s);
    Check(std::fabs(peak - 1.4699f) < 0.01f,
          "legacy mix: one channel at full volume matches MAME's normalised 3-channel network value (~1.47)");
    Check(loud[1].back() == 0.0f && loud[2].back() == 0.0f,
          "legacy mix: channels at volume 0 receive no share of the combined level");

    // ay2 (no set_flags() -> MAME default AY8910_LEGACY_OUTPUT, separate streams): full-volume
    // swing is 0.5 (build_single_table normalize=1), silence exactly 0.
    Ay8910DeviceAdapter ay2;
    ay2.SetLegacyNormalizedSeparateOutput(true);
    WriteReg(ay2, kRegEnable, 0x08);
    WriteReg(ay2, kRegAVol, 0x0F);
    WriteReg(ay2, kRegAFine, 6);
    WriteReg(ay2, kRegACoarse, 0);
    auto sep = MakeChannelBuffers();
    ay2.Render(200 * Ay8910DeviceAdapter::kInputClocksPerSample, sep);
    float sepPeak = 0.0f;
    for (float s : sep[0]) sepPeak = std::max(sepPeak, s);
    Check(std::fabs(sepPeak - 0.5f) < 1e-4f, "separate normalised output: full-volume swing is 0.5 (MAME build_single_table)");
    Check(sep[1].back() == 0.0f, "separate normalised output: a volume-0 channel is exactly 0");
}

void Ay8910RenderIsDeterministicAndLosesNoChipTime() {
    std::printf("Ay8910RenderIsDeterministicAndLosesNoChipTime\n");

    auto run = [](std::vector<float>& out) {
        Ay8910DeviceAdapter ay;
        auto buffers = MakeChannelBuffers();
        WriteReg(ay, kRegEnable, 0x08);
        WriteReg(ay, kRegAVol, 0x0F);
        WriteReg(ay, kRegAFine, 6);
        WriteReg(ay, kRegACoarse, 0);
        // Same total chip time, fed in uneven chunks that don't land on
        // kInputClocksPerSample (8): the partial-clock carry must reproduce
        // the single-call result.
        for (std::uint64_t chunk : {std::uint64_t{20}, std::uint64_t{7}, std::uint64_t{500}, std::uint64_t{3},
                                    std::uint64_t{130}}) {
            ay.Render(chunk, buffers);
        }
        out = buffers[0];
    };

    std::vector<float> a;
    std::vector<float> b;
    run(a);
    run(b);
    Check(!a.empty() && a == b, "determinism: identical register + timing sequence yields identical samples");
    // 20 + 7 + 500 + 3 + 130 = 660 input clocks -> 82 whole samples, 4 carried.
    Check(a.size() == 82, "determinism: leftover input clocks are carried, not dropped or double-counted");
}
