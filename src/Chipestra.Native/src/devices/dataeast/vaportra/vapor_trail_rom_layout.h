// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "resource_resolver.h"

#include <cstdint>
#include <vector>

// The three ROM images VaporTrailMachine needs; the main 68000 program is never loaded (sound-only scope).
// "soundcpu"/"oki1"/"oki2" are Chipestra's logical resource names (chips fj04/fj06/fj05).
struct VaporTrailSoundRoms {
    // The H6280's 64KB program ("soundcpu"), copied 1:1 with no interleaving or banking.
    std::vector<std::uint8_t> SoundCpuProgram;
    // OKIM6295 #1 sample ROM ("oki1"), copied at its resolved size (out-of-range reads return 0).
    std::vector<std::uint8_t> Oki1Program;
    // OKIM6295 #2 sample ROM ("oki2").
    std::vector<std::uint8_t> Oki2Program;
};

// AssembleVaporTrailSoundRoms - maps resolved resources onto the fixed socket layout. An entirely empty
// `resources` yields a registered-but-not-playable set (program zero-filled to 0x10000, OKI ROMs empty).
// Once any resource is supplied, all of "soundcpu"/"oki1"/"oki2" are required: a missing one throws
// std::runtime_error naming it instead of silently zero-filling.
VaporTrailSoundRoms AssembleVaporTrailSoundRoms(const MachineResources& resources);
