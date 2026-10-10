// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// Ym2610DeviceAdapter native tests. Declared here so
// native_lifecycle_tests.cpp's main() can call them - matches the
// ym2151_adapter_tests.h/ym2203_adapter_tests.h convention.
//
// Tests are ordered: register semantics -> ADPCM-A/B sample playback ->
// FM/SSG channel output -> determinism. All stimulus is synthetic
// (hand-written register writes, small synthetic ADPCM sample bytes);
// nothing reads a real ROM.

void Ym2610RegisterAccessAndStatusBusy();
void Ym2610FmChannelOutputReachesPerChannelBuffers();
void Ym2610FmHighBankChannelsHonourPanRegister();
void Ym2610AdpcmAChannelPlaysSampleFromExternalRom();
void Ym2610AdpcmBChannelPlaysSampleFromExternalRom();
void Ym2610SsgChannelOutputReachesPerChannelBuffers();
void Ym2610RenderIsDeterministicAndLosesNoChipTime();
