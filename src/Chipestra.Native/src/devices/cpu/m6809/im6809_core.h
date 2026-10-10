// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include <cstdint>
#include <functional>

// The MC6809E's three externally driven interrupt inputs (the same NMI/FIRQ/IRQ split
// MAME's m6809_base_device exposes); the 6809 has no on-chip timer.
enum class M6809InputLine {
    Nmi,   // edge-triggered (only the Assert transition latches a request); masked entirely
           // until the first write to S (m_lds_encountered in MAME)
    Firq,  // level-triggered, masked by CC bit F
    Irq,   // level-triggered, masked by CC bit I
};

enum class M6809InputLineState {
    Clear,
    Assert,
};

// Stable, MAME-independent Motorola MC6809E execution interface. No MAME type appears
// here; every bus access goes through callbacks, and the address space is the CPU's native
// flat 16 bits (no external bank switching, unlike the H6280's MMU).
//
// Execute() consumes E/Q cycles (MAME's m_icount unit). A Machine wanting MC6809E vs MC6809
// vs HD63B09E clock-divider differences applies that divider itself via ClockConverter.
class IM6809Core {
public:
    virtual ~IM6809Core() = default;

    using MemoryReadFn = std::function<std::uint8_t(std::uint16_t address)>;
    using MemoryWriteFn = std::function<void(std::uint16_t address, std::uint8_t value)>;

    // Flat 16-bit address space. No internal I/O page (unlike H6280) - every
    // access, opcode fetch included, goes through these callbacks.
    virtual void SetMemoryCallbacks(MemoryReadFn read, MemoryWriteFn write) = 0;

    // Power-on reset: DP=0, CC I and F set (interrupts masked), PC read from
    // the reset vector (0xFFFE/0xFFFF) through the memory callbacks.
    virtual void Reset() = 0;

    // Executes at least `cycleBudget` E/Q cycles (an instruction is never
    // split, so the budget may be exceeded by part of one instruction, never
    // undershot - same rule as the other cores). Returns the cycles consumed.
    //
    // SYNC/CWAI: MAME blocks execute_run() in a spin loop until an interrupt is asserted.
    // Here the wait is driven by Execute()'s own budget: if no interrupt is pending, the
    // call consumes exactly cycleBudget and the same wait resumes on the next call, so it
    // never blocks inside a single call.
    virtual std::uint64_t Execute(std::uint64_t cycleBudget) = 0;

    virtual void SetInputLine(M6809InputLine line, M6809InputLineState state) = 0;
};
