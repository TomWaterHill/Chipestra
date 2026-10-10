// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include <cstdint>
#include <functional>

// Stable, MAME-independent Motorola 68000 CPU execution interface. No MAME type appears
// in this header or in any implementation reachable through it, and an implementation owns
// no memory: every bus access goes through the callbacks below.
//
// Memory callbacks are byte-granular (like IZ80Core) even though the real bus is 16 bits
// wide and big-endian; an implementation assembles 16/32-bit accesses from byte calls.
class IM68000Core {
public:
    virtual ~IM68000Core() = default;

    using MemoryReadFn = std::function<std::uint8_t(std::uint32_t address)>;
    using MemoryWriteFn = std::function<void(std::uint32_t address, std::uint8_t value)>;

    // Installs the callbacks used for every memory access (opcode fetch, operand fetch and
    // data read/write alike). Separate program/opcode address spaces are not modeled.
    virtual void SetMemoryCallbacks(MemoryReadFn read, MemoryWriteFn write) = 0;

    // Installs a callback invoked on every autovectored interrupt taken. It is a
    // notification hook for tests/diagnostics only; autovectoring (vector = 24 + level) is
    // always used.
    using AutovectorAckFn = std::function<void(std::uint8_t level)>;
    virtual void SetAutovectorAcknowledgeCallback(AutovectorAckFn callback) = 0;

    // Resets to the RESET-exception state: SSP loaded from the long at address 0, PC from
    // the long at address 4, S=1, T=0, interrupt mask=7.
    virtual void Reset() = 0;

    // Executes instructions until at least `cycleBudget` clock cycles have elapsed. An
    // instruction is never split, so the budget may be slightly exceeded. Returns the cycles
    // consumed.
    virtual std::uint64_t Execute(std::uint64_t cycleBudget) = 0;

    // Sets the interrupt priority level, modeling the combined IPL0-2 pins. 0 means no
    // interrupt pending; 1-6 are maskable against the SR interrupt mask; 7 is always taken.
    // The Machine must present the highest currently-asserted level and call this again
    // whenever it changes, including back to 0 (level-triggered; software clears each
    // source explicitly).
    // re-derive and re-present the resulting level after every such clear).
    virtual void SetInterruptLevel(std::uint8_t level) = 0;
};
