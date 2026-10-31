// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// VRC7 (mapper 85) support of the NES Machine and the per-<rom> archive of the game definition, called from
// native_lifecycle_tests.cpp's main(). The synthetic tests use a hand-written 512 KB program and made-up
// instrument tables; the real-ROM test (CHIPESTRA_ROM_ROOT with the game and ds1001.zip, and
// CHIPESTRA_REFERENCE_DIR) plays every request of lagrange.xml's game and compares the APU and VRC7 writes with a
// recording of the real game.

void Vrc7PrgMapsBanksAndDecodesTheWiring();
void NesVrc7CartridgeAndDriverSettings();
void GameDefinitionRomArchiveResolvesFromTwoArchives();
void NesVrc7MachineInputsAreChecked();
void NesMachineVrc7BankingWramFrameCounterAndSound();
void LagrangeViaDefinitionXmlRealRomMatchesTheRealGame();
