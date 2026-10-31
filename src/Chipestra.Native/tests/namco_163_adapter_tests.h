// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// Namco 163 sound chip (Namco163SoundDeviceAdapter) native tests, called from native_lifecycle_tests.cpp's main().
// The synthetic tests use hand-made RAM contents and expectations worked out from the register description; the
// last one replays recorded register accesses against the original emulator's own sound stream and needs
// CHIPESTRA_REFERENCE_DIR (it is skipped without it).

void Namco163RamAddressAndAutoIncrement();
void Namco163TimeDivisionFollowsTheChannelCount();
void Namco163ReadsTheWaveformAndKeepsThePhase();
void Namco163VolumeScalesTheSample();
void Namco163ChannelStreamsAddUpToTheChipOutput();
void Namco163MuteSilencesOnlyThatChannel();
void Namco163DisabledChipIsSilentAndStopped();
void Namco163SampleCountFollowsTheOriginalsTiming();
void Namco163RenderIsChunkIndependentAndDeterministic();
void Namco163ReplayMatchesTheRecordedStreams();
