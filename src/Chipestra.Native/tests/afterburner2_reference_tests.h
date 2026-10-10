// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// After Burner II real-ROM bring-up native tests.
//
// AfterBurner2DefinitionXmlIsValid always runs: it validates the committed
// xml/afterburn2.xml against the parser and the catalog.
//
// AfterBurner2ReferenceBringUp is gated on the CHIPESTRA_ROM_ROOT environment
// variable: with no ROM Root set it prints SKIP and passes, so the
// build gate stays green on machines without the ROMs. When a ROM
// Root is set it resolves the After Burner II sound ROMs, brings up
// XBoardMachine on the real ROM, probes the sound-command space for
// continuous audio, does a reference-level waveform check and a
// determinism check, and prints findings on IM0 usage, YM IRQ latency and
// L/R handling.
//
// No ROM bytes, ROM Root paths, or driver command values are hard-coded
// here or emitted anywhere persistent - only comparison metadata is printed.

void AfterBurner2DefinitionXmlIsValid();
void AfterBurner2ReferenceBringUp();

// Real-ROM, CHIPESTRA_ROM_ROOT-gated like AfterBurner2ReferenceBringUp above:
// chipestra_get_logical_channel_info, exercised through the full public C
// ABI (chipestra_create_instance -> chipestra_load_machine -> chipestra_start_playback),
// not the native-shortcut construction the other functions in this file use.
// It checks that the per-channel Peak/RMS readback works while the
// PlaybackPump thread is rendering a real command.
void ChannelMeterAbiDuringRealPlaybackReportsNonZeroPeak();
