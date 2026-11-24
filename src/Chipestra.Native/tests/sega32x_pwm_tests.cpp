// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill

#include "sega32x_pwm_tests.h"

#include "devices/sega/megadrive/sega32x_pwm.h"
#include "test_support.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {
constexpr std::uint16_t kCtrlBoth = 0x0005;   // left to left, right to right
}

void Sega32xPwmFifoStatusAndRegisters() {
    std::printf("Sega32xPwmFifoStatusAndRegisters\n");
    Sega32xPwm pwm;
    Check(pwm.Read(4) == 0x4000 && pwm.Read(6) == 0x4000 && pwm.Read(8) == 0x4000, "after reset both FIFOs are empty (bit 14), as MAME");
    pwm.Write(0, kCtrlBoth);
    pwm.Write(2, 1000);
    Check(pwm.Read(0) == kCtrlBoth && pwm.Read(2) == 1000, "the control and cycle registers read back");
    pwm.Write(4, 10);
    Check(pwm.Read(4) == 0x0000 && pwm.Read(6) == 0x4000, "one entry in the left FIFO: neither empty nor full; the right FIFO is still empty, as MAME");
    pwm.Write(4, 11);
    pwm.Write(4, 12);
    Check(pwm.Read(4) == 0x8000 && pwm.Read(8) == (0x8000 & 0x4000), "three entries: full (bit 15); the mono status is the AND of the two, as MAME");
    pwm.Write(8, 5);   // mono write: both channels; the left one is full: its oldest entry is dropped
    Check(pwm.Read(4) == 0x8000 && pwm.Read(6) == 0x0000, "a mono write enters both FIFOs, a full FIFO drops its oldest entry (as MAME)");
    pwm.Write(0, kCtrlBoth);
    Check(pwm.Read(4) == 0x4000 && pwm.Read(6) == 0x4000, "writing the control register empties the FIFOs (as MAME)");
    Check(pwm.Read(0xA) == 0xFFFF, "an undefined register reads 0xFFFF (as MAME)");
}

void Sega32xPwmSamplePeriodLevelAndRouting() {
    std::printf("Sega32xPwmSamplePeriodLevelAndRouting\n");
    {   // a full FIFO drops its oldest entry; a control write throws the entries away
        Sega32xPwm pwm;
        pwm.Write(0, kCtrlBoth);
        pwm.Write(2, 1000);
        pwm.Write(4, 100);
        pwm.Write(4, 200);
        pwm.Write(4, 300);
        pwm.Write(4, 400);
        pwm.AdvanceTo(3000);
        const auto c = pwm.TakeChanges();
        Check(c.size() == 3 && std::fabs(c[0].left - 0.2f) < 1e-6f && std::fabs(c[1].left - 0.3f) < 1e-6f && std::fabs(c[2].left - 0.4f) < 1e-6f,
              "a write to a full FIFO drops the oldest entry: 200, 300, 400 come out (as MAME)");
        pwm.Write(4, 500);
        pwm.Write(0, kCtrlBoth);
        pwm.AdvanceTo(9000);
        Check(pwm.TakeChanges().empty(), "entries written before a control register write never come out (as MAME)");
    }
    {
        Sega32xPwm pwm;
        pwm.Write(0, kCtrlBoth);
        pwm.Write(2, 1000);
        pwm.Write(4, 500);
        pwm.Write(6, 250);
        pwm.Write(4, 1000);
        pwm.Write(6, 0);
        pwm.AdvanceTo(5000);
        const auto changes = pwm.TakeChanges();
        Check(changes.size() == 2, "each sample that changes an output makes one change (judged correct: the hardware holds the last value)");
        Check(changes.size() == 2 && changes[0].clock == 1000 && changes[1].clock == 2000, "samples come every `cycle` clocks (judged correct; MAME uses cycle - 1)");
        Check(changes.size() == 2 && std::fabs(changes[0].left - 0.5f) < 1e-6f && std::fabs(changes[0].right - 0.25f) < 1e-6f, "the level is the pulse width divided by the cycle (judged correct; MAME's DAC ignores the cycle)");
        Check(changes.size() == 2 && changes[1].left == 1.0f && changes[1].right == 0.0f, "a full width gives 1, zero gives 0");
        pwm.AdvanceTo(9000);
        Check(pwm.TakeChanges().empty(), "an empty FIFO holds the last output: no further change");
    }
    {   // routing: LMD = 2 sends the left channel to the right output, RMD = 2 the right channel to the left output
        Sega32xPwm pwm;
        pwm.Write(0, 0x000A);
        pwm.Write(2, 100);
        pwm.Write(4, 10);
        pwm.Write(6, 20);
        pwm.AdvanceTo(100);
        const auto c = pwm.TakeChanges();
        Check(c.size() == 1 && std::fabs(c[0].left - 0.2f) < 1e-6f && std::fabs(c[0].right - 0.1f) < 1e-6f, "the control register's mode fields swap the channels (as MAME)");
    }
    {   // mono write feeds both outputs
        Sega32xPwm pwm;
        pwm.Write(0, kCtrlBoth);
        pwm.Write(2, 200);
        pwm.Write(8, 100);
        pwm.AdvanceTo(200);
        const auto c = pwm.TakeChanges();
        Check(c.size() == 1 && c[0].left == 0.5f && c[0].right == 0.5f, "a mono write comes out on both outputs");
    }
    {   // a mode of 0 turns an output off: the FIFO still advances
        Sega32xPwm pwm;
        pwm.Write(0, 0x0001);   // only the left channel is on
        pwm.Write(2, 100);
        pwm.Write(4, 50);
        pwm.Write(6, 90);
        pwm.AdvanceTo(100);
        const auto c = pwm.TakeChanges();
        Check(c.size() == 1 && c[0].left == 0.5f && c[0].right == 0.0f, "an output whose mode is 0 stays silent");
        Check(pwm.Read(6) == 0x4000, "but its FIFO still empties (as MAME)");
    }
}

void Sega32xPwmInterruptIntervalAndRunningConditions() {
    std::printf("Sega32xPwmInterruptIntervalAndRunningConditions\n");
    {
        Sega32xPwm pwm;
        int interrupts = 0;
        pwm.SetInterruptCallback([&] { ++interrupts; });
        pwm.Write(0, static_cast<std::uint16_t>(0x0200 | kCtrlBoth));   // every 2 samples
        pwm.Write(2, 100);
        pwm.AdvanceTo(1000);   // 10 samples
        Check(interrupts == 5, "the interrupt comes every N samples (N in bits 8-11 of the control register, as MAME)");
    }
    {
        Sega32xPwm pwm;
        int interrupts = 0;
        pwm.SetInterruptCallback([&] { ++interrupts; });
        pwm.Write(0, kCtrlBoth);   // the interval field is 0: 16 samples
        pwm.Write(2, 10);
        pwm.AdvanceTo(10 * 33);
        Check(interrupts == 2, "an interval field of 0 means 16 samples (as MAME)");
    }
    {
        Sega32xPwm pwm;
        int interrupts = 0;
        pwm.SetInterruptCallback([&] { ++interrupts; });
        pwm.Write(0, 0x0100);   // both modes off
        pwm.Write(2, 10);
        pwm.AdvanceTo(1000);
        Check(interrupts == 0 && !pwm.Running(), "with both outputs off the unit does not run (as MAME)");
        pwm.Write(0, 0x0105);
        pwm.Write(2, 1);
        pwm.AdvanceTo(2000);
        Check(interrupts == 0 && !pwm.Running(), "a cycle of 1 does not run either (as MAME)");
    }
    {   // the time of the first sample counts from the register write that started it
        Sega32xPwm pwm;
        pwm.AdvanceTo(777);
        pwm.Write(0, kCtrlBoth);
        pwm.Write(2, 100);
        pwm.Write(4, 50);
        pwm.AdvanceTo(777 + 100);
        const auto c = pwm.TakeChanges();
        Check(c.size() == 1 && c[0].clock == 877, "the first sample comes one cycle after the write that started the unit");
    }
}

void Sega32xPwmRasterizesTheChangesToAnOutputRate() {
    std::printf("Sega32xPwmRasterizesTheChangesToAnOutputRate\n");
    Sega32xPwm pwm(1000000.0);   // a 1 MHz unit clock keeps the numbers round
    std::deque<Sega32xPwm::Change> changes;
    changes.push_back({2000, 0.5f, 0.25f});
    changes.push_back({4000, 1.0f, 0.0f});
    float left = 0.1f, right = 0.2f;
    std::vector<float> l(8), r(8);
    pwm.Rasterize(changes, 0, 1000.0, left, right, l.data(), r.data(), 8);   // one output frame per 1000 clocks
    Check(l[0] == 0.1f && l[1] == 0.1f && l[2] == 0.5f && l[3] == 0.5f && l[4] == 1.0f && l[7] == 1.0f, "the left output holds each value from its change on (zero-order hold)");
    Check(r[0] == 0.2f && r[2] == 0.25f && r[4] == 0.0f, "and so does the right output");
    Check(left == 1.0f && right == 0.0f, "the values in force at the end are returned for the next block");
}
