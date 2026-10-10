// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// DariusMachine native tests, called from native_lifecycle_tests.cpp's main().
//
// Synthetic ROM only (no real Darius ROM data): checks the two-Z80
// coordination model and the PC060HA-substituted command-injection path
// end to end, not just each piece in isolation.

void DariusMachineConstructsAndRegistersThirteenChannels();
void DariusMachineTwoZ80sCoordinateViaSharedAdpcmLatch();
void DariusMachineCommandInjectionDeliversViaPc060haToAudiocpuNmi();
void DariusMachineRepeatedCommandsReAssertNmi();
void DariusMachineFirstCommandAfterBootSendsNoStop();
void DariusMachineSwitchSendsStopThenTheCommand();
void DariusMachineResetForgetsTheActiveSong();
void DariusMachineRegisteredInDefaultCatalog();
void DariusMachineOnPanOverrideClearedResyncsPan();
