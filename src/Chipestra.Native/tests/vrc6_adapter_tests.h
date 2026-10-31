// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// Konami VRC6 sound chip (Vrc6SoundDeviceAdapter, ported from MAME's vrc6snd_device) native tests. Declared
// here so native_lifecycle_tests.cpp's main() can call them - same pattern as the other adapter tests.
//
// Chip only: every register sequence is hand-written and nothing here touches a ROM.

void Vrc6ResetState();
void Vrc6PulsePeriodAndDuty();
void Vrc6PulseConstantVolumeMode();
void Vrc6SawtoothAccumulation();
void Vrc6FrequencyControl();
void Vrc6EnableResetsThePhase();
void Vrc6BusAddressesReachTheRegisters();
void Vrc6MuteZeroesOnlyTheOutput();
void Vrc6RenderIsChunkIndependentAndDeterministic();
