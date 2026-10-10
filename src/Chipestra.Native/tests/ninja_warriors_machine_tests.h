// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// NinjaWarriorsMachine native tests, called from native_lifecycle_tests.cpp's main().
//
// Synthetic ROM only (no real Ninja Warriors ROM data): proves the TC0140SYT-substituted
// command-injection path works end to end through a real, emulated audiocpu Z80, in the same
// style as DariusMachineCommandInjectionDeliversViaPc060haToAudiocpuNmi (darius_machine_tests.h)
// for the identical underlying protocol.

void NinjaWarriorsMachineConstructsAndRegistersFourteenChannels();
void NinjaWarriorsMachineCommandInjectionDeliversViaTc0140sytToAudiocpuNmi();
void NinjaWarriorsMachineRepeatedCommandsReAssertNmi();
void NinjaWarriorsMachineRegisteredInDefaultCatalog();
void NinjaWarriorsMachineFmAdpcmChannelsStayCenteredUnlessOverridden();
