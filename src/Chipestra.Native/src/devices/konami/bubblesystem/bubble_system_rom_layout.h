// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "resource_resolver.h"

#include <cstdint>
#include <vector>

// The ROM images the real Bubble System's sound subsystem needs. Same shape as
// GradiusSoundRoms (the sound hardware and address map are identical to GX400), but the
// audiocpu ROM ("400b03.8g") is a different program from GX400's ("400-e03.5l"). Kept as
// its own struct so a future divergence between the boards does not cross-contaminate.
struct BubbleSystemSoundRoms {
    std::vector<std::uint8_t> Z80Program;               // "soundcpu" logical resource
    std::vector<std::uint8_t> K005289WaveformProm;       // "wave0"+"wave1" concatenated
};

// Same "resource logical name -> board socket" pattern as AssembleGradiusSoundRoms, kept
// separate so a socket-layout change on one board does not touch the other.
BubbleSystemSoundRoms AssembleBubbleSystemSoundRoms(const MachineResources& resources);

// The main 68000's own ROM images:
//   "bootcode"     - the 480-byte boot program (MAME's "maincpu" region,
//                    ROM_LOAD16_WORD: already in the correct byte order)
//   "bubblememory" - the cartridge image (MAME's "bubblememory" region,
//                    ROM_LOAD16_WORD_SWAP: each 16-bit word is byte-swapped relative to
//                    the raw file; AssembleBubbleSystemMainCpuRoms undoes that swap so
//                    BubbleSystemMainMemoryMap's K005297 page-copy logic reads it
//                    directly)
struct BubbleSystemMainCpuRoms {
    std::vector<std::uint8_t> BootCode;       // up to 0x1E0 bytes
    std::vector<std::uint8_t> BubbleMemory;   // cartridge-dependent size (0x48360 for Gradius)
};

// Throws std::runtime_error naming the missing logical name(s) when `resources.Count() > 0`
// but "bootcode" and/or "bubblememory" are absent (silently zero-filling would mask a
// game XML that forgot a required resource). `resources.Count() == 0` (registered but not
// yet playable) is not an error and returns empty vectors.
BubbleSystemMainCpuRoms AssembleBubbleSystemMainCpuRoms(const MachineResources& resources);
