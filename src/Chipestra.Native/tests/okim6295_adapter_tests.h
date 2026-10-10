// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// Okim6295DeviceAdapter native tests. Declared here so native_lifecycle_tests.cpp's main() can call them -
// same pattern as k005289_adapter_tests.h/msm5205_adapter_tests.h.

void Okim6295ResetAndIdleStatus();
void Okim6295Pin7LowUsesDivisor165();
void Okim6295StartAndStopVoice();
void Okim6295InvalidStartStopIsIgnored();
void Okim6295AlreadyPlayingVoiceIgnoresNewStart();
void Okim6295RenderProducesOneSamplePerDivisorClocks();
void Okim6295VoicePlaysExactSampleCountThenStops();
void Okim6295MultipleVoicesIndependent();
void Okim6295VolumeAttenuationReducesAmplitude();
void Okim6295ResetDoesNotClearPendingPhraseHalf();
void Okim6295DeterministicAcrossInstances();
