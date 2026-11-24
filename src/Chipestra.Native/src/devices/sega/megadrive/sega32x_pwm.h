// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <utility>

// The PWM sound unit of the Sega 32X: two pulse width channels (left, right) with a 3-entry FIFO each, a cycle register that
// sets the sample period in clocks of the unit's own clock, a control register (routing of the two channels to the two
// outputs, and the number of samples between two interrupts), and the interrupt that asks the SH-2s for more data.
//
// Basis of each rule: the register map, the FIFO status bits (bit 15 full, bit 14
// empty), the mono register that writes both channels, and the interrupt interval of 16 samples when the field is 0 follow
// MAME's 32X (mega32x.cpp, BSD-3-Clause); the sample period is the cycle register value in clocks (a counter that counts
// 0 to cycle - 1, as the hardware description gives; MAME uses cycle - 1); the output level is the pulse width divided by
// the cycle (the duty ratio; MAME's DAC takes the width as a 12-bit value whatever the cycle).
//
// Time is the unit's clock, 23.01136 MHz on an NTSC console (3/7 of the master clock, the SH-2's clock). The owner calls
// AdvanceTo() before every register access so the FIFOs are where they were at that time.
class Sega32xPwm {
public:
    static constexpr double kNtscClockHz = 53693175.0 * 3.0 / 7.0;
    static constexpr int kFifoSize = 3;

    // One change of the outputs: from `clock` on the left and right outputs hold these duty ratios (0 to 1).
    struct Change {
        std::uint64_t clock;
        float left, right;
    };

    using InterruptFn = std::function<void()>;

    explicit Sega32xPwm(double clockHz = kNtscClockHz) : clockHz_(clockHz) { Reset(); }
    void SetInterruptCallback(InterruptFn fn) { interrupt_ = std::move(fn); }
    void Reset();

    // Register access, offset 0 control, 2 cycle, 4 left, 6 right, 8 mono (the SH-2's 0x4030-0x4038).
    std::uint16_t Read(unsigned offset) const;
    void Write(unsigned offset, std::uint16_t value);
    // Lets the sample timer run until `clock` (the clock of the access that follows).
    void AdvanceTo(std::uint64_t clock);

    // The changes of the outputs since the last call (in time order).
    std::deque<Change> TakeChanges() { std::deque<Change> out; out.swap(changes_); return out; }
    bool HasChanges() const { return !changes_.empty(); }
    // True when AdvanceTo(clock) would do something (the sample timer is due)
    bool Due(std::uint64_t clock) const { return running_ && nextTick_ <= clock; }
    // Rasterizes `changes` into `frames` samples starting at unit clock `startClock`, at `outputRate`; `left`/`right` hold the
    // value in force at the start (updated to the value at the end).
    void Rasterize(const std::deque<Change>& changes, double startClock, double outputRate, float& left, float& right, float* outLeft, float* outRight, std::size_t frames) const;

    double ClockHz() const { return clockHz_; }
    std::uint16_t Control() const { return ctrl_; }
    std::uint16_t Cycle() const { return cycle_; }
    bool Running() const { return running_; }

private:
    void Restart();
    void Tick();
    void Push(std::uint16_t* fifo, int& size, std::uint16_t value);
    void Output(int side, std::uint16_t width);   // side 0 left output, 1 right output

    double clockHz_;
    InterruptFn interrupt_;
    std::uint16_t ctrl_ = 0, cycle_ = 0;
    std::uint16_t fifoL_[kFifoSize] = {}, fifoR_[kFifoSize] = {};
    int sizeL_ = 0, sizeR_ = 0;
    std::uint16_t statusL_ = 0x4000, statusR_ = 0x4000;
    bool running_ = false;
    std::uint64_t now_ = 0, nextTick_ = 0;
    int tickCount_ = 0, interval_ = 16;
    float outL_ = 0, outR_ = 0;
    std::deque<Change> changes_;
};
