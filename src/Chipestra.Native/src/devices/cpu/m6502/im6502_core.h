// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include <cstdint>
#include <functional>

// The externally driven inputs of the Ricoh RP2A03 (the NES's 6502 with the sound generator
// on the same die); the same set MAME's m6502_device exposes.
enum class M6502InputLine {
    Nmi,          // edge-triggered (only the Assert transition latches a request)
    Irq,          // level-triggered, masked by the I flag
    ApuIrq,       // level-triggered, the on-chip sound generator's interrupt line; same vector and masking as Irq
    SetOverflow,  // the SO pin: an Assert transition sets the V flag
};

enum class M6502InputLineState {
    Clear,
    Assert,
};

// Stable, MAME-independent 6502 execution interface. No MAME type appears here; every bus
// access, opcode fetches and the reset/interrupt vector reads included, goes through the
// callbacks. The address space is the CPU's native flat 16 bits.
//
// Execute() consumes CPU clock cycles: one per bus access, as in the original.
class IM6502Core {
public:
    virtual ~IM6502Core() = default;

    using MemoryReadFn = std::function<std::uint8_t(std::uint16_t address)>;
    using MemoryWriteFn = std::function<void(std::uint16_t address, std::uint8_t value)>;

    virtual void SetMemoryCallbacks(MemoryReadFn read, MemoryWriteFn write) = 0;

    // Power-on state: registers and input lines return to their power-on values, then the
    // reset sequence runs (I flag set, PC read from 0xFFFC/0xFFFD, first opcode fetched). The
    // cycles that sequence takes are reported by the next Execute() call.
    virtual void Reset() = 0;

    // Executes at least `cycleBudget` cycles (an instruction is never split, so the budget may
    // be exceeded by part of one instruction, never undershot). Returns the cycles consumed.
    // A jammed CPU (one of the KIL opcodes) consumes the whole budget without doing anything.
    virtual std::uint64_t Execute(std::uint64_t cycleBudget) = 0;

    virtual void SetInputLine(M6502InputLine line, M6502InputLineState state) = 0;
};
