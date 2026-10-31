// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// Namco 163 (mapper 19) support of the NES Machine, called from native_lifecycle_tests.cpp's main(). The synthetic
// tests use a hand-written 128 KB program; the real-ROM test (CHIPESTRA_ROM_ROOT with the game, and
// CHIPESTRA_REFERENCE_DIR) plays every request of the sound driver and compares the APU and Namco 163 writes
// with a recording of the real game.

void Namco163PrgMapsBanksAndDecodesTheSoundRegisters();
void NesNamco163CartridgeAndDriverSettings();
void NesNamco163MachineInputsAreChecked();
void NesMachineNamco163BankingSoundPortAndChannels();
void RollingThunderViaDefinitionXmlRealRomMatchesTheRealGame();
