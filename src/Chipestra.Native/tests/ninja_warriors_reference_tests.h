// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// Real Ninja Warriors ROM bring-up native tests.
//
// NinjaWarriorsDefinitionXmlIsValid() always runs (no ROM needed): it checks that xml/ninjaw.xml
// parses and matches AssembleNinjaWarriorsSoundRoms's expectations.
// NinjaWarriorsViaDefinitionXmlRealRomBringUp() SKIPs (not fails) when
// CHIPESTRA_ROM_ROOT is not set, like every other real-ROM bring-up test
// (DariusViaDefinitionXmlRealRomBringUp, GradiusViaDefinitionXmlRealRomBringUp, ...).

void NinjaWarriorsDefinitionXmlIsValid();
void NinjaWarriorsViaDefinitionXmlRealRomBringUp();

// Real-ROM regression: no ADPCM-A voice fires by itself after Initialize() (needs CHIPESTRA_ROM_ROOT, else SKIPs).
void NinjaWarriorsNoStrayVoiceAfterBoot();

// Checks whether the audiocpu busy-polls the TC0140SYT comm register (mode 4/status) via a tight
// loop at a fixed PC instead of using the NMI (as the Metal Black driver does; the machine and
// sound_map are identical). Method: opcode-fetch PC-frequency histogram before/after
// SendCommand(). SKIPs without CHIPESTRA_ROM_ROOT.
void NinjaWarriorsAudiocpuPollingDiagnostic();

// Register-level check (like MetalBlackYm2610RegisterTraceDiagnostic): command dispatch is
// reached and some registers are written, but are the ones that actually trigger a note/sample
// (reg 0x28 FM key on/off, reg 0x00 on port B ADPCM-A start/stop key) ever written?
void NinjaWarriorsYm2610RegisterTraceDiagnostic();

// Full sweep of every command ninjaw.xml declares, using the same real key-on/ADPCM-A-start
// register signal as Metal Black's MetalBlackKeyOnSweepDiagnostic: does any declared track
// actually trigger real notes?
void NinjaWarriorsKeyOnSweepDiagnostic();
