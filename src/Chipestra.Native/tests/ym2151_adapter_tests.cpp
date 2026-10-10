// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Ym2151DeviceAdapter native tests. All register writes and Z80 byte
// programs below are hand-written synthetic stimulus - no real ROM data.
//
// Expected timer periods are derived independently from the public OPM
// register spec: Timer A period = (1024 - TA) input clocks scaled by
// OPERATORS*prescale (32*2 = 64); Timer B period = 16*(256 - TB) likewise.
// One Ym2151DeviceAdapter output sample == 64 input clocks.

#include "ym2151_adapter_tests.h"

#include "devices/sega/xboard/xboard_machine.h"
#include "devices/sound/ym2151/ym2151_device_adapter.h"
#include "test_support.h"
#include "chipestra_abi.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <vector>

namespace {

// Tallies the adapter's IRQ-line callback so a test can assert on edges
// rather than just the final level.
struct IrqProbe {
    int asserts = 0;
    int clears = 0;
    bool last = false;

    Ym2151DeviceAdapter::IrqHandler Handler() {
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
    return std::vector<std::vector<float>>(Ym2151DeviceAdapter::kChannelCount);
}

// Program a loud, sustained note on one FM channel using algorithm 7 (all
// four operators sum straight to the output) so Render() is guaranteed to
// produce a non-silent signal on that channel and nothing on the others.
void ProgramLoudNote(Ym2151DeviceAdapter& ym, std::uint8_t channel) {
    auto w = [&](std::uint8_t reg, std::uint8_t value) {
        ym.WriteAddress(reg);
        ym.WriteData(value);
    };
    w(0x20 + channel, 0xC7);  // pan L+R on, feedback 0, algorithm 7
    w(0x28 + channel, 0x4A);  // key code (a mid-range frequency)
    w(0x30 + channel, 0x00);  // key fraction 0
    for (std::uint8_t op : {std::uint8_t(0), std::uint8_t(8), std::uint8_t(16), std::uint8_t(24)}) {
        const std::uint8_t o = channel + op;
        w(0x40 + o, 0x01);  // DT1 0, MUL 1
        w(0x60 + o, 0x00);  // total level 0 (max volume)
        w(0x80 + o, 0x1F);  // key scale 0, attack rate 31 (instant attack)
        w(0xA0 + o, 0x00);  // AM off, decay rate 0 (stay at attack peak)
        w(0xC0 + o, 0x00);  // DT2 0, sustain rate 0
        w(0xE0 + o, 0x00);  // sustain level 0, release rate 0
    }
    w(0x08, 0x78 | (channel & 0x07));  // key on all four operators
}

}  // namespace

void Ym2151RegisterAccessAndStatusBusy() {
    std::printf("Ym2151RegisterAccessAndStatusBusy\n");

    Ym2151DeviceAdapter ym([](bool) {});
    auto buffers = MakeChannelBuffers();

    Check(ym.ReadStatus() == 0x00, "register access: status reads 0 after reset");

    // Any data-port write marks the chip busy for 32*prescale = 64 input
    // clocks (ymfm write_data() -> ymfm_set_busy_end); observing that through
    // ReadStatus() proves the address+data write path actually reached the
    // chip.
    ym.WriteAddress(0x20);
    ym.WriteData(0xC0);
    Check((ym.ReadStatus() & 0x80) != 0, "register access: STATUS_BUSY set right after a data write");

    ym.Render(64, buffers);  // exactly one output sample / 64 input clocks
    Check((ym.ReadStatus() & 0x80) == 0, "register access: STATUS_BUSY clears once the busy window elapses");
}

void Ym2151TimerAFiresAfterProgrammedPeriod() {
    std::printf("Ym2151TimerAFiresAfterProgrammedPeriod\n");

    IrqProbe irq;
    Ym2151DeviceAdapter ym(irq.Handler());
    auto buffers = MakeChannelBuffers();

    // TA = 1000 -> period (1024 - 1000) * 64 = 1536 input clocks = 24 samples.
    ym.WriteAddress(0x10);
    ym.WriteData(1000 >> 2);
    ym.WriteAddress(0x11);
    ym.WriteData(1000 & 0x03);
    ym.WriteAddress(0x14);
    ym.WriteData(0x05);  // bit0 load timer A, bit2 enable timer A IRQ

    ym.Render(23 * 64, buffers);
    Check((ym.ReadStatus() & 0x01) == 0, "timer A: STATUS_TIMERA still clear one sample before the deadline");
    Check(irq.asserts == 0, "timer A: IRQ not asserted before the deadline");

    ym.Render(2 * 64, buffers);
    Check((ym.ReadStatus() & 0x01) != 0, "timer A: STATUS_TIMERA set once the programmed period elapses");
    Check(irq.asserts == 1 && irq.last, "timer A: IRQ asserted exactly once on the overflow");
}

void Ym2151TimerBFiresAfterProgrammedPeriod() {
    std::printf("Ym2151TimerBFiresAfterProgrammedPeriod\n");

    IrqProbe irq;
    Ym2151DeviceAdapter ym(irq.Handler());
    auto buffers = MakeChannelBuffers();

    // TB = 254 -> period 16 * (256 - 254) * 64 = 2048 input clocks = 32 samples.
    ym.WriteAddress(0x12);
    ym.WriteData(254);
    ym.WriteAddress(0x14);
    ym.WriteData(0x0A);  // bit1 load timer B, bit3 enable timer B IRQ

    ym.Render(31 * 64, buffers);
    Check((ym.ReadStatus() & 0x02) == 0, "timer B: STATUS_TIMERB still clear one sample before the deadline");
    Check(irq.asserts == 0, "timer B: IRQ not asserted before the deadline");

    ym.Render(2 * 64, buffers);
    Check((ym.ReadStatus() & 0x02) != 0, "timer B: STATUS_TIMERB set once the programmed period elapses");
    Check(irq.asserts == 1 && irq.last, "timer B: IRQ asserted exactly once on the overflow");
}

void Ym2151TimerIrqAssertsAndClears() {
    std::printf("Ym2151TimerIrqAssertsAndClears\n");

    IrqProbe irq;
    Ym2151DeviceAdapter ym(irq.Handler());
    auto buffers = MakeChannelBuffers();

    ym.WriteAddress(0x10);
    ym.WriteData(1000 >> 2);
    ym.WriteAddress(0x11);
    ym.WriteData(1000 & 0x03);
    ym.WriteAddress(0x14);
    ym.WriteData(0x05);
    ym.Render(30 * 64, buffers);
    Check(irq.asserts == 1 && irq.last, "irq clear: timer IRQ asserted after advancing past the period");

    // Mode write with only the 'reset timer A' bit (0x10): clears the status
    // flag, drops the IRQ line, and (no load bit) stops the timer.
    ym.WriteAddress(0x14);
    ym.WriteData(0x10);
    Check(irq.clears == 1 && !irq.last, "irq clear: the reset-timer-A mode bit deasserts the IRQ line");
    Check((ym.ReadStatus() & 0x01) == 0, "irq clear: STATUS_TIMERA cleared by the reset");

    ym.Render(60 * 64, buffers);
    Check(irq.asserts == 1, "irq clear: a reset without reload stops the timer - no further asserts");
}

void Ym2151ChannelOutputReachesPerChannelBuffers() {
    std::printf("Ym2151ChannelOutputReachesPerChannelBuffers\n");

    Ym2151DeviceAdapter ym([](bool) {});
    auto buffers = MakeChannelBuffers();

    ProgramLoudNote(ym, 0);

    const std::uint64_t clocks = 2000 * 64;
    const std::size_t produced = ym.Render(clocks, buffers);
    Check(produced == 2000, "channel output: Render() produced one sample per 64 input clocks");
    Check(buffers[0].size() == 2000, "channel output: channel 0's buffer received every sample");
    Check(ym.GeneratedSampleCount() == 2000, "channel output: GeneratedSampleCount() agrees");

    float peakKeyed = 0.0f;
    for (float sample : buffers[0]) {
        peakKeyed = std::max(peakKeyed, std::fabs(sample));
    }
    Check(peakKeyed > 0.01f, "channel output: the keyed-on channel 0 produced a non-silent signal");

    float peakOthers = 0.0f;
    for (std::uint32_t ch = 1; ch < Ym2151DeviceAdapter::kChannelCount; ++ch) {
        for (float sample : buffers[ch]) {
            peakOthers = std::max(peakOthers, std::fabs(sample));
        }
    }
    Check(peakOthers == 0.0f, "channel output: the seven un-keyed channels stayed exactly silent");
}

void Ym2151RenderIsDeterministicAndLosesNoChipTime() {
    std::printf("Ym2151RenderIsDeterministicAndLosesNoChipTime\n");

    auto run = [](std::vector<float>& out) {
        Ym2151DeviceAdapter ym([](bool) {});
        auto buffers = MakeChannelBuffers();
        ProgramLoudNote(ym, 0);
        // Same total chip time, fed in uneven chunks that don't land on 64:
        // the partial-clock carry must reproduce the single-call result.
        for (std::uint64_t chunk : {std::uint64_t{100}, std::uint64_t{33}, std::uint64_t{4000},
                                    std::uint64_t{7}, std::uint64_t{1860}}) {
            ym.Render(chunk, buffers);
        }
        out = buffers[0];
    };

    std::vector<float> a;
    std::vector<float> b;
    run(a);
    run(b);
    Check(!a.empty() && a == b, "determinism: identical register + timing sequence yields identical samples");
    // 100 + 33 + 4000 + 7 + 1860 = 6000 input clocks -> 93 whole samples, 48 carried.
    Check(a.size() == 93, "determinism: leftover input clocks are carried, not dropped or double-counted");
}

void XBoardMachineYm2151IrqAndCommandNmiDoNotConflict() {
    std::printf("XBoardMachineYm2151IrqAndCommandNmiDoNotConflict\n");

    // Synthetic sound program:
    //   0x0000  LD SP,F900h ; IM 1 ; EI ; XOR A ; LD (F810h),A ; LD (F801h),A ; JR $
    //   0x0038  IRQ (IM 1): PUSH AF ; ack+stop YM timer A via OUT ; INC (F810h) ; POP AF ; EI ; RETI
    //   0x0066  NMI: IN A,(40h) ; LD (F801h),A ; RETN
    std::vector<std::uint8_t> rom(0xF000, 0x00);
    auto put = [&](std::size_t at, std::initializer_list<std::uint8_t> bytes) {
        for (std::uint8_t b : bytes) {
            rom[at++] = b;
        }
    };
    put(0x0000, {0x31, 0x00, 0xF9, 0xED, 0x56, 0xFB, 0xAF, 0x32, 0x10, 0xF8, 0x32, 0x01, 0xF8, 0x18, 0xFE});
    put(0x0038, {0xF5, 0x3E, 0x14, 0xD3, 0x00, 0x3E, 0x10, 0xD3, 0x01, 0x3A, 0x10, 0xF8, 0x3C, 0x32, 0x10,
                 0xF8, 0xF1, 0xFB, 0xED, 0x4D});
    put(0x0066, {0xDB, 0x40, 0x32, 0x01, 0xF8, 0xED, 0x45});

    XBoardMachine machine(std::move(rom));
    machine.Initialize();

    // Reach the interrupt-enabled spin loop.
    machine.Render(EmuDuration{200}, nullptr);

    // Program YM2151 Timer A (value 1000 -> 1536 input clocks) and enable it.
    Ym2151DeviceAdapter& ym = machine.Ym2151ForTest();
    ym.WriteAddress(0x10);
    ym.WriteData(1000 >> 2);
    ym.WriteAddress(0x11);
    ym.WriteData(1000 & 0x03);
    ym.WriteAddress(0x14);
    ym.WriteData(0x05);

    // Advance past the deadline (the YM renders after the Z80 each block, so
    // the IRQ line asserts at the end of this call)...
    machine.Render(EmuDuration{1600}, nullptr);
    // ...and fire a command in the same window: NMI, an independent Z80 line.
    MusicCommand command;
    command.Kind = MusicCommandKind::Raw;
    command.Value = 0x55;
    machine.SendCommand(command);

    // The Z80 now sees NMI pending and IRQ asserted simultaneously.
    machine.Render(EmuDuration{3000}, nullptr);

    Check(machine.ReadMemory(0xF801) == 0x55,
          "no conflict: the command-latch NMI handler ran and stored the latched byte");
    Check(machine.ReadMemory(0xF810) == 0x01,
          "no conflict: the YM2151 Timer-A IRQ handler ran exactly once and stopped the timer");

    auto snap = machine.Z80Snapshot();
    Check(snap.sp == 0xF900, "no conflict: stack balanced after both handlers returned");
    Check(!snap.halted, "no conflict: CPU still running, not wedged");
    Check(snap.pc == 0x000D, "no conflict: execution resumed at the spin loop after both handlers");
}
