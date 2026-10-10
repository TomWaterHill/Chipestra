// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "devices/cpu/z80/iz80_core.h"
#include "chipestra_abi.h"

#include <cstdint>

// Turns a MusicCommand into the command-latch write plus NMI assert that the
// real Out Run main CPU performs via the 315-5195 memory mapper (MAME's
// sega_315_5195_mapper_device::write()/write_to_sound()/pread()). The chip
// differs from X-Board's 315-5250, but the latch + NMI / read-clears-NMI
// behaviour is the same.
//
// Unlike XBoardCommandInterface, a single Send() asserts NMI several times.
// The real hardware asserts this NMI continuously (about 65-180Hz) whatever the
// main CPU wants, and the sound driver's NMI handler advances an 8-slot
// ring-buffer pointer on every service, while the driver's main loop only ever
// checks one fixed slot for a pending command. The continuous re-assertion
// guarantees that slot is revisited soon. There is no main CPU here, so Send()
// arms kMailboxRelatchCount NMI services, consumed one per
// OutRunMachine::Render() block via Tick().
//
// The re-latches must be spaced across Render() blocks, not fired back-to-back
// and not re-asserted from inside ReadLatchAndClearNmi(). Re-asserting from the
// handler's own I/O read raises a new NMI edge before the running handler has
// executed its RETN, so the Z80 core re-enters the handler on top of itself;
// each nested entry pushes a return address that is never popped, which leaves
// SP permanently offset and corrupts RAM.
class OutRunCommandInterface {
public:
    // `z80` must outlive this object (non-owning).
    explicit OutRunCommandInterface(IZ80Core& z80);

    // Latches command.Value, asserts NMI once immediately (the first of
    // kMailboxRelatchCount services) and arms Tick() to deliver the rest. A
    // Send() during a burst restarts the count with the new value.
    void Send(const MusicCommand& command);

    // Returns the latched byte and clears NMI (read-clears-the-interrupt, as in
    // pread()). Wired as the Z80's I/O port 0x40 read. It never re-latches here
    // (see the class comment).
    std::uint8_t ReadLatchAndClearNmi();

    // Called once per OutRunMachine::Render() block. If a burst still owes
    // re-latches, asserts NMI again with the latched value; otherwise a no-op.
    void Tick();

private:
    // The driver's command-mailbox ring buffer has 8 slots (an 8-bit counter is
    // compared against a fixed limit of 8 to gate whether the pointer advances or
    // wraps). Re-latching this many times guarantees the pointer visits slot 0,
    // the only one the main loop reads, whatever was sent before. Extra
    // re-latches are harmless.
    static constexpr int kMailboxRelatchCount = 8;

    IZ80Core& z80_;
    std::uint8_t latched_value_ = 0;
    // Re-latches still owed after the initial Send() assert; 0 between commands.
    int pending_relatches_ = 0;
};
