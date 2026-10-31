// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// NES APU (NesApuDeviceAdapter, ported from MAME's nes_apu) native tests. Declared here so
// native_lifecycle_tests.cpp's main() can call them - same pattern as the CPU adapter tests.
//
// Chip only: every register sequence below is hand-written and nothing here touches a ROM.

void NesApuResetState();
void NesApuPulseFrequencyAndDutyCycles();
void NesApuPulseEnvelopeDecayAndLoop();
void NesApuPulseLengthCounterAndHalt();
void NesApuPulseSweep();
void NesApuTriangleWaveformAndLinearCounter();
void NesApuNoiseSequences();
void NesApuStatusRegisterAndChannelEnable();
void NesApuFrameCounterAndIrq();
void NesApuDmcReadsBytesAndRaisesIrq();
void NesApuDmcShiftsBitsLeastSignificantBitFirst();
void NesApuMixerMatchesTheNonLinearTables();
void NesApuPartialSamplesCarryOver();
void NesApuDeterministicAcrossRuns();
