// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// Bubble System BIOS-only configuration (BubbleSystemBoardMode::BiosOnly)
// native tests. Synthetic Z80 ROM only for the gate-safe tests; the real-ROM
// bring-up test skips itself when CHIPESTRA_ROM_ROOT is not set.

void BubbleSystemBiosVariantIsRegisteredAndNeedsOnlySoundResources();
void BubbleSystemBiosModeNeverRunsMainCpuAndPulsesSoundNmiPerFrame();
void BubbleSystemBiosModeIsDeterministic();
void BubbleSystemBiosModeDeliversPlayerCommandsToSoundZ80();
void BubbleSystemBiosModeSendCommandRestartsBoardFromPowerOn();
void BubbleSystemBiosModeStaleCommandDoesNotLeakIntoNextColdRestart();
void BubbleSystemPublishesOnlyTheBiosOnlyEntry();
void BubbleSystemBiosDefinitionXmlIsValid();
void BubbleSystemBiosViaDefinitionXmlRealRomBringUp();
