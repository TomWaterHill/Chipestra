// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "vapor_trail_command_interface.h"

VaporTrailCommandInterface::VaporTrailCommandInterface(IH6280Core& h6280) : h6280_(h6280) {}

void VaporTrailCommandInterface::Send(std::uint8_t value) {
    latched_value_ = value;
    h6280_.SetInputLine(H6280InputLine::Irq1, H6280InputLineState::Assert);
}

std::uint8_t VaporTrailCommandInterface::ReadLatchAndClearIrq1() {
    h6280_.SetInputLine(H6280InputLine::Irq1, H6280InputLineState::Clear);
    return latched_value_;
}
