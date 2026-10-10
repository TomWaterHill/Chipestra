// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// GradiusMachine native tests. Declared here so native_lifecycle_tests.cpp's
// main() can call them (one shared test executable with a single main()).
//
// Boot sequence / interrupt-driven command injection (reset/vector/read/
// write/interrupt wiring). All ROM bytes are hand-written synthetic programs
// (no real Gradius ROM data).

void GradiusMachineBootSequenceReadsAndWrites();
void GradiusMachineCommandInjectionTriggersIrq();
void GradiusMachineRepeatedCommandsReAssertIrq();
void GradiusMachineRegisteredInDefaultCatalog();

// Main 68000 CPU integration.
void GradiusMachineMainCpuSharedRamBridgeReachesSoundZ80();
void GradiusMachineMainCpuSharedRamBridgeDropsEvenByteWrite();
void GradiusMachineMainCpuSoundlatchAndOutlatchReachZ80Irq();
