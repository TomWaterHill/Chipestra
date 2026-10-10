// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "devices/cpu/z80/iz80_core.h"
#include "chipestra_abi.h"

#include <cstdint>

// Turns a MusicCommand into the command-latch write plus NMI assert that the
// real X-Board main CPU performs (MAME: sega_315_5250_compare_timer_device::
// write()/write_to_sound()).
//
// A single Send() updates the latch and asserts NMI atomically, as on the
// hardware: no timed pulse, multi-step sequence or boot handshake is needed.
//
// Command.Value alone becomes the latched byte; which byte means which
// track/stop is a property of the sound driver ROM, so every MusicCommandKind
// writes Value as-is.
class XBoardCommandInterface {
public:
    // `z80` must outlive this object (non-owning).
    explicit XBoardCommandInterface(IZ80Core& z80);

    // Latches command.Value and asserts NMI, like the hardware's write_to_sound().
    void Send(const MusicCommand& command);

    // Returns the latched byte and clears NMI (the read-clears-the-interrupt
    // behaviour of the hardware). Wired as the Z80's I/O port 0x40 read.
    std::uint8_t ReadLatchAndClearNmi();

private:
    IZ80Core& z80_;
    std::uint8_t latched_value_ = 0;
};
