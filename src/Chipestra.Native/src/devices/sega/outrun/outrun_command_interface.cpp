// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "outrun_command_interface.h"

OutRunCommandInterface::OutRunCommandInterface(IZ80Core& z80) : z80_(z80) {}

void OutRunCommandInterface::Send(const MusicCommand& command) {
    latched_value_ = command.Value;
    pending_relatches_ = kMailboxRelatchCount - 1;  // this Send() performs relatch #1 itself
    z80_.SetInputLine(Z80InputLine::Nmi, Z80InputLineState::Assert);
}

std::uint8_t OutRunCommandInterface::ReadLatchAndClearNmi() {
    z80_.SetInputLine(Z80InputLine::Nmi, Z80InputLineState::Clear);
    return latched_value_;
}

void OutRunCommandInterface::Tick() {
    if (pending_relatches_ > 0) {
        --pending_relatches_;
        z80_.SetInputLine(Z80InputLine::Nmi, Z80InputLineState::Assert);
    }
}
