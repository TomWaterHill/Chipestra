// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include <cstdint>
#include <functional>

// Hd63701InputLine/Hd63701InputLineState - the HD63701V0's externally driven
// interrupt inputs. Only the two pins Namco System 1 wires to the MCU are
// exposed: NMI and the standard IRQ1 pin. The internal-only "IRQ2" request
// (raised by the on-chip timer/SCI) is serviced by the core itself and has no
// entry here. MAME's TIN (timer input capture), IS3 (port 3 handshake strobe)
// and STBY (standby) inputs are not exposed: nothing on this board drives them.
enum class Hd63701InputLine {
    Nmi,  // edge-triggered (only the Assert transition latches a request)
    Irq1, // level-triggered, masked by CC bit I (0x10)
};

enum class Hd63701InputLineState {
    Clear,
    Assert,
};

// IHd63701Core - stable, MAME-independent Hitachi HD63701V0 (6801-family MCU
// used as Namco System 1's CUS64 custom chip) execution interface. Same design
// idea as IZ80Core/IM68000Core/IH6280Core/IM6809Core/I8039Core: no MAME type
// appears here, every bus access goes through callbacks, and addresses are the
// CPU's native flat 16-bit space (see MameDerivedHd63701Adapter for which
// addresses the core intercepts as on-chip peripherals).
//
// Time model: Execute() consumes the CPU's own native cycle (MAME's m_icount
// unit for this device); a Machine applies any input-clock divider itself via
// ClockConverter.
class IHd63701Core {
public:
    virtual ~IHd63701Core() = default;

    using MemoryReadFn = std::function<std::uint8_t(std::uint16_t address)>;
    using MemoryWriteFn = std::function<void(std::uint16_t address, std::uint8_t value)>;

    // Flat 16-bit address space. Every access outside the core's own
    // intercepted on-chip I/O/RAM window (see the adapter's class comment)
    // goes through these callbacks, opcode fetches included.
    virtual void SetMemoryCallbacks(MemoryReadFn read, MemoryWriteFn write) = 0;

    // Power-on reset: CC = 0xC0 then SEI (I flag set, interrupts masked; CC
    // becomes 0xD0), PC read from the reset vector (0xFFFE/0xFFFF) through
    // the memory callbacks, on-chip peripheral registers reset to their
    // documented power-on state (port DDRs cleared, TCSR/TRCSR/RMCR reset,
    // free-running counter cleared, output-compare defaulted to 0xFFFF).
    virtual void Reset() = 0;

    // Executes at least `cycleBudget` cycles (an instruction is never split,
    // so the budget may be exceeded by part of one instruction, never
    // undershot - same rule as the other cores). Returns the cycles consumed.
    //
    // WAI/SLP: an interrupt already pending when the instruction executes is
    // taken with no idle at all. Otherwise the wait is driven by Execute()'s
    // per-call budget: each call checks for a pending interrupt once per
    // outstanding cycle, returns having consumed exactly cycleBudget if none
    // arrived, and resumes the same wait on the next call.
    virtual std::uint64_t Execute(std::uint64_t cycleBudget) = 0;

    virtual void SetInputLine(Hd63701InputLine line, Hd63701InputLineState state) = 0;
};
