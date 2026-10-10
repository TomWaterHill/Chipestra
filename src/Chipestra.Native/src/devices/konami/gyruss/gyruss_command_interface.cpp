// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "gyruss_command_interface.h"

GyrussCommandInterface::GyrussCommandInterface(IZ80Core& z80) : z80_(z80) {}

void GyrussCommandInterface::Send(const MusicCommand& command) {
    latch_ = command.Value;
    irq_pending_ = true;
}

bool GyrussCommandInterface::Tick() {
    if (irq_pending_) {
        z80_.SetInputLine(Z80InputLine::Irq, Z80InputLineState::Assert);
        irq_pending_ = false;
        return true;
    }
    return false;
}

void GyrussCommandInterface::ClearIrqAfterBlock() { z80_.SetInputLine(Z80InputLine::Irq, Z80InputLineState::Clear); }
