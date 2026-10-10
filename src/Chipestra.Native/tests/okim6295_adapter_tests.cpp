// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Okim6295DeviceAdapter native tests. All ROM data below is hand-written synthetic bytes (no real ROM/game
// data) - same "not ROM-dependent" convention as k005289_adapter_tests.cpp/msm5205_adapter_tests.cpp. Expected values
// (phrase-table layout, sample counts, divisor) are derived independently from the OKIM6295 command protocol as
// documented in MAME's src/devices/sound/okim6295.cpp/okiadpcm.cpp, not by reading back what this adapter
// itself computes.

#include "okim6295_adapter_tests.h"

#include "devices/sound/okim6295/okim6295_device_adapter.h"
#include "test_support.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

void Okim6295ResetAndIdleStatus() {
    std::printf("Okim6295ResetAndIdleStatus\n");
    std::vector<std::uint8_t> rom = {};
    Okim6295DeviceAdapter adapter(rom);
    adapter.Reset();
    Check(adapter.Read() == 0xF0, "Read() should return 0xF0 when no voice is playing");
    Check(adapter.Divisor() == 132, "Divisor should be 132 for default Pin7::High");
}

void Okim6295Pin7LowUsesDivisor165() {
    std::printf("Okim6295Pin7LowUsesDivisor165\n");
    std::vector<std::uint8_t> rom = {};
    Okim6295DeviceAdapter adapter(rom, Okim6295DeviceAdapter::Pin7::Low);
    Check(adapter.Divisor() == 165, "Divisor should be 165 for Pin7::Low");
}

void Okim6295StartAndStopVoice() {
    std::printf("Okim6295StartAndStopVoice\n");
    std::vector<std::uint8_t> rom = {0x00, 0x00, 0x08, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x77, 0x77, 0x77, 0x00, 0x00, 0x00, 0x00, 0x00};
    Okim6295DeviceAdapter adapter(rom);
    adapter.Write(0x80); // start phrase 0
    Check(!adapter.PlayingForTest(0), "Voice 0 should not be playing yet");
    adapter.Write(0x10); // second half: voice 0, attenuation 0
    Check(adapter.PlayingForTest(0) == true, "Voice 0 should be playing");
    Check(adapter.SampleCountForTest(0) == 6, "Voice 0 should have 6 samples");
    Check(adapter.Read() == 0xF1, "Read() should return 0xF1 when voice 0 is playing");
    adapter.Write(0x08); // stop command for voice 0
    Check(adapter.PlayingForTest(0) == false, "Voice 0 should not be playing anymore");
    Check(adapter.Read() == 0xF0, "Read() should return 0xF0 when no voice is playing");
}

void Okim6295InvalidStartStopIsIgnored() {
    std::printf("Okim6295InvalidStartStopIsIgnored\n");
    std::vector<std::uint8_t> rom = {0x00, 0x00, 0x0A, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    Okim6295DeviceAdapter adapter(rom);
    adapter.Write(0x80);
    adapter.Write(0x10);
    Check(adapter.PlayingForTest(0) == false, "Invalid start/stop should be ignored");
    Check(adapter.Read() == 0xF0, "Read() should return 0xF0 when no voice is playing");
}

void Okim6295AlreadyPlayingVoiceIgnoresNewStart() {
    std::printf("Okim6295AlreadyPlayingVoiceIgnoresNewStart\n");
    std::vector<std::uint8_t> rom = {
        0x00, 0x00, 0x10, 0x00, 0x00, 0x14, // phrase 0: start=16, stop=20 (10 nibbles)
        0x00, 0x00, 0x10, 0x00, 0x00, 0x11, // phrase 1: start=16, stop=17 (4 nibbles)
        0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77
    };
    Okim6295DeviceAdapter adapter(rom);
    adapter.Write(0x80);
    adapter.Write(0x10); // start phrase 0 on voice 0
    Check(adapter.SampleCountForTest(0) == 10, "Voice 0 should have 10 samples");
    adapter.Write(0x81);
    adapter.Write(0x10); // try to start phrase 1 on voice 0, but it's already playing
    Check(adapter.SampleCountForTest(0) == 10, "Voice 0 should still have 10 samples (ignored)");
}

void Okim6295RenderProducesOneSamplePerDivisorClocks() {
    std::printf("Okim6295RenderProducesOneSamplePerDivisorClocks\n");
    std::vector<std::uint8_t> rom = {0x00, 0x00, 0x08, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x77, 0x77, 0x77, 0x00, 0x00, 0x00, 0x00, 0x00};
    Okim6295DeviceAdapter adapter(rom);
    adapter.Write(0x80);
    adapter.Write(0x10);
    std::vector<std::vector<float>> voices(4);
    std::size_t samples = adapter.Render(131, voices);
    Check(samples == 0, "Should not produce samples before divisor");
    for (auto& v : voices) {
        Check(v.empty(), "All voices should be empty before divisor");
    }
    samples = adapter.Render(1, voices);
    Check(samples == 1, "Should produce one sample after crossing divisor");
    Check(voices[0].size() == 1, "Voice 0 should have one sample");
    Check(voices[1].size() == 1, "Voice 1 should have one sample");
    Check(voices[1].back() == 0.0f, "Voice 1 should be silent");
    Check(voices[0].back() != 0.0f, "Voice 0 should have non-zero sample");
}

void Okim6295VoicePlaysExactSampleCountThenStops() {
    std::printf("Okim6295VoicePlaysExactSampleCountThenStops\n");
    std::vector<std::uint8_t> rom = {0x00, 0x00, 0x08, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x77, 0x77, 0x77, 0x00, 0x00, 0x00, 0x00, 0x00};
    Okim6295DeviceAdapter adapter(rom);
    adapter.Write(0x80);
    adapter.Write(0x10);
    std::vector<std::vector<float>> voices(4);
    adapter.Render(6 * 132, voices);
    Check(!adapter.PlayingForTest(0), "Voice 0 should stop after playing all samples");
    Check(voices[0].size() == 6, "Voice 0 should have 6 samples");
}

void Okim6295MultipleVoicesIndependent() {
    std::printf("Okim6295MultipleVoicesIndependent\n");
    std::vector<std::uint8_t> rom = {0x00, 0x00, 0x10, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x77, 0x77, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    Okim6295DeviceAdapter adapter(rom);
    adapter.Write(0x80);
    adapter.Write(0x30); // start voices 0 and 1
    Check(adapter.PlayingForTest(0) == true, "Voice 0 should be playing");
    Check(adapter.PlayingForTest(1) == true, "Voice 1 should be playing");
    Check(adapter.PlayingForTest(2) == false, "Voice 2 should not be playing");
    Check(adapter.PlayingForTest(3) == false, "Voice 3 should not be playing");
    adapter.Write(0x18); // stop voices 0 and 1
    Check(adapter.PlayingForTest(0) == false, "Voice 0 should not be playing anymore");
    Check(adapter.PlayingForTest(1) == false, "Voice 1 should not be playing anymore");
}

void Okim6295VolumeAttenuationReducesAmplitude() {
    std::printf("Okim6295VolumeAttenuationReducesAmplitude\n");
    std::vector<std::uint8_t> rom = {0x00, 0x00, 0x08, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x77, 0x77, 0x77, 0x00, 0x00, 0x00, 0x00, 0x00};
    Okim6295DeviceAdapter adapter1(rom);
    Okim6295DeviceAdapter adapter2(rom);
    adapter1.Write(0x80);
    adapter1.Write(0x10); // attenuation 0 (loudest)
    adapter2.Write(0x80);
    // MAME's volume table has 9 audible steps (indices 0-8, index 8 = 0x02/0x20 - quiet but not zero); only
    // index 9 and beyond are silent (okim6295.cpp's "only 9 steps" comment).
    adapter2.Write(0x19);  // attenuation 9 (the first fully silent step)
    std::vector<std::vector<float>> voices1(4);
    std::vector<std::vector<float>> voices2(4);
    adapter1.Render(132, voices1);
    adapter2.Render(132, voices2);
    Check(std::abs(voices1[0].back()) > std::abs(voices2[0].back()), "Louder voice should have larger amplitude");
    Check(voices2[0].back() == 0.0f, "Quiet voice should be silent");
}

void Okim6295ResetDoesNotClearPendingPhraseHalf() {
    std::printf("Okim6295ResetDoesNotClearPendingPhraseHalf\n");
    std::vector<std::uint8_t> rom = {0x00, 0x00, 0x08, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x77, 0x77, 0x77, 0x00, 0x00, 0x00, 0x00, 0x00};
    Okim6295DeviceAdapter adapter(rom);
    adapter.Write(0x80); // start phrase 0
    adapter.Reset();
    adapter.Write(0x10); // complete the original phrase
    Check(adapter.PlayingForTest(0) == true, "Phrase should still be completed after reset");
}

void Okim6295DeterministicAcrossInstances() {
    std::printf("Okim6295DeterministicAcrossInstances\n");
    std::vector<std::uint8_t> rom = {0x00, 0x00, 0x08, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x77, 0x77, 0x77, 0x00, 0x00, 0x00, 0x00, 0x00};
    Okim6295DeviceAdapter adapter1(rom);
    Okim6295DeviceAdapter adapter2(rom);
    adapter1.Write(0x80);
    adapter1.Write(0x10);
    adapter2.Write(0x80);
    adapter2.Write(0x10);
    std::vector<std::vector<float>> voices1(4);
    std::vector<std::vector<float>> voices2(4);
    adapter1.Render(1000, voices1);
    adapter2.Render(1000, voices2);
    Check(voices1 == voices2, "Both instances should produce identical output");
}
