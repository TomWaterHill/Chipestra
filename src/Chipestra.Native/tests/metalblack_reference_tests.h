// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// Real Metal Black ROM bring-up native tests.
//
// MetalBlackDefinitionXmlIsValid() always runs (no ROM needed): it checks that xml/metalblk.xml
// parses and matches AssembleMetalBlackSoundRoms's expectations.
//
// MetalBlackViaDefinitionXmlRealRomBringUp() SKIPs (not fails) when
// CHIPESTRA_ROM_ROOT is not set or the metalb ROM set is not found under it. It checks the
// command 0x0C ("07:Born to be free(Round 1)") by title lookup from metalblk.xml, and a
// "no command sent yet" baseline control (see the .cpp for why the baseline matters).

void MetalBlackDefinitionXmlIsValid();
void MetalBlackViaDefinitionXmlRealRomBringUp();

// Real-ROM regression: switching songs directly does not leave the previous song playing (SKIPs without ROMs).
void MetalBlackSongSwitchDoesNotMixSongs();

// Checks whether the audiocpu busy-polls the TC0140SYT comm/status register instead of using the
// NMI (so nmiServiceCount is the wrong signal to check). Cheap, read-mostly: an opcode-fetch
// PC-frequency histogram over the real ROM's idle loop, plus a write-observer on the TC0140SYT
// mode-select address 0xE200. SKIPs without CHIPESTRA_ROM_ROOT.
void MetalBlackAudiocpuPollingDiagnostic();

// Logs actual YM2610 (register, data) pairs around a STOP (0x00) command and a target command,
// to see whether FM key-on (reg 0x28) or ADPCM-A start/stop control registers are touched,
// which tells "command genuinely retriggers new notes" from "old sample keeps looping" more
// directly than a channel meter can. (A stuck ADPCM sample is not proof that the requested BGM plays.)
void MetalBlackYm2610RegisterTraceDiagnostic();

// Full sweep of every command metalblk.xml declares, using the real key-on/ADPCM-A-start
// register signal (not peak/IsTimeVarying()): does any declared track actually trigger real
// notes, per command.
void MetalBlackKeyOnSweepDiagnostic();
