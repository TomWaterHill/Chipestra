// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "devices/cpu/h6280/ih6280_core.h"
#include "chipestra_abi.h"

#include <cstdint>

// Turns a MusicCommand into the physical sound-latch write plus IRQ1 assert that the real main 68000
// (not emulated; sound-only scope) performs at its address 0x100007 (generic_latch_8; the 68000 writes the
// even base 0x100006 as a word, so the odd byte is covered).
//
// Strategy: immediate latch. A single Send() updates the latch and asserts IRQ1 together.
// IRQ1 is level-triggered: ReadLatchAndClearIrq1() clears it, as reading generic_latch_8 clears its pending
// flag (the H6280 reads the same latched byte at both 0x140000 and 0x140001). Re-asserting IRQ1 for a later
// command is the Machine's job from its Render() loop, never from inside the CPU's interrupt service path.
//
// Command.Value alone becomes the latched byte; what a byte means (a track, or the driver's STOP = 0x00) is
// defined by the sound driver ROM, not by this class.
class VaporTrailCommandInterface {
public:
    // `h6280` must outlive this object (non-owning).
    explicit VaporTrailCommandInterface(IH6280Core& h6280);

    // Latches the value and asserts IRQ1, like the 68000 write to 0x100007.
    void Send(std::uint8_t value);

    // Returns the latched byte and clears IRQ1 (the read-clears-pending side effect). Wired as the H6280
    // read callback for 0x140000/0x140001.
    std::uint8_t ReadLatchAndClearIrq1();

private:
    IH6280Core& h6280_;
    std::uint8_t latched_value_ = 0;
};
