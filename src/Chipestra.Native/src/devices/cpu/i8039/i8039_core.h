// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include <cstdint>
#include <functional>

// External interrupt pin of the i8039. The EA pin is irrelevant (the part has no
// internal ROM), so only a single IRQ line is modeled.
enum class I8039InputLine {
    Irq,  // External ~INT pin (level-triggered; the CPU clears it via a Port 2 write)
};

enum class I8039InputLineState {
    Clear,
    Assert,
};

// Stable, MAME-independent interface of the Intel i8039 (MCS-48 family) CPU. No MAME
// types appear; all bus accesses go through callbacks.
//
// Scope: external program ROM only (no internal ROM / EA switching); no UPI-41 extensions
// (DBB/STS, EN DMA, JOBF, ...) and no i8021/i8022 special opcodes, as the i8039 has none.
// The 128-byte internal RAM (register banks and software stack) is owned by the adapter.
//
// Execute() consumes the CPU's own input clocks (raw XTAL count); one machine cycle is
// 15 input clocks.
class I8039Core {
public:
    virtual ~I8039Core() = default;

    using ProgramReadFn = std::function<std::uint8_t(std::uint16_t address)>;

    // Program space (12-bit) read callback; the only program fetch path (no internal ROM).
    virtual void SetProgramMemoryCallback(ProgramReadFn read) = 0;

    using BusReadFn = std::function<std::uint8_t(std::uint8_t address)>;
    using BusWriteFn = std::function<void(std::uint8_t address, std::uint8_t value)>;

    // External data bus callbacks (MOVX, OUTL/INS BUS, ANL/ORL BUS). address is R0/R1 for
    // MOVX and always 0 for the BUS instructions (the BUS pins carry no address).
    virtual void SetBusCallbacks(BusReadFn read, BusWriteFn write) = 0;

    using PortReadFn = std::function<std::uint8_t()>;
    using PortWriteFn = std::function<void(std::uint8_t value)>;

    // Port 1/Port 2: latched 8-bit ports. IN returns the latch ANDed with the external
    // input (open-drain with pull-up); with no read callback the input reads 0xFF.
    virtual void SetPort1Callbacks(PortReadFn read, PortWriteFn write) = 0;
    virtual void SetPort2Callbacks(PortReadFn read, PortWriteFn write) = 0;

    using TestInputFn = std::function<bool()>;

    // T0/T1 test inputs (JT0/JNT0/JT1/JNT1, event counter mode). Unset reads as true
    // (pull-up).
    virtual void SetTestInputCallbacks(TestInputFn t0, TestInputFn t1) = 0;

    // Power-on reset (PC=0, Port 1/2 inputs 0xFF, interrupt/timer enables cleared).
    virtual void Reset() = 0;

    // Executes instructions for at least `clockBudget` input clocks (an instruction is
    // never split, so the budget may be slightly exceeded). Returns clocks consumed.
    virtual std::uint64_t Execute(std::uint64_t clockBudget) = 0;

    // Sets the level of the external IRQ line.
    virtual void SetInputLine(I8039InputLine line, I8039InputLineState state) = 0;
};
