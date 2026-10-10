// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// Cus30DeviceAdapter native tests. Declared here so native_lifecycle_tests.cpp's main() can call
// them - matches k005289_adapter_tests.h's own precedent (one shared test executable, single main()).

void Cus30WaveRamIsPlainReadWrite();
void Cus30VoiceRegisterDecodeSetsFrequencyVolumeWaveformSelect();
void Cus30Register04SetsNextVoiceNoiseSw();
void Cus30Register04WrapsFromVoice7ToVoice0();
void Cus30SharedRamIsPlainReadWrite();
void Cus30Register5ReturnsLiveCounterPosition();
void Cus30RenderIsSilentWhenBothVolumesZero();
void Cus30RenderCounterFreezesWhileSilent();
void Cus30RenderProducesNonzeroSampleWhenActive();
void Cus30ResetClearsVoiceStateAndMemory();
