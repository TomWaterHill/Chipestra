// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "devices/cpu/z80/iz80_core.h"
#include "chipestra_abi.h"

#include <cstdint>

// Turns a MusicCommand into the soundlatch write + IRQ pulse that the real
// GX400/Bubble System main 68000 performs (outlatch bit 2 = sound_irq_w, plus the
// GENERIC_LATCH_8 soundlatch). One pulse per command, matching real hardware.
//
// The Z80 IRQ is level-triggered and the Z80 adapter does not auto-clear it, so
// something must deassert it. Holding it up for a whole Render() block would
// re-trigger the handler at every EI;RETI, so GradiusMachine::Render() steps the CPU
// one instruction at a time, watches Snapshot().irqServiceCount and calls
// ClearIrqAfterBlock() the moment the interrupt is taken (the real /IOREQ auto-ack).
// The line is never touched from inside the interrupt-servicing path itself.
class GradiusCommandInterface {
public:
    // `z80` must outlive this object (non-owning).
    explicit GradiusCommandInterface(IZ80Core& z80);

    // Latches command.Value and arms Tick() to assert IRQ on the next block. Equivalent
    // to SetRawLatch() followed by PulseIrq().
    void Send(const MusicCommand& command);

    // The two halves of Send(), exposed separately for the main 68000's bus writes
    // (soundlatch write, then the sound_irq_w pulse).
    void SetRawLatch(std::uint8_t value) { latch_ = value; }
    void PulseIrq() { irq_pending_ = true; }

    // Called once per Render() block before the Z80 runs: asserts IRQ if a pulse is
    // armed. Returns true iff this call performed a fresh Clear->Assert transition.
    bool Tick();

    // Called once per Render() block after the Z80 ran: deasserts IRQ (idempotent).
    void ClearIrqAfterBlock();

    // Returns the latched byte (soundlatch read); no read-clear side effect.
    std::uint8_t ReadLatch() const { return latch_; }

    // Cold-restart hook: forgets a command that was Send()-ed but never delivered and
    // drops the IRQ line, so a stale command is not consumed during the next boot.
    void Reset();

private:
    IZ80Core& z80_;
    std::uint8_t latch_ = 0;
    bool irq_pending_ = false;
};
