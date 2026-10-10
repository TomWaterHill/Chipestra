// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "clock.h"

#include <stdexcept>

namespace {

std::uint64_t Gcd(std::uint64_t a, std::uint64_t b) {
    while (b != 0) {
        std::uint64_t t = b;
        b = a % b;
        a = t;
    }
    return a;
}

}  // namespace

ClockConverter::ClockConverter(std::uint64_t globalTicksPerSecond, ClockRate deviceRate) {
    if (globalTicksPerSecond == 0 || deviceRate.hz == 0) {
        throw std::invalid_argument("ClockConverter: rates must be non-zero");
    }
    std::uint64_t g = Gcd(globalTicksPerSecond, deviceRate.hz);
    num_ = globalTicksPerSecond / g;
    den_ = deviceRate.hz / g;
}

EmuDuration ClockConverter::CyclesToTicks(std::uint64_t cycles) const {
    return EmuDuration(cycles * num_ / den_);
}

std::uint64_t ClockConverter::TicksToCycles(EmuDuration duration) const {
    return duration.ticks * den_ / num_;
}
