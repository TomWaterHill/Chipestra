// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// NesMachine native tests, called from native_lifecycle_tests.cpp's main().
//
// The synthetic tests use a small hand-written 6502 program (no real game data) laid out at $8000 of a
// 32 KB program ROM; the real-ROM test (CHIPESTRA_ROM_ROOT) plays every song and effect of starsoldier.xml
// through the same path the application uses.

void NesMachineRegisteredInDefaultCatalog();
void NesMachineExposesFiveLogicalChannels();
void NesMachineStartupClearsRamAndRunsInitRoutinesInOrder();
void NesMachineFrameRoutinesRunOncePerFrameInOrder();
void NesMachineSendCommandWritesTheDriverVariables();
void NesMachineAcceptsOnlyTheKindsItCanPlay();
void NesMachineSoundRoutinesTouchPpuAndInputStandIns();
void NesMachineSwitchIsIdenticalToAFreshStart();
void NesMachineMuteRecomputesTheMixer();
void NesMachineIsDeterministicAcrossInstances();
void NesMachineRejectsBadCartridgesAndDriverSettings();
void NesDriverConfigParsesAndRejects();
void NesMachineViaDefinitionXmlRealRomPlaysAllSongs();
