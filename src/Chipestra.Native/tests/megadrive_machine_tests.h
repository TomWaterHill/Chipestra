// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// MegaDriveMachine native tests, called from native_lifecycle_tests.cpp's main(). The synthetic tests use small Z80
// programs written for the test; the real-cartridge tests need CHIPESTRA_ROM_ROOT (the cartridge) and
// CHIPESTRA_REFERENCE_DIR (recordings of the original emulator) and are skipped without them.

void MegaDriveCartridgeDecodingAndDriverConfig();
void MegaDriveInterruptIsOneScanlineLongAndLostWhenDisabled();
void MegaDriveBankRegisterAndChipPorts();
void MegaDriveSwitchingStartsAfresh();
void MegaDriveViaDefinitionXmlMatchesTheOriginalsChipWrites();
void MegaDriveCompressedDriverSongsAndConstants();
void MegaDriveRegionSetsTheClocksAndTheFrame();
void MegaDriveRegionListSelectsOneRegion();
void MegaDriveYm2612StatusShowsBusyAfterADataWrite();
void MegaDriveSorcerianMatchesTheOriginalsChipWrites();
void MegaDriveTimeTraxMatchesTheOriginalsChipWrites();
void MegaDriveVoicesPlayTheSameSamplesAsTheOriginal();
void MegaDriveSwitchTimeReport();
void MegaDriveRestoredStateSoundsLikeTheWarmUp();
void MegaDriveVoiceEndsInSilence();
void MegaDriveSongSoundsFromTheRequestOn();
void MegaDriveCartridgeFileLayouts();
