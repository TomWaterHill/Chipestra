// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// Sunsoft 5B sound chip (Sunsoft5bSoundDeviceAdapter) native tests, called from native_lifecycle_tests.cpp's main().
// The synthetic tests use register values and expectations worked out from the chip description; the last one replays
// recorded register writes against the original emulator's own sound stream and needs CHIPESTRA_REFERENCE_DIR (it
// is skipped without it).

void Sunsoft5bRegistersAndAddressLatch();
void Sunsoft5bToneAndNoiseTiming();
void Sunsoft5bEnvelopeSteps();
void Sunsoft5bMixTable();
void Sunsoft5bChannelsAddUpAndMuteTakesAChannelOut();
void Sunsoft5bSampleCountChunksAndDeterminism();
void Sunsoft5bReplayMatchesTheRecordedStreams();
