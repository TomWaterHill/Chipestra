// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// Analog output-stage filter tests (frequency response of each mode, exact pass-through when Off,
// crossfade on a mode change, the C ABI entry point and the channel marks of the Mega Drive
// machines). Called from native_lifecycle_tests.cpp's main().

void AnalogFilterOffIsAnExactPassThrough();
void AnalogFilterModel1FrequencyResponseMatchesTheFirstOrderDesign();
void AnalogFilterModel2FrequencyResponseMatchesTheTwoStageDesign();
void AnalogFilterOnlyTouchesMarkedChannels();
void AnalogFilterModeChangeCrossfadesWithoutAStep();
void AnalogFilterAbiValidatesArguments();
void MegaDriveMachinesMarkTheirFmAndPsgChannelsForTheAnalogFilter();
