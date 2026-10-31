// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// Famicom Disk System sound chip (Rp2c33SoundDeviceAdapter, ported from MAME's rp2c33_sound_device)
// native tests. Declared here so native_lifecycle_tests.cpp's main() can call them - same pattern
// as the other adapter tests.
//
// Chip only: every register sequence is hand-written and nothing here touches a ROM or a disk image.

void Rp2c33ResetState();
void Rp2c33SoundIoEnableGatesTheRegisters();
void Rp2c33WavetableWriteNeedsTheWriteEnable();
void Rp2c33WavePlaybackAndFrequency();
void Rp2c33WaveHaltFreezesTheAddress();
void Rp2c33VolumeEnvelope();
void Rp2c33EnvelopeSpeedRegisterAndHalt();
void Rp2c33ModulatorTableAndPosition();
void Rp2c33ModulatorBendsThePitch();
void Rp2c33MasterVolumeAndGainClamp();
void Rp2c33ReadbackRegisters();
void Rp2c33RenderIsChunkIndependentAndDeterministic();
