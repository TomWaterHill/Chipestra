// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "xboard_command_interface.h"

XBoardCommandInterface::XBoardCommandInterface(IZ80Core& z80) : z80_(z80) {}

void XBoardCommandInterface::Send(const MusicCommand& command) {
    latched_value_ = command.Value;
    z80_.SetInputLine(Z80InputLine::Nmi, Z80InputLineState::Assert);
}

std::uint8_t XBoardCommandInterface::ReadLatchAndClearNmi() {
    z80_.SetInputLine(Z80InputLine::Nmi, Z80InputLineState::Clear);
    return latched_value_;
}
