// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// Mega Drive sound chips (Ym2612DeviceAdapter, Sn76489SoundDeviceAdapter) native tests, called from
// native_lifecycle_tests.cpp's main(). The synthetic tests use values worked out from the chip descriptions; the
// replay tests feed recorded register writes to the adapters and compare every sample with the original
// emulator's own sound streams (CHIPESTRA_REFERENCE_DIR; skipped without it).

void Ym2612RegistersTimersAndDac();
void Ym2612ChannelsAddUpToTheChipOutput();
void Ym2612ChunkingAndDeterminism();
void Ym2612ReplayMatchesTheRecordedStreams();
void Sn76489TonesVolumesAndNoise();
void Sn76489ChunkingAndDeterminism();
void Sn76489ReplayMatchesTheRecordedStreams();
