// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// Metal Black native tests, called from native_lifecycle_tests.cpp's main().
//
// Synthetic ROM only (no real Metal Black ROM data). Metal Black is played through an unmodified
// NinjaWarriorsMachine, so these tests only prove the registration (TAITO_F2SYSTEM resolves to a
// working NinjaWarriorsMachine) and the command-injection path once more under that registration;
// the underlying TC0140SYT/Z80/YM2610 mechanics are covered by ninja_warriors_machine_tests.h and
// ym2610_adapter_tests.h.

void MetalBlackMachineConstructsAndRegistersFourteenChannels();
void MetalBlackMachineCommandInjectionDeliversViaTc0140sytToAudiocpuNmi();
void MetalBlackMachineRegisteredInDefaultCatalog();
