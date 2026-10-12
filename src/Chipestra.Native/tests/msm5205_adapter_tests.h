// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// Msm5205DeviceAdapter native tests. Declared here so
// native_lifecycle_tests.cpp's main() can call them.
//
// All stimulus is synthetic (hand-written data-pin writes); nothing reads
// real ROM/speech data.

void Msm5205RenderProducesOneSamplePerPeriodAndCarriesPartialClocks();
void Msm5205VckHandlerFiresBeforeThatSamplesDecode();
void Msm5205ResetPinForcesSilenceRegardlessOfData();
void Msm5205RepeatedStrongNibbleProducesGrowingNonSilentSignal();
void Msm5205SlaveDecodesOncePerWriteAtTheWriteTime();
void Msm5205SlaveResetBitAndDataNibble();
void Msm5205SlaveWriteTimingIsIndependentOfRenderChunking();
