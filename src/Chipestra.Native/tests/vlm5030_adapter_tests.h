// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// Vlm5030DeviceAdapter native tests - declared here, defined in
// vlm5030_adapter_tests.cpp, called from native_lifecycle_tests.cpp's
// main() (matching k005289_adapter_tests.h's own convention).

void Vlm5030RegisterAndPinSemanticsUpdateInternalState();
void Vlm5030FrameParsingDecodesKnownBitPackedVoiceData();
void Vlm5030ChannelOutputReachesItsBuffer();
void Vlm5030FallingEnergyInterpolationStaysWithinTheFrameEnergies();
