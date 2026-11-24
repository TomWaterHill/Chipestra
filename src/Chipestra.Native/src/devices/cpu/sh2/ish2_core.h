// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include <cstdint>
#include <functional>

// Stable, MAME-independent execution interface of a Hitachi SH-2 (SH7604 class) CPU core. No MAME type appears here. Every
// bus access, opcode fetches and exception vector reads included, goes through the callbacks, with the full 32-bit
// address the program used (the owner decides how the cached/uncached mirrors and the on-chip registers map).
//
// Execute() consumes CPU clock cycles with the per-instruction counts of the MAME interpreter (one cycle for most
// instructions, extra cycles for multiply-accumulate, the read-modify-write byte instructions, delayed branches that are
// not taken as a pair, exceptions are not charged). Memory wait states are not part of the count; the owner adds them.
class ISh2Core {
public:
    virtual ~ISh2Core() = default;

    using Read8Fn = std::function<std::uint8_t(std::uint32_t address)>;
    using Read16Fn = std::function<std::uint16_t(std::uint32_t address)>;
    using Read32Fn = std::function<std::uint32_t(std::uint32_t address)>;
    using Write8Fn = std::function<void(std::uint32_t address, std::uint8_t value)>;
    using Write16Fn = std::function<void(std::uint32_t address, std::uint16_t value)>;
    using Write32Fn = std::function<void(std::uint32_t address, std::uint32_t value)>;
    // An external interrupt of `level` (1..15) was accepted: returns its vector number (the autovector is 64 + level / 2).
    using IrqAcknowledgeFn = std::function<std::uint32_t(int level)>;

    struct Callbacks {
        Read8Fn read8;
        Read16Fn read16;
        Read32Fn read32;
        Write8Fn write8;
        Write16Fn write16;
        Write32Fn write32;
        IrqAcknowledgeFn irqAcknowledge;  // optional; null: the autovector
    };

    virtual void SetCallbacks(Callbacks callbacks) = 0;

    // The reset exception: PC and R15 are read from vector 0 and 1 (addresses 0 and 4), SR gets the interrupt mask 15, the other
    // registers are cleared, pending interrupts are dropped.
    virtual void Reset() = 0;

    // Executes at least `cycleBudget` cycles (an instruction is never split). Returns the cycles consumed.
    virtual std::uint64_t Execute(std::uint64_t cycleBudget) = 0;

    // The IRL lines: an interrupt request of `level` (1..15) is held while the line is asserted. The CPU takes the highest
    // asserted level above the mask of SR.
    virtual void SetInterruptLine(int level, bool asserted) = 0;
    virtual void SetNmi(bool asserted) = 0;  // taken on the Assert transition, vector 11
    // The on-chip interrupt controller's request: `level` (0 = none) with its own vector; taken once, then cleared.
    virtual void SetInternalInterrupt(int level, std::uint32_t vector) = 0;
};
