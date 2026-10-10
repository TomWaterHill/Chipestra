// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// SampleRateConverter / ResamplingMixerFeed native tests. Declared
// here so native_lifecycle_tests.cpp's main() can call them - see that file's
// top-of-file comment for why this project uses one shared test executable.
//
// Coverage: DC preservation, amplitude (gain) preservation, block-boundary
// continuity, a simple image/alias check, determinism, and the non-integer
// 62500:192000 ratio path; plus ResamplingMixerFeed's passthrough/resample
// behavior and the end-to-end "YM2151 -> resampler -> Mixer, not silence
// padding" check.

void SrcPreservesDcLevel();
void SrcPreservesSineAmplitude();
void SrcBlockBoundaryIsContinuous();
void SrcIsDeterministic();
void SrcHandlesNonIntegerRatioAndProducesRequestedCount();
void SrcAttenuatesImagesOnUpsample();
void SrcAntiAliasPreservesDcOnDownsample();
void SrcAntiAliasFrequencyResponse();
void SoxrSrcPreservesDcLevel();
void SoxrSrcAntiAliasFrequencyResponse();
void SoxrSrcIsDeterministic();
void SoxrSrcSteadyStateAcrossManySmallBlocksNeedsNoRepeatedPadding();
void ResamplingMixerFeedPassthroughAndResample();
void XBoardMachineYm2151ReachesMixerThroughResampler();
void ResamplingMixerFeedCompensatesGroupDelayAcrossDifferentNativeRates();
