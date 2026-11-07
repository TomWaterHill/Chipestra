// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// MegaDrive68kMachine native tests, called from native_lifecycle_tests.cpp's main(). The synthetic tests use a small 68000
// program written for the test; the real-cartridge test needs CHIPESTRA_ROM_ROOT (the cartridge) and CHIPESTRA_REFERENCE_DIR
// (recordings of the original emulator: md/sonic1/real_game/<normal|fine>/boot_12s.txt) and is skipped without them.

void MegaDrive68kDriverConfigIsParsedAndChecked();
void MegaDrive68kSyntheticDriverRunsAndRequestsReachTheChips();
void MegaDrive68kCallRequestsReachTheGamesRoutines();
void MegaDrive68kRequestCanNameAByteOfTheGamesRam();
void MegaDrive68kZ80ResetCanBeReleasedAtTheStart();
void MegaDrive68kBusRulesBetweenTheCpus();
void MegaDrive68kZ80StartsWhenItsResetIsReleased();
void MegaDrive68kRealCartridgeMatchesTheOriginalsChipWrites();
void MegaDrive68kBattleManiaReplayMatchesTheOriginalsChipWrites();
void MegaDrive68kBattleMania2ReplayMatchesTheOriginalsChipWrites();
void MegaDrive68kGauntlet4ReplayMatchesTheOriginalsChipWrites();
void MegaDrive68kPhantasyStar4ReplayMatchesTheOriginalsChipWrites();
void MegaDrive68kGamesOwnHandlersHorizontalInterruptsAndPointerRequests();
void MegaDrive68kStarCruiserReplayMatchesTheOriginalsChipWrites();
