// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Vlm5030DeviceAdapter native tests. All register writes and voice
// data below are hand-written synthetic stimulus - no real ROM data.

#include "vlm5030_adapter_tests.h"

#include "devices/sound/vlm5030/vlm5030_device_adapter.h"
#include "test_support.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

void Vlm5030RegisterAndPinSemanticsUpdateInternalState() {
    std::printf("Vlm5030RegisterAndPinSemanticsUpdateInternalState\n");
    Vlm5030DeviceAdapter vlm5030([](std::uint16_t) -> std::uint8_t { return 0; });
    vlm5030.Reset();
    Check(!vlm5030.BusyPin(), "register/pin semantics: after construction and reset, BusyPin() is false");

    vlm5030.DataW(0x03);
    vlm5030.RstPin(true);
    vlm5030.RstPin(false);
    Check(vlm5030.InterpStepForTest() == 4, "register/pin semantics: RstPin falling edge latches correct interp step");
    Check(vlm5030.FrameSizeForTest() == 40, "register/pin semantics: RstPin falling edge latches correct frame size");
    Check(vlm5030.PitchOffsetForTest() == 0, "register/pin semantics: RstPin falling edge latches correct pitch offset");

    vlm5030.DataW(0x28);
    vlm5030.RstPin(true);
    vlm5030.RstPin(false);
    Check(vlm5030.InterpStepForTest() == 1, "register/pin semantics: RstPin falling edge latches correct interp step");
    Check(vlm5030.FrameSizeForTest() == 60, "register/pin semantics: RstPin falling edge latches correct frame size");
    Check(vlm5030.PitchOffsetForTest() == 0, "register/pin semantics: RstPin falling edge latches correct pitch offset");

    vlm5030.StPin(false);
    vlm5030.StPin(true);
    Check(vlm5030.PhaseForTest() == 2, "register/pin semantics: StPin rising edge sets phase to 2 (Setup)");
    Check(vlm5030.BusyPin(), "register/pin semantics: StPin rising edge makes BusyPin true");

    vlm5030.VcuPin(true);
    vlm5030.DataW(0x5A);
    vlm5030.StPin(true);
    vlm5030.StPin(false);
    Check(vlm5030.VcuAddrHForTest() == 0x5A01, "register/pin semantics: StPin falling edge with VcuPin=true sets VcuAddrH correctly");

    vlm5030.Reset();
    vlm5030.RstPin(true);
    vlm5030.RstPin(false);
    vlm5030.RstPin(true);
    std::uint8_t phase_before = vlm5030.PhaseForTest();
    vlm5030.RstPin(true);
    Check(vlm5030.PhaseForTest() == phase_before, "register/pin semantics: RstPin(true) while BusyPin() is false is a no-op");
}

namespace {

// A tiny hand-built "voiceram" image (this file's own header comment - no
// real ROM/speech data, just synthetic bit-packed frames this test itself
// derived): byte[0..1] is the indirect-address table entry (big-endian,
// value 0x0002) StPin's falling edge resolves through when VcuPin is
// clear and DataW(0) was latched beforehand (table = (0 & 0xFE) +
// ((0&1)<<8) = 0); byte[2..7] and byte[8..13] are two back-to-back copies
// of the same 48-bit speech frame; byte[14] is the end-of-speech marker
// (0x03 - cmd bit0=1 "extend", bit1=1 "end").
//
// The frame's first 11 bits (cmd + pitch + energy; ParseFrame()'s own
// GetBits(1, 5) / GetBits(6, 5) calls) were chosen as a plain integer and
// packed by hand: cmd=0, pitch index=5, energy index=10 ->
// V = 0 | (5 << 1) | (10 << 6) = 10 + 640 = 650 = 0x028A (little-endian:
// low byte 0x8A, high byte 0x02). All higher bits (K1-K10's own 3-6 bit
// fields, GetBits(11..) onward) are left 0, giving K1 = kTableK1[0] = 390
// and K2-K10 = 0 (this adapter's own vlm5030_device_adapter.cpp - the K1
// table's own index-0 entry is not 0, unlike every other K table).
//
// Expected decode (from the coefficient tables in vlm5030_device_adapter.cpp,
// transcribed from MAME's src/devices/sound/tms5110r.hxx):
// pitchtable[5] = 25, energytable[10] = 15.
//
// Why the frame is repeated twice rather than once: Render()'s
// interpolation logic synthesizes each sample's gate from old_energy_ - the
// *previous* ParseFrame() call's energy, one full frame behind new_energy_ -
// and, when that gate is still 0 (as for the first frame after silence),
// forces target_energy_ back to 0 for that frame instead of ramping toward
// the newly-decoded energy. A single non-silent frame surrounded by silence
// therefore never reaches an audible current_energy_ under
// kFastInterpParam's "instant" (interp_step_=4) interpolation. A second
// consecutive copy gives the second ParseFrame() call a non-zero old_energy_
// and a matching new_energy_, which produces non-zero samples - a real
// property of the chip's interpolation design.
constexpr std::uint8_t kVoiceRam[] = {
    0x00, 0x02,                          // indirect table entry -> frame at offset 2
    0x8A, 0x02, 0x00, 0x00, 0x00, 0x00,  // frame A (V=650 packed little-endian, K bits 0)
    0x8A, 0x02, 0x00, 0x00, 0x00, 0x00,  // frame B (identical repeat, see comment above)
    0x03,                                // end of speech
};

std::uint8_t ReadKnownVoiceRam(std::uint16_t offset) {
    return offset < sizeof(kVoiceRam) ? kVoiceRam[offset] : 0;
}

// SetupParameter(0x02): bit1 set -> InterpStepForTest()==4 (fastest/
// "instant" interpolation - chosen specifically so ParseFrame() fires as
// early as possible, at the second frame_size_ boundary, rather than
// needing 4x as many samples with a slower interp_step_); bits3-5=0 ->
// FrameSizeForTest()==kSpeedTable[0]==40.
constexpr std::uint8_t kFastInterpParam = 0x02;

// Starts speech playback (indirect addressing mode, DataW(0) selects
// table entry 0) on a fresh adapter already carrying kFastInterpParam.
void StartKnownSpeech(Vlm5030DeviceAdapter& vlm5030) {
    vlm5030.DataW(kFastInterpParam);
    vlm5030.RstPin(true);
    vlm5030.RstPin(false);  // falling edge: latches the parameter

    vlm5030.VcuPin(false);
    vlm5030.DataW(0x00);
    vlm5030.StPin(true);
    vlm5030.StPin(false);  // falling edge, VCU clear: starts speech (indirect mode)
}

}  // namespace

void Vlm5030FrameParsingDecodesKnownBitPackedVoiceData() {
    std::printf("Vlm5030FrameParsingDecodesKnownBitPackedVoiceData\n");

    Vlm5030DeviceAdapter vlm5030(ReadKnownVoiceRam);
    StartKnownSpeech(vlm5030);
    Check(vlm5030.PhaseForTest() == 4, "frame parsing: StPin falling edge (indirect mode) starts playback (phase=Run)");

    // ParseFrame() first fires at the *second* frame_size_ (40-sample)
    // boundary - sample_count_ starts at frame_size_ (40, not 0) from
    // StPin, so boundary 1 lands at sample index 40 and, with
    // interp_step_=4 (kFastInterpParam), only decrements interp_count_
    // from 4 to 0 without calling ParseFrame() yet; boundary 2 (sample
    // index 80) is where interp_count_==0 is finally observed and
    // ParseFrame() actually runs, decoding this test's own frame. A
    // second real ParseFrame() call (at boundary 3, sample index 120)
    // would then hit the end-of-speech marker and zero new_energy_/
    // new_pitch_ again - rendering 100 samples (comfortably inside the
    // [81, 119] window either boundary could land on, allowing a few
    // samples of margin for this derivation) observes the decoded frame
    // stably, before that happens.
    std::vector<float> samples;
    vlm5030.Render(100 * Vlm5030DeviceAdapter::kInputClocksPerSample, samples);

    Check(vlm5030.NewEnergyForTest() == 15, "frame parsing: energy index 10 decodes to energytable[10] == 15");
    Check(vlm5030.NewPitchForTest() == 25, "frame parsing: pitch index 5 decodes to pitchtable[5] == 25");
}

void Vlm5030ChannelOutputReachesItsBuffer() {
    std::printf("Vlm5030ChannelOutputReachesItsBuffer\n");

    // An un-triggered adapter (never started via StPin) stays exactly
    // silent (a real chip's PH_IDLE state) but still owes the caller one
    // sample per kInputClocksPerSample of input clocks, matching every
    // other adapter's Render() contract (this class's own Render()
    // comment) - Render() must never return short just because the chip
    // has nothing to synthesize this call.
    {
        Vlm5030DeviceAdapter idle(ReadKnownVoiceRam);
        std::vector<float> samples;
        const std::size_t produced = idle.Render(50 * Vlm5030DeviceAdapter::kInputClocksPerSample, samples);
        Check(produced == 50, "channel output: an idle chip still produces one sample per kInputClocksPerSample");
        Check(samples.size() == 50, "channel output: the idle chip's buffer received every sample");
        bool allSilent = true;
        for (float s : samples) {
            if (s != 0.0f) {
                allSilent = false;
                break;
            }
        }
        Check(allSilent, "channel output: an idle (never-triggered) chip's output is exactly silent");
    }

    // A triggered adapter, rendered past the point where its second
    // decoded frame (kVoiceRam's own header comment explains why two
    // back-to-back copies of the frame are needed for audible output)
    // actually reaches a non-zero current_energy_: that frame is decoded
    // at sample index 120 (frame_size_=40's third boundary), and the
    // chip's periodic voiced excitation (pitch period 25, from this
    // frame's own pitchtable[5]=25) fires on the very next sample, index
    // 120 - so 150 samples leaves comfortable margin past that point
    // without yet reaching the following (silent, end-of-speech) frame's
    // boundary at sample index 160.
    {
        Vlm5030DeviceAdapter speaking(ReadKnownVoiceRam);
        StartKnownSpeech(speaking);
        std::vector<float> samples;
        const std::size_t clocks = 150 * Vlm5030DeviceAdapter::kInputClocksPerSample;
        const std::size_t produced = speaking.Render(clocks, samples);
        Check(produced == 150, "channel output: a speaking chip produces one sample per kInputClocksPerSample");
        Check(samples.size() == 150, "channel output: the speaking chip's buffer received every sample");
        bool anyNonSilent = false;
        for (float s : samples) {
            if (s != 0.0f) {
                anyNonSilent = true;
                break;
            }
        }
        Check(anyNonSilent, "channel output: a speaking chip eventually produces audible (non-zero) samples");
    }
}

namespace {

// Four back-to-back frames whose energy DROPS: two at energytable[24]=68, then two at energytable[4]=5 (V = 10 |
// (24<<6) = 0x060A, and 10 | (4<<6) = 0x010A, little-endian; K bits all 0). Decoding the drop makes the
// interpolator ramp a falling energy - the case that must not wrap around (see the test below).
constexpr std::uint8_t kFallingEnergyVoiceRam[] = {
    0x00, 0x02,                          // indirect table entry -> frames at offset 2
    0x0A, 0x06, 0x00, 0x00, 0x00, 0x00,  // energy index 24 (68)
    0x0A, 0x06, 0x00, 0x00, 0x00, 0x00,  // energy index 24 (68)
    0x0A, 0x01, 0x00, 0x00, 0x00, 0x00,  // energy index 4  (5)
    0x0A, 0x01, 0x00, 0x00, 0x00, 0x00,  // energy index 4  (5)
    0x03,                                // end of speech
};

std::uint8_t ReadFallingEnergyVoiceRam(std::uint16_t offset) {
    return offset < sizeof(kFallingEnergyVoiceRam) ? kFallingEnergyVoiceRam[offset] : 0;
}

}  // namespace

void Vlm5030FallingEnergyInterpolationStaysWithinTheFrameEnergies() {
    std::printf("Vlm5030FallingEnergyInterpolationStaysWithinTheFrameEnergies\n");
    // vlm5030.cpp keeps m_old_energy/m_target_energy as uint16_t, which C++ promotes to (signed) int in
    //   m_current_energy = m_old_energy + (m_target_energy - m_old_energy) * interp_effect / FR_SIZE;
    // so a falling energy interpolates DOWNWARD. Held as std::uint32_t, the same expression would wrap around to
    // ~1e9 whenever the energy fell, saturating the lattice filter with full-scale clipped noise.
    Vlm5030DeviceAdapter vlm5030(ReadFallingEnergyVoiceRam);
    vlm5030.DataW(0x00);  // interp_step 1 (2400bps: four interpolation steps per frame), frame size 40
    vlm5030.RstPin(true);
    vlm5030.RstPin(false);
    vlm5030.VcuPin(false);
    vlm5030.DataW(0x00);
    vlm5030.StPin(true);
    vlm5030.StPin(false);

    std::uint32_t maxEnergy = 0;
    float maxSample = 0.0f;
    bool sawHighEnergy = false;
    for (int i = 0; i < 1500; ++i) {
        std::vector<float> one;
        vlm5030.Render(Vlm5030DeviceAdapter::kInputClocksPerSample, one);
        maxEnergy = std::max(maxEnergy, vlm5030.CurrentEnergyForTest());
        sawHighEnergy = sawHighEnergy || vlm5030.CurrentEnergyForTest() >= 60;
        for (float x : one) maxSample = std::max(maxSample, std::fabs(x));
    }
    std::printf("  highest current energy seen: %u (frames only ever carry 68 and 5); largest |sample|: %.3f\n", maxEnergy,
                maxSample);
    Check(sawHighEnergy, "falling energy: the loud frames were actually reached (the test exercises the decode)");
    Check(maxEnergy <= 68, "falling energy: the interpolated energy never exceeds the loudest frame (no unsigned wrap-around)");
    Check(maxSample < 0.9f, "falling energy: the output never saturates toward the +-1.0 clamp");
}
