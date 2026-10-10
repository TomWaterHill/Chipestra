// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// Real Gyruss ROM bring-up native tests.
//
// GyrussDefinitionXmlIsValid() always runs (no ROM needed - just validates
// xml/gyruss.xml parses and matches AssembleGyrussSoundRoms's
// own expectations, same pattern as every other board's *DefinitionXmlIsValid).
//
// GyrussViaDefinitionXmlRealRomBringUp() SKIPs (not fails) when
// CHIPESTRA_ROM_ROOT is not set or the gyruss ROM set is not found under it,
// same convention as every other real-ROM bring-up test in this project.

void GyrussDefinitionXmlIsValid();
void GyrussViaDefinitionXmlRealRomBringUp();

// Uses the SAME full public C ABI path the GUI's Managed Core drives
// (chipestra_parse_game_definition -> chipestra_resolve_resources -> chipestra_load_machine ->
// chipestra_start_playback -> chipestra_get_logical_channel_info/chipestra_get_master_state -
// see afterburner2_reference_tests.cpp's
// ChannelMeterAbiDuringRealPlaybackReportsNonZeroPeak) to check that the
// real Mixer/ABI output is audible, not only the Machine-internal shortcut
// test above.
void GyrussChannelMeterAbiDuringRealPlaybackReportsPerChannelPeak();
