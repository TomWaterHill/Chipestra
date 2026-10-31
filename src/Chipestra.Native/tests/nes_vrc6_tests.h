// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// VRC6 (mappers 24 and 26) support of the NES Machine, called from native_lifecycle_tests.cpp's main(). The
// synthetic tests use a hand-written 128 KB program; the real-ROM test (CHIPESTRA_ROM_ROOT and
// CHIPESTRA_REFERENCE_DIR) plays every request of akumajou.xml's game and compares the APU and VRC6 writes with
// a recording of the real game.

void Vrc6PrgMapsBanksLikeTheOriginal();
void Vrc6PrgMapper26SwapsTheAddressLines();
void NesVrc6CartridgeAndDriverSettings();
void NesMachineVrc6BankingRequestCallAndSound();
void AkumajouViaDefinitionXmlRealRomMatchesTheRealGame();
void AkumajouVoicesDecodeLikeTheRomAndMatchTheNsf();
void AkumajouDmcReadsDependOnTheBankOnlyForTheVoices();
