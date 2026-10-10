// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// SegaPcmDeviceAdapter native tests. Every PCM sample byte below is
// hand-written synthetic data - no real After Burner II PCM ROM.
//
// Register layout (MAME sega_315_5218 map): for voice v the registers sit at
// base + v*8 with base in {0x02 lvol, 0x03 rvol, 0x04/0x05 loop lo/hi,
// 0x06 end, 0x07 freq, 0x84/0x85 addr bits 8-15 / 16-23, 0x86 ctrl}.
// ctrl: bit0 = disable, bit1 = loop disable, bits 4-6 = bank (BANK_512).

#include "segapcm_adapter_tests.h"

#include "devices/sega/xboard/xboard_machine.h"
#include "devices/sound/segapcm/segapcm_device_adapter.h"
#include "mixer.h"
#include "resampling_mixer_feed.h"
#include "test_support.h"
#include "chipestra_abi.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

std::vector<std::vector<float>> MakeVoiceBuffers() {
    return std::vector<std::vector<float>>(SegaPcmDeviceAdapter::kVoiceCount);
}

// Register offset for voice `v`'s register whose voice-0 offset is `base`.
std::uint8_t Reg(std::uint8_t base, std::uint32_t v) {
    return static_cast<std::uint8_t>(base + v * 8);
}

// Program a voice for steady playback of a single ROM byte (freq 0 -> the
// address never advances), enabled, in `bank`, full volume both sides.
void ProgramSteadyVoice(SegaPcmDeviceAdapter& pcm, std::uint32_t v, std::uint8_t bankBits,
                        std::uint8_t lvol, std::uint8_t rvol) {
    pcm.WriteRegister(Reg(0x02, v), lvol);
    pcm.WriteRegister(Reg(0x03, v), rvol);
    pcm.WriteRegister(Reg(0x07, v), 0x00);  // freq 0 - stay on one sample
    pcm.WriteRegister(Reg(0x84, v), 0x00);  // addr bits 8-15
    pcm.WriteRegister(Reg(0x85, v), 0x00);  // addr bits 16-23
    pcm.WriteRegister(Reg(0x06, v), 0x40);  // end - addr>>16 stays 0, never reached
    pcm.WriteRegister(Reg(0x86, v), bankBits);  // ctrl: enabled (bit0 clear)
}

float PeakAbs(const std::vector<float>& v) {
    float peak = 0.0f;
    for (float x : v) {
        peak = std::max(peak, std::fabs(x));
    }
    return peak;
}

}  // namespace

void SegaPcmVoiceRegisterReadback() {
    SegaPcmDeviceAdapter pcm({}, SegaPcmDeviceAdapter::kBank512);

    Check(pcm.ReadRegister(Reg(0x02, 1)) == 0xFF, "register: a fresh voice reads 0xFF (power-on state)");

    // Write voice 0's registers, read them all back.
    pcm.WriteRegister(Reg(0x02, 0), 0x40);
    pcm.WriteRegister(Reg(0x03, 0), 0x3F);
    pcm.WriteRegister(Reg(0x04, 0), 0xAB);  // loop lo
    pcm.WriteRegister(Reg(0x05, 0), 0xCD);  // loop hi
    pcm.WriteRegister(Reg(0x06, 0), 0x12);
    pcm.WriteRegister(Reg(0x07, 0), 0x08);
    pcm.WriteRegister(Reg(0x84, 0), 0x20);  // addr bits 8-15
    pcm.WriteRegister(Reg(0x85, 0), 0x01);  // addr bits 16-23
    pcm.WriteRegister(Reg(0x86, 0), 0x30);  // ctrl (bank 3, enabled)

    Check(pcm.ReadRegister(Reg(0x02, 0)) == 0x40, "register: lvol reads back");
    Check(pcm.ReadRegister(Reg(0x03, 0)) == 0x3F, "register: rvol reads back");
    Check(pcm.ReadRegister(Reg(0x04, 0)) == 0xAB, "register: loop low byte reads back");
    Check(pcm.ReadRegister(Reg(0x05, 0)) == 0xCD, "register: loop high byte reads back");
    Check(pcm.ReadRegister(Reg(0x06, 0)) == 0x12, "register: end reads back");
    Check(pcm.ReadRegister(Reg(0x07, 0)) == 0x08, "register: freq reads back");
    Check(pcm.ReadRegister(Reg(0x84, 0)) == 0x20, "register: addr bits 8-15 read back");
    Check(pcm.ReadRegister(Reg(0x85, 0)) == 0x01, "register: addr bits 16-23 read back");
    Check(pcm.ReadRegister(Reg(0x86, 0)) == 0x30, "register: ctrl reads back");

    // A different voice has an independent register file.
    pcm.WriteRegister(Reg(0x02, 3), 0x55);
    Check(pcm.ReadRegister(Reg(0x02, 3)) == 0x55, "register: voice 3 lvol is independent");
    Check(pcm.ReadRegister(Reg(0x02, 0)) == 0x40, "register: voice 0 lvol untouched by the voice 3 write");

    // Offsets outside the voice-register slots are plain scratchpad RAM.
    pcm.WriteRegister(0x00, 0x99);
    pcm.WriteRegister(0x81, 0x77);
    Check(pcm.ReadRegister(0x00) == 0x99 && pcm.ReadRegister(0x81) == 0x77,
          "register: non-voice offsets behave as scratchpad RAM");

    // Reset clears everything back to power-on.
    pcm.Reset();
    Check(pcm.ReadRegister(Reg(0x02, 0)) == 0xFF && pcm.ReadRegister(0x00) == 0xFF,
          "register: Reset() restores the power-on 0xFF state");
}

void SegaPcmVoiceEnableAndBankSelect() {
    // 3 x 64KB banks (BANK_512: bank offset = (ctrl & 0x70) << 12).
    std::vector<std::uint8_t> rom(0x30000, 0x80);   // 0x80 == silence
    std::fill(rom.begin() + 0x00000, rom.begin() + 0x00100, std::uint8_t{0xC0});  // bank 0: +0x40
    std::fill(rom.begin() + 0x10000, rom.begin() + 0x10100, std::uint8_t{0x40});  // bank 1: -0x40
    std::fill(rom.begin() + 0x20000, rom.begin() + 0x20100, std::uint8_t{0xFF});  // bank 2: +0x7F

    SegaPcmDeviceAdapter pcm(std::move(rom), SegaPcmDeviceAdapter::kBank512);
    auto buffers = MakeVoiceBuffers();

    // Voice 0, bank 0, full volume, address 0, freq 0.
    ProgramSteadyVoice(pcm, 0, /*bankBits=*/0x00, 0x7F, 0x7F);
    pcm.Render(32 * SegaPcmDeviceAdapter::kOutputClockDivider, buffers);
    // sample = (0xC0 - 0x80) = +64; the buffer now carries the raw waveform
    // normalized to [-1, 1] (v / 128), with the L/R volumes applied downstream.
    const float expectBank0 = 64.0f / 128.0f;
    Check(std::fabs(buffers[0].back() - expectBank0) < 1e-4f, "bank: voice reads bank 0 when ctrl bank bits are 0");

    // Switch voice 0 to bank 1 (ctrl bit 4).
    for (auto& b : buffers) {
        b.clear();
    }
    pcm.WriteRegister(Reg(0x86, 0), 0x10);
    pcm.Render(32 * SegaPcmDeviceAdapter::kOutputClockDivider, buffers);
    Check(std::fabs(buffers[0].back() + expectBank0) < 1e-4f,
          "bank: ctrl bit 4 selects bank 1 (different ROM window, opposite-sign sample)");

    // Bank 2 (ctrl bits 5).
    for (auto& b : buffers) {
        b.clear();
    }
    pcm.WriteRegister(Reg(0x86, 0), 0x20);
    pcm.Render(32 * SegaPcmDeviceAdapter::kOutputClockDivider, buffers);
    const float expectBank2 = 127.0f / 128.0f;
    Check(std::fabs(buffers[0].back() - expectBank2) < 1e-4f, "bank: ctrl bits 5 select bank 2");

    // Disable the voice (ctrl bit 0) -> silence, and it is observable via a
    // register read.
    for (auto& b : buffers) {
        b.clear();
    }
    pcm.WriteRegister(Reg(0x86, 0), 0x21);  // bank 2 bits + disable
    pcm.Render(16 * SegaPcmDeviceAdapter::kOutputClockDivider, buffers);
    Check(PeakAbs(buffers[0]) == 0.0f, "enable: a disabled voice (ctrl bit 0 set) is exactly silent");
    Check(pcm.ReadRegister(Reg(0x86, 0)) == 0x21, "enable: ctrl bit 0 is observable via a register read");
}

void SegaPcmLoopAndStopAtEndAddress() {
    // Region A (sample index 0..0x7F) = +0x40, region B (0x80..0xFF) = -0x40.
    std::vector<std::uint8_t> rom(0x20000, 0x80);
    std::fill(rom.begin() + 0x00, rom.begin() + 0x80, std::uint8_t{0xC0});
    std::fill(rom.begin() + 0x80, rom.begin() + 0x100, std::uint8_t{0x40});

    SegaPcmDeviceAdapter pcm(std::move(rom), SegaPcmDeviceAdapter::kBank512);
    auto buffers = MakeVoiceBuffers();

    auto program = [&](std::uint32_t v, std::uint8_t ctrl) {
        pcm.WriteRegister(Reg(0x02, v), 0x7F);
        pcm.WriteRegister(Reg(0x03, v), 0x7F);
        pcm.WriteRegister(Reg(0x07, v), 0xFF);  // freq: advance ~1 sample/tick
        pcm.WriteRegister(Reg(0x84, v), 0x00);  // start at address 0
        pcm.WriteRegister(Reg(0x85, v), 0x00);
        pcm.WriteRegister(Reg(0x06, v), 0x01);  // end: loop/stop once addr>>16 == 1
        pcm.WriteRegister(Reg(0x04, v), 0x80);  // loop -> sample index 0x80 (region B)
        pcm.WriteRegister(Reg(0x05, v), 0x00);
        pcm.WriteRegister(Reg(0x86, v), ctrl);
    };

    program(0, /*ctrl=*/0x00);  // loop enabled
    program(1, /*ctrl=*/0x02);  // loop disabled -> stop at end

    pcm.Render(600 * SegaPcmDeviceAdapter::kOutputClockDivider, buffers);

    const float regionA = 64.0f / 128.0f;  // raw waveform, v / 128

    // Voice 0: starts in region A, then after wrapping past `end` it loops
    // back to sample 0x80 and plays region B (-A) forever.
    Check(std::fabs(buffers[0][10] - regionA) < 1e-4f, "loop: voice starts playing region A");
    Check(std::fabs(buffers[0][550] + regionA) < 1e-4f,
          "loop: after hitting end it loops to the loop address and plays region B");
    Check(PeakAbs(buffers[0]) > 0.0f, "loop: a looping voice never goes silent");

    // Voice 1: same, but loop-disabled -> it self-stops at end and its ctrl
    // bit 0 gets set.
    Check(std::fabs(buffers[1][10] - regionA) < 1e-4f, "stop: loop-disabled voice also starts in region A");
    Check(buffers[1][550] == 0.0f, "stop: a loop-disabled voice is silent after reaching end");
    Check((pcm.ReadRegister(Reg(0x86, 1)) & 0x01) != 0,
          "stop: reaching end with loop disabled sets ctrl bit 0 (self-disable)");
}

void SegaPcmVoiceChannelOutputAndPan() {
    std::vector<std::uint8_t> rom(0x100, 0xC0);  // sample = +0x40 everywhere
    SegaPcmDeviceAdapter pcm(std::move(rom), SegaPcmDeviceAdapter::kBank512);
    auto buffers = MakeVoiceBuffers();

    // Voice 5 only: left at half of full scale, right silent. The per-voice
    // buffer now carries the raw waveform (v / 128); the independent L/R
    // volumes are exposed for the Machine to fold into pan/routing.
    ProgramSteadyVoice(pcm, 5, /*bankBits=*/0x00, /*lvol=*/0x40, /*rvol=*/0x00);

    const std::size_t produced = pcm.Render(100 * SegaPcmDeviceAdapter::kOutputClockDivider, buffers);
    Check(produced == 100, "channel output: Render() produced one sample per kOutputClockDivider master clocks");
    Check(pcm.GeneratedSampleCount() == 100, "channel output: GeneratedSampleCount() agrees");

    const float expected = 64.0f / 128.0f;  // raw waveform, independent of the L/R split
    bool voice5_ok = buffers[5].size() == 100;
    for (float s : buffers[5]) {
        voice5_ok = voice5_ok && std::fabs(s - expected) < 1e-4f;
    }
    Check(voice5_ok, "channel output: voice 5's buffer carries the raw waveform (L/R split not folded in)");
    Check(pcm.VoiceVolumeLeft(5) == 0x40 && pcm.VoiceVolumeRight(5) == 0x00,
          "channel output: the independent L/R volumes are exposed to the Machine");

    float others = 0.0f;
    for (std::uint32_t v = 0; v < SegaPcmDeviceAdapter::kVoiceCount; ++v) {
        if (v != 5) {
            others = std::max(others, PeakAbs(buffers[v]));
        }
    }
    Check(others == 0.0f, "channel output: the other 15 voices stayed exactly silent");
}

void SegaPcmRenderIsDeterministicAndLosesNoMasterClocks() {
    std::vector<std::uint8_t> rom(0x200, 0x00);
    for (std::size_t i = 0; i < rom.size(); ++i) {
        rom[i] = static_cast<std::uint8_t>(i & 0xFF);  // a ramp
    }

    auto run = [&](std::vector<float>& out, bool chunked) {
        SegaPcmDeviceAdapter pcm(rom, SegaPcmDeviceAdapter::kBank512);
        auto buffers = MakeVoiceBuffers();
        pcm.WriteRegister(Reg(0x02, 2), 0x7F);
        pcm.WriteRegister(Reg(0x03, 2), 0x60);
        pcm.WriteRegister(Reg(0x07, 2), 0x40);  // advance through the ramp
        pcm.WriteRegister(Reg(0x84, 2), 0x00);
        pcm.WriteRegister(Reg(0x85, 2), 0x00);
        pcm.WriteRegister(Reg(0x06, 2), 0x02);
        pcm.WriteRegister(Reg(0x86, 2), 0x00);
        if (chunked) {
            for (std::uint64_t c : {std::uint64_t{700}, std::uint64_t{40000}, std::uint64_t{13},
                                    std::uint64_t{99999}, std::uint64_t{511}}) {
                pcm.Render(c, buffers);
            }
        } else {
            pcm.Render(700 + 40000 + 13 + 99999 + 511, buffers);
        }
        out = buffers[2];
    };

    std::vector<float> single;
    std::vector<float> chunked;
    run(single, false);
    run(chunked, true);
    Check(!single.empty() && single == chunked,
          "determinism: the same master-clock total split into uneven chunks yields identical samples");
    // 700 + 40000 + 13 + 99999 + 511 = 141223 master clocks -> 275 whole
    // samples (275 * 512 = 140800), 423 carried.
    Check(single.size() == 275, "determinism: leftover master clocks carry, they are not dropped or double-counted");
}

void XBoardMachineSegaPcmAudioReachesMixer() {
    // A JR-self Z80 ROM: the sound CPU just spins, this test is about the
    // SegaPCM -> LogicalChannelSink -> resampler -> Mixer audio path.
    std::vector<std::uint8_t> pcmRom(0x10000, 0xC0);  // steady +0x40 sample
    XBoardMachine machine(std::vector<std::uint8_t>{0x18, 0xFE}, std::move(pcmRom));
    machine.Initialize();

    // The SegaPCM LogicalChannels follow the 8 YM2151 ones.
    const ChannelId pcmChannel0 = machine.LogicalChannels()[Ym2151DeviceAdapter::kChannelCount].Id;

    SegaPcmDeviceAdapter& pcm = machine.SegaPcmForTest();
    pcm.WriteRegister(0x02, 0x7F);  // voice 0 lvol
    pcm.WriteRegister(0x03, 0x7F);  // voice 0 rvol
    pcm.WriteRegister(0x07, 0x00);  // freq 0
    pcm.WriteRegister(0x84, 0x00);
    pcm.WriteRegister(0x85, 0x00);
    pcm.WriteRegister(0x06, 0x40);
    pcm.WriteRegister(0x86, 0x00);  // enable

    const std::size_t canonicalFrames = Mixer::kBlockSizeFrames * 8;  // 2048

    LogicalChannelSink native;
    ResamplingMixerFeed feed;

    // Warm-up: SoxrSampleRateConverter needs a real settling period before
    // its output is accurate (see ResamplingMixerFeedPassthroughAndResample
    // in sample_rate_converter_tests.cpp).
    for (int warm = 0; warm < 5; ++warm) {
        native.ClearAll();
        machine.Render(EmuDuration{canonicalFrames}, &native);
        feed.Resample(native, machine.LogicalChannels(), canonicalFrames);
    }

    native.ClearAll();
    machine.Render(EmuDuration{canonicalFrames}, &native);

    const LogicalChannelBuffer& nativeBuffer = native.ChannelBuffer(pcmChannel0);
    const std::size_t nativeSamples = nativeBuffer.Size();
    Check(nativeSamples > 0 && nativeSamples < canonicalFrames,
          "e2e: XBoardMachine wrote native-rate (31250Hz) SegaPCM samples, fewer than the canonical block");
    float nativePeak = 0.0f;
    for (std::size_t i = 0; i < nativeSamples; ++i) {
        nativePeak = std::max(nativePeak, std::fabs(nativeBuffer[i]));
    }
    Check(nativePeak > 0.01f, "e2e: the SegaPCM voice produced a non-silent native signal");

    const LogicalChannelSink& canonical = feed.Resample(native, machine.LogicalChannels(), canonicalFrames);
    Check(canonical.ChannelBuffer(pcmChannel0).Size() == canonicalFrames,
          "e2e: the resampler fills the whole canonical block for the SegaPCM channel");

    Mixer mixer;
    std::vector<StereoFrame> out;
    mixer.MixBlock(canonical, machine.LogicalChannels(), canonicalFrames, out);
    float mixPeak = 0.0f;
    for (const StereoFrame& f : out) {
        mixPeak = std::max({mixPeak, std::fabs(f.left), std::fabs(f.right)});
    }
    Check(mixPeak > 0.01f, "e2e: SegaPCM audio reaches the Mixer's stereo output through the resampler");
}
