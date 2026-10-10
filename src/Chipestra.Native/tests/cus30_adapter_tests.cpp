// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "cus30_adapter_tests.h"

#include "devices/sound/cus30/cus30_device_adapter.h"
#include "test_support.h"

#include <cmath>
#include <vector>

void Cus30WaveRamIsPlainReadWrite() {
    Cus30DeviceAdapter cus30;
    cus30.Write8(0x000, 0xAB);
    cus30.Write8(0x0FF, 0xCD);
    Check(cus30.Read8(0x000) == 0xAB, "waveram offset 0x000 round-trips");
    Check(cus30.Read8(0x0FF) == 0xCD, "waveram offset 0x0FF round-trips");
}

void Cus30VoiceRegisterDecodeSetsFrequencyVolumeWaveformSelect() {
    Cus30DeviceAdapter cus30;
    cus30.Write8(0x100 + 0, 0x0A);  // voice 0 left volume
    cus30.Write8(0x100 + 1, 0x53);  // waveform select 0x5, freq bits[19:16] = 0x3
    cus30.Write8(0x100 + 2, 0x12);  // freq bits[15:8]
    cus30.Write8(0x100 + 3, 0xAB);  // freq bits[7:0]
    Check(cus30.VoiceVolumeLeft(0) == 0x0A, "voice 0 left volume decoded");
    Check(cus30.WaveformSelectForTest(0) == 0x05, "voice 0 waveform select decoded (high nibble of +0x01)");
    Check(cus30.FrequencyForTest(0) == 0x312ABu, "voice 0 20-bit frequency assembled from +0x01(lo nibble)/+0x02/+0x03");
}

void Cus30Register04SetsNextVoiceNoiseSw() {
    Cus30DeviceAdapter cus30;
    cus30.Write8(0x100 + 0 * 8 + 4, 0x80 | 0x07);  // voice 0 right volume=7, bit7 set
    Check(cus30.VoiceVolumeRight(0) == 0x07, "voice 0 right volume decoded from the same +0x04 write");
    Check(cus30.NoiseSwForTest(1), "bit7 of voice 0's +0x04 write sets voice 1's noise_sw (namco.cpp cross-voice side effect)");
    Check(!cus30.NoiseSwForTest(0), "voice 0's own noise_sw is untouched by its own +0x04 write");
}

void Cus30Register04WrapsFromVoice7ToVoice0() {
    Cus30DeviceAdapter cus30;
    cus30.Write8(0x100 + 7 * 8 + 4, 0x80);  // voice 7's +0x04, bit7 set
    Check(cus30.NoiseSwForTest(0), "voice 7's +0x04 write wraps (7+1)&7=0, setting voice 0's noise_sw");
}

void Cus30SharedRamIsPlainReadWrite() {
    Cus30DeviceAdapter cus30;
    cus30.Write8(0x140, 0x11);
    cus30.Write8(0x3FF, 0x22);
    Check(cus30.Read8(0x140) == 0x11, "shared-RAM offset 0x140 round-trips");
    Check(cus30.Read8(0x3FF) == 0x22, "shared-RAM offset 0x3FF round-trips");
}

void Cus30Register5ReturnsLiveCounterPosition() {
    Cus30DeviceAdapter cus30;
    cus30.Write8(0x100 + 0, 0x0F);  // voice 0 left volume (active)
    cus30.Write8(0x100 + 3, 0xFF);  // small frequency
    std::vector<std::vector<float>> samples;
    cus30.Render(4, samples);
    const std::uint8_t livePosition = cus30.Read8(0x100 + 0 * 8 + 5);
    const std::uint8_t expected = static_cast<std::uint8_t>((cus30.CounterForTest(0) >> 15) & 0x1F);
    Check(livePosition == expected, "register+5 read returns the live waveform position (counter>>15)&0x1F");
}

void Cus30RenderIsSilentWhenBothVolumesZero() {
    Cus30DeviceAdapter cus30;
    cus30.Write8(0x100 + 1, 0xF0);  // waveform select 15 (irrelevant - voice is silent)
    cus30.Write8(0x100 + 3, 0xFF);  // nonzero frequency
    std::vector<std::vector<float>> samples;
    cus30.Render(8, samples);
    bool allZero = true;
    for (float s : samples[0]) {
        if (s != 0.0f) allZero = false;
    }
    Check(allZero, "a voice with both volumes at 0 renders pure silence regardless of frequency/waveform select");
    Check(cus30.CounterForTest(0) == 0, "counter never advances while both volumes are 0");
}

void Cus30RenderCounterFreezesWhileSilent() {
    Cus30DeviceAdapter cus30;
    cus30.Write8(0x100 + 3, 0xFF);  // nonzero frequency, both volumes still 0
    std::vector<std::vector<float>> samples;
    cus30.Render(16, samples);
    Check(cus30.CounterForTest(0) == 0, "real hardware freezes the phase counter while silent (namco.cpp's own `if(lv||rv)` guard)");
}

void Cus30RenderProducesNonzeroSampleWhenActive() {
    Cus30DeviceAdapter cus30;
    cus30.Write8(0x000, 0xF0);      // waveram byte 0, high nibble 0xF -> nibble-8 = +7 (this is voice 0's first sample)
    cus30.Write8(0x100 + 0, 0x0F);  // voice 0 left volume, active
    cus30.Write8(0x100 + 3, 0xFF);  // nonzero frequency so the counter advances on later samples
    std::vector<std::vector<float>> samples;
    cus30.Render(1, samples);
    Check(std::fabs(samples[0][0] - 0.875f) < 0.001f,
          "an active voice's first sample reads waveram byte 0's high nibble (15-8=7, normalized 7/8=0.875)");
}

void Cus30ResetClearsVoiceStateAndMemory() {
    Cus30DeviceAdapter cus30;
    cus30.Write8(0x000, 0xAB);
    cus30.Write8(0x140, 0xCD);
    cus30.Write8(0x100 + 0, 0x0F);
    cus30.Write8(0x100 + 1, 0xF0);
    cus30.Write8(0x100 + 4, 0x8F);

    cus30.Reset();

    Check(cus30.Read8(0x000) == 0, "Reset() clears waveram");
    Check(cus30.Read8(0x140) == 0, "Reset() clears shared RAM");
    Check(cus30.VoiceVolumeLeft(0) == 0, "Reset() clears voice 0 left volume");
    Check(cus30.WaveformSelectForTest(0) == 0, "Reset() clears voice 0 waveform select");
    Check(!cus30.NoiseSwForTest(1), "Reset() clears the cross-voice noise_sw side effect");
    Check(cus30.FrequencyForTest(0) == 0, "Reset() clears voice 0 frequency");
}
