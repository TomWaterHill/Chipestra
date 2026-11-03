// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// The register a request number is passed in (<driver request_register>) and the separate routine for effects
// (<driver sfx_call>) of the NES Machine, called from native_lifecycle_tests.cpp's main(). The synthetic tests use a
// hand-written program; the real-ROM test (CHIPESTRA_ROM_ROOT with the game, and CHIPESTRA_REFERENCE_DIR) plays every
// song and effect of the sound driver and compares the APU writes with a recording of the real game.

void NesRequestRegisterSettingsParseAndReject();
void NesFrameCodeIsUnchangedWithoutTheNewSettings();
void NesMachinePassesTheRequestInTheChosenRegister();
void SilverSurferViaDefinitionXmlRealRomMatchesTheRealGame();
