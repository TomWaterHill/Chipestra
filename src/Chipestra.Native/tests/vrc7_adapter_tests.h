// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// Konami VRC7 sound chip (Vrc7SoundDeviceAdapter, ymfm's ds1001) native tests. Declared here so
// native_lifecycle_tests.cpp's main() can call them - same pattern as the other adapter tests.
//
// The synthetic tests use made-up instrument tables and hand-written register sequences. The last one replays
// recorded register writes against the original emulator's own sound stream and needs the instrument ROM the
// user supplies (CHIPESTRA_ROM_ROOT) and the recordings (CHIPESTRA_REFERENCE_DIR); it is skipped without them.

void Vrc7InstrumentTableIsRequired();
void Vrc7RenderCountsSamplesAndCarriesTheRemainder();
void Vrc7UserInstrumentSoundsOnlyOnItsChannel();
void Vrc7BuiltInInstrumentReadsTheTable();
void Vrc7ChannelsAddUpToTheChipOutput();
void Vrc7MuteZeroesOnlyThatChannel();
void Vrc7ChannelsSevenToNineAndRhythmAreIgnored();
void Vrc7RenderIsChunkIndependentAndDeterministic();
void Vrc7ReplayMatchesTheRecordedStream();
