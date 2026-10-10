// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "gradius_command_interface.h"

GradiusCommandInterface::GradiusCommandInterface(IZ80Core& z80) : z80_(z80) {}

void GradiusCommandInterface::Send(const MusicCommand& command) {
    latch_ = command.Value;
    irq_pending_ = true;
}

bool GradiusCommandInterface::Tick() {
    if (irq_pending_) {
        z80_.SetInputLine(Z80InputLine::Irq, Z80InputLineState::Assert);
        irq_pending_ = false;
        return true;
    }
    return false;
}

void GradiusCommandInterface::ClearIrqAfterBlock() {
    z80_.SetInputLine(Z80InputLine::Irq, Z80InputLineState::Clear);
}

void GradiusCommandInterface::Reset() {
    latch_ = 0;
    irq_pending_ = false;
    z80_.SetInputLine(Z80InputLine::Irq, Z80InputLineState::Clear);
}
