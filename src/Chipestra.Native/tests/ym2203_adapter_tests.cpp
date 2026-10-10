// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Ym2203DeviceAdapter native tests. All register writes below are
// hand-written synthetic stimulus - no real ROM data.
//
// Expected timer periods and register addresses are derived independently
// from ymfm's OPN source (ymfm_opn.h/.cpp):
//   - Timer A = registers 0x24 (high 8 bits) / 0x25 (low 2 bits), Timer B =
//     register 0x26 (8 bits, no split), mode register 0x27 (bit0/1 = load
//     A/B, bit2/3 = enable A/B IRQ - same bit layout as YM2151's own mode
//     register 0x14, just different addresses).
//   - Period formula (ymfm_fm.ipp's shared fm_engine_base::update_timer(),
//     NOT the same simple *64 YM2151 uses - OPN has 12 operators and this
//     adapter fixes clock_prescale=6, giving a different multiplier):
//     Timer A: (1024-TA) * OPERATORS(12) * prescale(6) = (1024-TA)*72 input
//     clocks = (1024-TA)*6 output samples (kInputClocksPerSample=12).
//     Timer B: 16*(256-TB) * 12*6 = (256-TB)*1152 input clocks =
//     (256-TB)*96 output samples.
//   - Channel 0's 4 FM operator register offsets are {0, 4, 8, 12} (real
//     OPN hardware slot layout - verified via ymfm's own
//     operator_offset(opnum) = opnum + opnum/3 formula, NOT the naive
//     {0,1,2,3} a guess might assume).
//   - Key-on register 0x28: bits 0-1 = channel (0-2), bits 4-7 = per-
//     operator mask.
//   - The SSG half is register-compatible with a plain AY-3-8910 (address 0/1
//     = channel A tone period, address 7 = mixer/enable, address 8 = channel
//     A volume).

#include "ym2203_adapter_tests.h"

#include "devices/sound/ym2203/ym2203_device_adapter.h"
#include "test_support.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

struct IrqProbe {
    int asserts = 0;
    int clears = 0;
    bool last = false;

    Ym2203DeviceAdapter::IrqHandler Handler() {
        return [this](bool asserted) {
            if (asserted) {
                ++asserts;
            } else {
                ++clears;
            }
            last = asserted;
        };
    }
};

std::vector<std::vector<float>> MakeChannelBuffers() {
    return std::vector<std::vector<float>>(Ym2203DeviceAdapter::kChannelCount);
}

// Programs a loud, sustained note on FM channel A (algorithm 7 - all four
// operators sum straight to the output, same convention as YM2151's own
// algorithm numbering) so Render() is guaranteed to produce a non-silent
// signal on LogicalChannel 0 and nothing on the other two FM channels.
void ProgramLoudFmNote(Ym2203DeviceAdapter& ym) {
    auto w = [&](std::uint8_t reg, std::uint8_t value) {
        ym.WriteAddress(reg);
        ym.WriteData(value);
    };
    w(0xB0, 0x07);  // channel A: feedback 0, algorithm 7
    w(0xA4, 0x22);  // block/fnum high - any mid-range audible pitch
    w(0xA0, 0x00);  // fnum low
    for (std::uint8_t o : {std::uint8_t(0), std::uint8_t(4), std::uint8_t(8), std::uint8_t(12)}) {
        w(0x30 + o, 0x01);  // detune 0, multiple 1
        w(0x40 + o, 0x00);  // total level 0 (max volume)
        w(0x50 + o, 0x1F);  // KSR 0, attack rate 31 (instant attack)
        w(0x60 + o, 0x00);  // decay rate 0 (stay at attack peak)
        w(0x70 + o, 0x00);  // sustain rate 0
        w(0x80 + o, 0x00);  // sustain level 0, release rate 0
    }
    w(0x28, 0xF0);  // key on all 4 operators, channel A
}

// Programs a loud, sustained tone on SSG channel A (register-compatible
// AY-3-8910 semantics - matches Ay8910DeviceAdapter's own established
// register map).
void ProgramLoudSsgNote(Ym2203DeviceAdapter& ym) {
    auto w = [&](std::uint8_t reg, std::uint8_t value) {
        ym.WriteAddress(reg);
        ym.WriteData(value);
    };
    w(0x00, 0x20);  // tone period fine (channel A) - any small nonzero value
    w(0x01, 0x00);  // tone period coarse (channel A)
    w(0x07, 0x3E);  // mixer: bit0 clear = tone A enabled, all else disabled
    w(0x08, 0x0F);  // channel A volume: max, fixed level (not envelope)
}

}  // namespace

void Ym2203RegisterAccessAndStatusBusy() {
    std::printf("Ym2203RegisterAccessAndStatusBusy\n");

    Ym2203DeviceAdapter ym;
    auto buffers = MakeChannelBuffers();

    Check(ym.ReadStatus() == 0x00, "register access: status reads 0 after construction");

    // Any FM register write marks the chip busy for 32*clock_prescale(6) =
    // 192 input clocks = 16 output samples (this adapter fixes
    // clock_prescale at 6, matching real MAME's ym2203_device - see the
    // adapter header's own comment on OPN_FIDELITY_MED).
    ym.WriteAddress(0x30);
    ym.WriteData(0x00);
    Check((ym.ReadStatus() & 0x80) != 0, "register access: STATUS_BUSY set right after a data write");

    ym.Render(16 * Ym2203DeviceAdapter::kInputClocksPerSample, buffers);
    Check((ym.ReadStatus() & 0x80) == 0, "register access: STATUS_BUSY clears once the busy window elapses");
}

void Ym2203TimerAFiresAfterProgrammedPeriod() {
    std::printf("Ym2203TimerAFiresAfterProgrammedPeriod\n");

    IrqProbe irq;
    Ym2203DeviceAdapter ym(irq.Handler());
    auto buffers = MakeChannelBuffers();

    // TA = 1000 -> period (1024-1000)*6 = 144 output samples = 1728 input
    // clocks (see the header comment for the *6 multiplier).
    ym.WriteAddress(0x24);
    ym.WriteData(1000 >> 2);
    ym.WriteAddress(0x25);
    ym.WriteData(1000 & 0x03);
    ym.WriteAddress(0x27);
    ym.WriteData(0x05);  // bit0 load timer A, bit2 enable timer A IRQ

    ym.Render(143 * Ym2203DeviceAdapter::kInputClocksPerSample, buffers);
    Check((ym.ReadStatus() & 0x01) == 0, "timer A: STATUS_TIMERA still clear one sample before the deadline");
    Check(irq.asserts == 0, "timer A: IRQ not asserted before the deadline");

    ym.Render(2 * Ym2203DeviceAdapter::kInputClocksPerSample, buffers);
    Check((ym.ReadStatus() & 0x01) != 0, "timer A: STATUS_TIMERA set once the programmed period elapses");
    Check(irq.asserts == 1 && irq.last, "timer A: IRQ asserted exactly once on the overflow");
}

void Ym2203TimerBFiresAfterProgrammedPeriod() {
    std::printf("Ym2203TimerBFiresAfterProgrammedPeriod\n");

    IrqProbe irq;
    Ym2203DeviceAdapter ym(irq.Handler());
    auto buffers = MakeChannelBuffers();

    // TB = 254 -> period (256-254)*96 = 192 output samples = 2304 input
    // clocks.
    ym.WriteAddress(0x26);
    ym.WriteData(254);
    ym.WriteAddress(0x27);
    ym.WriteData(0x0A);  // bit1 load timer B, bit3 enable timer B IRQ

    ym.Render(191 * Ym2203DeviceAdapter::kInputClocksPerSample, buffers);
    Check((ym.ReadStatus() & 0x02) == 0, "timer B: STATUS_TIMERB still clear one sample before the deadline");
    Check(irq.asserts == 0, "timer B: IRQ not asserted before the deadline");

    ym.Render(2 * Ym2203DeviceAdapter::kInputClocksPerSample, buffers);
    Check((ym.ReadStatus() & 0x02) != 0, "timer B: STATUS_TIMERB set once the programmed period elapses");
    Check(irq.asserts == 1 && irq.last, "timer B: IRQ asserted exactly once on the overflow");
}

void Ym2203ChannelOutputReachesPerChannelBuffers() {
    std::printf("Ym2203ChannelOutputReachesPerChannelBuffers\n");

    Ym2203DeviceAdapter ym;
    auto buffers = MakeChannelBuffers();

    ProgramLoudFmNote(ym);
    ProgramLoudSsgNote(ym);

    const std::uint64_t clocks = 2000 * Ym2203DeviceAdapter::kInputClocksPerSample;
    const std::size_t produced = ym.Render(clocks, buffers);
    Check(produced == 2000, "channel output: Render() produced one sample per kInputClocksPerSample input clocks");
    Check(ym.GeneratedSampleCount() == 2000, "channel output: GeneratedSampleCount() agrees");

    auto peakOf = [&](std::uint32_t ch) {
        float peak = 0.0f;
        for (float sample : buffers[ch]) {
            peak = std::max(peak, std::fabs(sample));
        }
        return peak;
    };

    // FM channel A (index 0) and SSG channel A (index 3) are keyed on;
    // FM B/C (1,2) and SSG B/C (4,5) were never touched.
    Check(peakOf(0) > 0.01f, "channel output: keyed FM channel A produced a non-silent signal");
    Check(peakOf(3) > 0.01f, "channel output: keyed SSG channel A produced a non-silent signal");
    for (std::uint32_t ch : {std::uint32_t(1), std::uint32_t(2), std::uint32_t(4), std::uint32_t(5)}) {
        Check(peakOf(ch) == 0.0f, "channel output: untouched channel stayed exactly silent");
    }
}

void Ym2203RenderIsDeterministicAndLosesNoChipTime() {
    std::printf("Ym2203RenderIsDeterministicAndLosesNoChipTime\n");

    auto run = [](std::vector<float>& out) {
        Ym2203DeviceAdapter ym;
        auto buffers = MakeChannelBuffers();
        ProgramLoudFmNote(ym);
        // Same total chip time, fed in uneven chunks that don't land on 12:
        // the partial-clock carry must reproduce the single-call result.
        for (std::uint64_t chunk : {std::uint64_t{100}, std::uint64_t{33}, std::uint64_t{4000}, std::uint64_t{7},
                                    std::uint64_t{1860}}) {
            ym.Render(chunk, buffers);
        }
        out = buffers[0];
    };

    std::vector<float> a;
    std::vector<float> b;
    run(a);
    run(b);
    Check(!a.empty() && a == b, "determinism: identical register + timing sequence yields identical samples");
    // 100+33+4000+7+1860 = 6000 input clocks, an EXACT multiple of
    // kInputClocksPerSample(12) this time (unlike YM2151's own 64-clocks-
    // per-sample equivalent test, where 6000 wasn't a clean multiple of 64) -
    // so no carry is expected: 6000/12 = 500 whole samples exactly.
    Check(a.size() == 500, "determinism: 6000 input clocks is an exact multiple of 12 - no leftover carry");
}
