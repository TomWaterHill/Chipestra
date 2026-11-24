// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
//
// Register behaviour after MAME's 32X (src/mame/shared/mega32x.cpp, BSD-3-Clause, copyright-holders David Haywood); see the
// header for what differs.

#include "sega32x_pwm.h"

namespace {
inline std::uint32_t EffectiveCycle(std::uint16_t cycle) { return cycle == 0 ? 4096u : cycle; }
}  // namespace

void Sega32xPwm::Reset() {
    ctrl_ = 0;
    cycle_ = 0;
    sizeL_ = sizeR_ = 0;
    statusL_ = statusR_ = 0x4000;
    running_ = false;
    now_ = nextTick_ = 0;
    tickCount_ = 0;
    interval_ = 16;
    outL_ = outR_ = 0;
    changes_.clear();
}

void Sega32xPwm::Restart() {
    interval_ = (ctrl_ >> 8) & 15;
    if (interval_ == 0) interval_ = 16;
    running_ = EffectiveCycle(cycle_) > 1 && (ctrl_ & 0xF) != 0;
    tickCount_ = 0;
    sizeL_ = sizeR_ = 0;
    statusL_ = statusR_ = 0x4000;
    nextTick_ = now_ + EffectiveCycle(cycle_);
}

std::uint16_t Sega32xPwm::Read(unsigned offset) const {
    switch (offset) {
    case 0: return ctrl_;
    case 2: return cycle_;
    case 4: return statusL_;
    case 6: return statusR_;
    case 8: return static_cast<std::uint16_t>(statusL_ & statusR_);
    default: return 0xFFFF;
    }
}

void Sega32xPwm::Push(std::uint16_t* fifo, int& size, std::uint16_t value) {
    if (size == kFifoSize) {              // full: the oldest entry is dropped (as MAME does)
        for (int i = 0; i < kFifoSize - 1; ++i) fifo[i] = fifo[i + 1];
        --size;
    }
    fifo[size++] = value;
}

void Sega32xPwm::Write(unsigned offset, std::uint16_t value) {
    switch (offset) {
    case 0: ctrl_ = value; Restart(); break;
    case 2: cycle_ = value & 0xFFF; Restart(); break;
    case 4: Push(fifoL_, sizeL_, value); statusL_ = sizeL_ == kFifoSize ? 0x8000 : 0; break;
    case 6: Push(fifoR_, sizeR_, value); statusR_ = sizeR_ == kFifoSize ? 0x8000 : 0; break;
    case 8:
        Push(fifoL_, sizeL_, value);
        Push(fifoR_, sizeR_, value);
        statusL_ = sizeL_ == kFifoSize ? 0x8000 : 0;
        statusR_ = sizeR_ == kFifoSize ? 0x8000 : 0;
        break;
    default: break;
    }
}

void Sega32xPwm::Output(int side, std::uint16_t width) {
    float duty = static_cast<float>(width) / static_cast<float>(EffectiveCycle(cycle_));
    if (duty > 1.0f) duty = 1.0f;
    (side == 0 ? outL_ : outR_) = duty;
}

void Sega32xPwm::Tick() {
    const float beforeL = outL_, beforeR = outR_;
    if (sizeL_ > 0) {
        const unsigned mode = ctrl_ & 3;
        if (mode == 1) Output(0, fifoL_[0]);
        else if (mode == 2) Output(1, fifoL_[0]);
        for (int i = 0; i < kFifoSize - 1; ++i) fifoL_[i] = fifoL_[i + 1];
        --sizeL_;
    }
    statusL_ = sizeL_ == 0 ? 0x4000 : 0;
    if (sizeR_ > 0) {
        const unsigned mode = (ctrl_ >> 2) & 3;
        if (mode == 1) Output(1, fifoR_[0]);
        else if (mode == 2) Output(0, fifoR_[0]);
        for (int i = 0; i < kFifoSize - 1; ++i) fifoR_[i] = fifoR_[i + 1];
        --sizeR_;
    }
    statusR_ = sizeR_ == 0 ? 0x4000 : 0;
    if (outL_ != beforeL || outR_ != beforeR) {
        if (!changes_.empty() && changes_.back().clock == nextTick_) { changes_.back().left = outL_; changes_.back().right = outR_; }
        else changes_.push_back({nextTick_, outL_, outR_});
    }
    if (++tickCount_ == interval_) {
        tickCount_ = 0;
        if (interrupt_) interrupt_();
    }
}

void Sega32xPwm::AdvanceTo(std::uint64_t clock) {
    while (running_ && nextTick_ <= clock) {
        now_ = nextTick_;
        Tick();
        nextTick_ += EffectiveCycle(cycle_);
    }
    if (clock > now_) now_ = clock;
}

void Sega32xPwm::Rasterize(const std::deque<Change>& changes, double startClock, double outputRate, float& left, float& right, float* outLeft, float* outRight, std::size_t frames) const {
    std::size_t next = 0;
    const double clocksPerFrame = clockHz_ / outputRate;
    for (std::size_t i = 0; i < frames; ++i) {
        const double at = startClock + static_cast<double>(i) * clocksPerFrame;
        while (next < changes.size() && static_cast<double>(changes[next].clock) <= at) {
            left = changes[next].left;
            right = changes[next].right;
            ++next;
        }
        outLeft[i] = left;
        outRight[i] = right;
    }
}
