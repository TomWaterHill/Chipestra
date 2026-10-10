// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// Ym2151DeviceAdapter native tests. Declared here so
// native_lifecycle_tests.cpp's main() can call them (one shared test
// executable with a single main()).
//
// Tests are ordered: register semantics -> timer behavior -> IRQ
// assert/clear -> channel output -> (integration) NMI/IRQ non-conflict. All
// stimulus is synthetic (hand-written register writes and Z80 byte
// programs); nothing reads a real ROM.

void Ym2151RegisterAccessAndStatusBusy();
void Ym2151TimerAFiresAfterProgrammedPeriod();
void Ym2151TimerBFiresAfterProgrammedPeriod();
void Ym2151TimerIrqAssertsAndClears();
void Ym2151ChannelOutputReachesPerChannelBuffers();
void Ym2151RenderIsDeterministicAndLosesNoChipTime();
void XBoardMachineYm2151IrqAndCommandNmiDoNotConflict();
