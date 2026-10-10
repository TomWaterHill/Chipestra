// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// GyrussMachine native tests. Declared here so native_lifecycle_tests.cpp's
// main() can call them (one shared test executable, single main()).
//
// Boot sequence / interrupt-driven command injection. All ROM bytes are
// hand-written synthetic programs (no real Gyruss ROM data).

void GyrussMachineRegisteredInDefaultCatalog();
void GyrussMachineExposes16LogicalChannels();
void GyrussMachineBootSequenceReadsAndWrites();
void GyrussMachineCommandInjectionWritesAy1Registers();
void GyrussMachineCommandDispatchTriggersI8039Dac();
void GyrussMachineOnPanOverrideClearedResyncsPan();
void GyrussAyRcFilterCutoffsMatchComponentValues();
void GyrussAyRcFilterBypassDcAndReset();
void GyrussMachineAyFilterFollowsPortB();
void GyrussMixerLowPassMatchesMixerDescription();
void GyrussMixerLowPassResetClearsState();
