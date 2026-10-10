// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// Real Darius ROM bring-up tests. See darius_reference_tests.cpp's top-of-file
// comment for the ROM-handling convention these follow.

void DariusDefinitionXmlIsValid();
void DariusViaDefinitionXmlRealRomBringUp();

// Checks whether the audiocpu busy-polls the comm register (PC060HA mode 4/
// status at 0xB000/0xB001) instead of taking the NMI, as the Metal Black and
// Ninja Warriors drivers do. SKIPs without CHIPESTRA_ROM_ROOT.
void DariusAudiocpuPollingDiagnostic();

// Song-switch regressions: DariusMachine::SendCommand() sends STOP and renders a silent gap before the
// requested command when a song is already running. SKIP without CHIPESTRA_ROM_ROOT.
void DariusSongSwitchDoesNotMixSongs();
void DariusSwitchFromBgmToSeStopsTheBgm();
void DariusSongSwitchThroughUnrenderedStopDoesNotMixSongs();
