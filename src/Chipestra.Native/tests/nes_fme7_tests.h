// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// FME-7 / Sunsoft 5B (mapper 69) support of the NES Machine and the way the Machine treats control tracks and
// request numbers, called from native_lifecycle_tests.cpp's main(). The synthetic tests use a hand-written 256 KB
// program; the real-ROM test (CHIPESTRA_ROM_ROOT with the game, and CHIPESTRA_REFERENCE_DIR) plays every request of
// the sound driver and compares the APU and 5B writes with a recording of the real game.

void Fme7PrgMapsBanksAndRoutesTheSoundRegisters();
void NesFme7CartridgeAndDriverSettings();
void NesMachineFme7BankingSoundAndChannels();
void NesMachineControlTracksMakeNoRequestAndZeroIsARequest();
void GimmickViaDefinitionXmlRealRomMatchesTheRealGame();
