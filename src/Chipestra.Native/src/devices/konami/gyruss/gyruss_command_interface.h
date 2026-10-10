// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "devices/cpu/z80/iz80_core.h"
#include "chipestra_abi.h"

#include <cstdint>

// Turns a MusicCommand into the soundlatch write (`0xc100`, command byte) and
// the IRQ pulse (`0xc080`, a one-instruction Z80 IM1 pulse) that the real
// main CPU (not emulated; this is a sound-only player) performs. Same
// soundlatch + HOLD_LINE-IRQ pattern as GradiusCommandInterface; there is no
// nibble handshake.
//
// The IRQ is level-triggered and the Z80 adapter re-checks it at every
// instruction boundary, so it must be deasserted as soon as the CPU takes the
// interrupt, from GyrussMachine::Render()'s priming loop, never from inside
// the interrupt-servicing path.
class GyrussCommandInterface {
public:
    // `z80` must outlive this object (non-owning).
    explicit GyrussCommandInterface(IZ80Core& z80);

    // Latches command.Value (the byte written to 0xc100) and arms Tick() to
    // pulse IRQ (0xc080) on the next block.
    void Send(const MusicCommand& command);

    // Called once per Render() block before the Z80 executes: asserts IRQ if
    // Send() armed a pulse. Returns true iff this call asserted the line.
    bool Tick();

    // Called once per Render() block after the Z80 has executed: deasserts IRQ
    // (idempotent).
    void ClearIrqAfterBlock();

    // Returns the latched byte (the 0x8000 soundlatch read). No side effects.
    std::uint8_t ReadLatch() const { return latch_; }

private:
    IZ80Core& z80_;
    std::uint8_t latch_ = 0;
    bool irq_pending_ = false;
};
