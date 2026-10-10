// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// Dac8BitR2RDeviceAdapter native tests. Declared here so native_lifecycle_tests.cpp's main() can
// call them - matches cus30_adapter_tests.h's own precedent (one shared test executable, single main()).

void Dac8BitR2RSingleWriteHoldsForRestOfBlock();
void Dac8BitR2RMultipleWritesLandAtCorrectTickOffsets();
void Dac8BitR2RNoWriteHoldsPreviousBlocksFinalValue();
void Dac8BitR2RGainScalesOutputInFourSteps();
void Dac8BitR2RTwoInstancesAreIndependent();
void Dac8BitR2RReconstructIsDeterministic();
