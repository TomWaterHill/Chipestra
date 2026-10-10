// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// K005289DeviceAdapter native tests. All register writes and PROM
// content below are hand-written synthetic stimulus - no real ROM data.
//
// Expected waveform-advance timing is derived independently from the public
// port of k005289.cpp's own algorithm (this adapter's own header/source
// comments): the counter starts at 0 (power-on), so the very first
// TickOneSample() call always underflows immediately regardless of the
// programmed frequency - the first output sample already reflects one
// waveform-position advance. From there, the waveform position advances
// every (frequency+1) samples. One K005289DeviceAdapter output sample ==
// kInputClocksPerSample (1) input clock.

#include "k005289_adapter_tests.h"

#include "devices/sound/k005289/k005289_device_adapter.h"
#include "test_support.h"

#include <cstdint>
#include <vector>

namespace {

std::vector<std::vector<float>> MakeVoiceBuffers() {
    return std::vector<std::vector<float>>(K005289DeviceAdapter::kVoiceCount);
}

}  // namespace

void K005289RegisterSemanticsUpdateInternalState() {
    std::printf("K005289RegisterSemanticsUpdateInternalState\n");
    K005289DeviceAdapter k005289({});

    // Test ControlA/ControlB volume setting
    k005289.ControlA(0x0F);  // volume 15
    Check(k005289.VolumeForTest(0) == 15, "register semantics: ControlA sets volume correctly");
    k005289.ControlB(0x0F);  // volume 15
    Check(k005289.VolumeForTest(1) == 15, "register semantics: ControlB sets volume correctly");

    // Test ControlA/ControlB waveform high bits
    k005289.ControlA(0xE0);  // waveform high bits 7,6,5 set
    Check((k005289.WaveformForTest(0) & 0xE0) == 0xE0, "register semantics: ControlA sets waveform high bits correctly");
    k005289.ControlB(0xE0);  // waveform high bits 7,6,5 set
    Check((k005289.WaveformForTest(1) & 0xE0) == 0xE0, "register semantics: ControlB sets waveform high bits correctly");

    // Test Ld1/Ld2 pitch setting (both offsets require zero arithmetic:
    // 0x0FFF - 0 = 0x0FFF unchanged; 0x0FFF - 0x0FFF = 0)
    k005289.Ld1(0x000, 0);  // pitch = 0x0FFF
    Check(k005289.PitchForTest(0) == 0x0FFF, "register semantics: Ld1 with offset 0x000 sets pitch correctly");
    k005289.Ld1(0x0FFF, 0);  // pitch = 0x0000
    Check(k005289.PitchForTest(0) == 0x0000, "register semantics: Ld1 with offset 0x0FFF sets pitch correctly");
    k005289.Ld2(0x000, 0);  // pitch = 0x0FFF
    Check(k005289.PitchForTest(1) == 0x0FFF, "register semantics: Ld2 with offset 0x000 sets pitch correctly");
    k005289.Ld2(0x0FFF, 0);  // pitch = 0x0000
    Check(k005289.PitchForTest(1) == 0x0000, "register semantics: Ld2 with offset 0x0FFF sets pitch correctly");

    // Test Tg1/Tg2 frequency update - Ld1/Ld2 alone must not touch frequency
    k005289.Ld1(0x000, 0);  // set pitch 0x0FFF
    k005289.Tg1(0);         // copy pitch to frequency
    Check(k005289.FrequencyForTest(0) == 0x0FFF, "register semantics: Tg1 copies pitch to frequency");
    k005289.Ld1(0x0FFF, 0);  // set new pitch 0x0000
    Check(k005289.PitchForTest(0) == 0x0000, "register semantics: Ld1 updates pitch without affecting frequency");
    Check(k005289.FrequencyForTest(0) == 0x0FFF, "register semantics: frequency not updated until Tg1 called again");
    k005289.Tg1(0);  // copy new pitch to frequency
    Check(k005289.FrequencyForTest(0) == 0x0000, "register semantics: Tg1 updates frequency with new pitch");

    k005289.Ld2(0x000, 0);  // set pitch 0x0FFF
    k005289.Tg2(0);         // copy pitch to frequency
    Check(k005289.FrequencyForTest(1) == 0x0FFF, "register semantics: Tg2 copies pitch to frequency");
    k005289.Ld2(0x0FFF, 0);  // set new pitch 0x0000
    Check(k005289.PitchForTest(1) == 0x0000, "register semantics: Ld2 updates pitch without affecting frequency");
    Check(k005289.FrequencyForTest(1) == 0x0FFF, "register semantics: frequency not updated until Tg2 called again");
    k005289.Tg2(0);  // copy new pitch to frequency
    Check(k005289.FrequencyForTest(1) == 0x0000, "register semantics: Tg2 updates frequency with new pitch");
}

void K005289WaveformGenerationMatchesProgrammedFrequencyAndProm() {
    std::printf("K005289WaveformGenerationMatchesProgrammedFrequencyAndProm\n");

    // Voice 0's PROM half (bytes 0-255): only positions 1/2/3 matter for
    // this test (see this file's own header comment on why the very first
    // output sample already reflects waveform position 1, not 0). Chosen
    // nibble values give exactly-representable float sample values
    // (denominators are powers of two) so the assertions below can use
    // exact equality rather than a tolerance.
    std::vector<std::uint8_t> prom(K005289DeviceAdapter::kWaveformPromTotalBytes, 0);
    prom[1] = 0x00;  // nibble 0  -> (0-8)*15/512
    prom[2] = 0x0F;  // nibble 15 -> (15-8)*15/512 (k005289.cpp add_int(..., 512))
    prom[3] = 0x08;  // nibble 8  -> 0

    K005289DeviceAdapter k005289(prom);
    auto buffers = MakeVoiceBuffers();

    k005289.ControlA(0x0F);  // volume 15 (max), waveform high bits 0 (stay within positions 0-31)
    // pitch = 9 (0x0FFF - 0x0FF6 = 9, GradiusMemoryMap-independent synthetic
    // stimulus - this adapter has no address-decode window of its own to
    // route through here, Ld1 is called directly).
    k005289.Ld1(0x0FF6, 0);
    k005289.Tg1(0);  // frequency = 9 -> waveform advances every 9+1 = 10 samples

    const std::size_t produced = k005289.Render(25, buffers);
    Check(produced == 25, "waveform generation: Render() produced one sample per kInputClocksPerSample input clock");
    Check(buffers[0].size() == 25, "waveform generation: voice 0's buffer received every sample");

    // Samples 0-9 (10 samples): waveform position 1. Samples 10-19 (10
    // samples): position 2. Samples 20-24 (5 samples, block ends mid-run):
    // position 3. Derivation in this file's own header comment.
    bool phase1Ok = true, phase2Ok = true, phase3Ok = true;
    for (std::size_t i = 0; i <= 9; ++i) phase1Ok = phase1Ok && (buffers[0][i] == -8.0f * 15.0f / 512.0f);
    for (std::size_t i = 10; i <= 19; ++i) phase2Ok = phase2Ok && (buffers[0][i] == 7.0f * 15.0f / 512.0f);
    for (std::size_t i = 20; i <= 24; ++i) phase3Ok = phase3Ok && (buffers[0][i] == 0.0f);
    Check(phase1Ok, "waveform generation: samples 0-9 match PROM position 1 (period = frequency+1 = 10 samples)");
    Check(phase2Ok, "waveform generation: samples 10-19 match PROM position 2 (waveform advanced exactly once)");
    Check(phase3Ok, "waveform generation: samples 20-24 match PROM position 3 (waveform advanced a second time)");
}

void K005289ChannelOutputReachesPerVoiceBuffers() {
    std::printf("K005289ChannelOutputReachesPerVoiceBuffers\n");

    // Voice 0's PROM half alternates 0x00/0x0F across the first 32 bytes
    // (the only positions the 5-bit waveform counter can ever address) so
    // the rendered signal actually varies as the waveform position
    // advances - an all-zero PROM would produce a constant sample
    // regardless of internal waveform stepping, which would prove nothing.
    std::vector<std::uint8_t> prom(K005289DeviceAdapter::kWaveformPromTotalBytes, 0);
    for (std::size_t i = 0; i < 32; ++i) {
        prom[i] = (i % 2 == 0) ? 0x00 : 0x0F;
    }

    K005289DeviceAdapter k005289(prom);
    auto buffers = MakeVoiceBuffers();

    k005289.ControlA(0x0F);  // voice 0: volume 15, waveform high bits 0
    k005289.Ld1(0x0FF9, 0);  // pitch = 0x0FFF - 0x0FF9 = 6 -> period 7 samples
    k005289.Tg1(0);
    // Voice 1 is left untouched (volume 0, ControlB never called) - its
    // sample must stay exactly 0.0f regardless of PROM content, the same
    // "un-programmed channel is inert" pattern Ay8910ChannelOutputReaches
    // PerChannelBuffers established.

    const std::size_t clocks = 200;
    const std::size_t produced = k005289.Render(clocks, buffers);
    Check(produced == clocks, "channel output: Render() produced one sample per kInputClocksPerSample input clock");
    for (std::uint32_t voice = 0; voice < K005289DeviceAdapter::kVoiceCount; ++voice) {
        Check(buffers[voice].size() == clocks, "channel output: every voice's buffer received every sample");
    }

    bool voice0Toggled = false;
    for (std::size_t i = 1; i < buffers[0].size(); ++i) {
        if (buffers[0][i] != buffers[0][0]) {
            voice0Toggled = true;
            break;
        }
    }
    Check(voice0Toggled, "channel output: the programmed voice 0 produced an oscillating (audible) signal");

    bool voice1Constant = true;
    for (float sample : buffers[1]) {
        if (sample != 0.0f) {
            voice1Constant = false;
            break;
        }
    }
    Check(voice1Constant, "channel output: the un-programmed voice 1 (volume 0) stayed silent the entire render");
}
