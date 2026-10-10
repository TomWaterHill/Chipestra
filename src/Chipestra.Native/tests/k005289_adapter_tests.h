// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// K005289DeviceAdapter native tests - declared here, defined in
// k005289_adapter_tests.cpp, called from native_lifecycle_tests.cpp's
// main() (matching ay8910_adapter_tests.h's own convention).

void K005289RegisterSemanticsUpdateInternalState();
void K005289WaveformGenerationMatchesProgrammedFrequencyAndProm();
void K005289ChannelOutputReachesPerVoiceBuffers();
