// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "chipestra_abi.h"

#include <cstdint>

// ClockRate - a device's clock frequency in whole Hz. Never a
// floating-point seconds value: integer Hz keeps every conversion exact.
struct ClockRate {
    std::uint64_t hz = 0;

    constexpr ClockRate() = default;
    constexpr explicit ClockRate(std::uint64_t hz) : hz(hz) {}

    constexpr bool operator==(const ClockRate& other) const { return hz == other.hz; }
    constexpr bool operator!=(const ClockRate& other) const { return hz != other.hz; }
};

// ClockConverter - converts between the Native Runtime's global
// EmuTick counter (running at `globalTicksPerSecond`) and a specific
// device's own clock cycles (running at `deviceRate`).
//
// The conversion ratio (ticks per cycle) is reduced to lowest terms at
// construction time. Round-trip conversion is exact - CyclesToTicks then
// TicksToCycles reproduces the original cycle count, and likewise the other
// way round - whenever globalTicksPerSecond and deviceRate.hz divide evenly
// into one another in the direction being converted; this is the intended
// usage (whoever sets up a Machine's Scheduler picks a global tick rate that
// is an integer multiple of every device rate it needs to convert).
class ClockConverter {
public:
    ClockConverter(std::uint64_t globalTicksPerSecond, ClockRate deviceRate);

    EmuDuration CyclesToTicks(std::uint64_t cycles) const;
    std::uint64_t TicksToCycles(EmuDuration duration) const;

private:
    // Reduced (via GCD) ticks-per-cycle ratio: ticks = cycles * num_ / den_.
    std::uint64_t num_;
    std::uint64_t den_;
};
