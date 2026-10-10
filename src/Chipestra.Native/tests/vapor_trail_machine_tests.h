// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// VaporTrailMachine native tests, called from native_lifecycle_tests.cpp's main().
//
// Boot warm-up, command -> IRQ1 delivery, IRQ1 clears on latch read, YM2151 IRQ -> IRQ2, each
// device reachable via the memory map, determinism, SendCommand re-entrancy safety. All ROM bytes are
// hand-written synthetic programs (no real Vapor Trail ROM data); a NOP-filled ROM is sufficient
// because every test observes wiring through VaporTrailMachine's test accessors (H6280Snapshot/
// ReadMemory/WriteMemory/Ym2151ForTest/...), not through a hand-crafted driver's interrupt handler:
// VaporTrailCommandInterface's IRQ1 line state and VaporTrailMemoryMap's device windows are both
// directly observable without the CPU executing a real handler.

void VaporTrailMachineRegisteredInDefaultCatalog();
void VaporTrailMachineExposes22LogicalChannels();
void VaporTrailMachineBootHandshakeLeavesNoSongActive();
void VaporTrailMachineSendCommandAssertsIrq1AndLatchesByte();
void VaporTrailMachineReadingLatchClearsIrq1();
void VaporTrailMachineYm2151IrqReachesH6280Irq2();
void VaporTrailMachineDevicesReachableViaMemoryMap();
void VaporTrailMachineRenderIsDeterministicAcrossInstances();
void VaporTrailMachineSendCommandGatesSongSwitchWithStop();
void VaporTrailMachineSoundDeviceSyncTracksCumulativeCyclesAcrossBusAccesses();
