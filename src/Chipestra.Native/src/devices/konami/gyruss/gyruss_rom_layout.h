// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "resource_resolver.h"

#include <cstdint>
#include <vector>

// The two sound ROM images Gyruss needs (the sound-only player never loads
// maincpu/sub/sprites/tiles/proms).
struct GyrussSoundRoms {
    // The sound Z80's 16KB program ships as two 8KB chips at sequential
    // addresses (gyrussk.1a at 0x0000-0x1FFF, gyrussk.2a at 0x2000-0x3FFF),
    // assembled into the flat 24KB image GyrussSoundMemoryMap::LoadRom expects
    // (0x4000-0x5FFF stays zero-filled, unpopulated on the board).
    std::vector<std::uint8_t> Z80Program;
    std::vector<std::uint8_t> I8039Program;  // "audio2" resource (gyrussk.3a, 4KB, 0x000-0xFFF)
};

// Maps a resolved MachineResources set onto the board's fixed socket layout.
//
// Logical names: "soundcpu_lo" and "soundcpu_hi" (the Z80's two 8KB chips) and
// "audio2" (MAME's ROM_REGION tag for the i8039 program). MAME's "sub" tag is a
// different chip (the encrypted sprite CPU) and is not used here.
// A resource absent from `resources` simply leaves the corresponding field
// empty/zero-filled (the caller resolves + checks Ok before calling this).
GyrussSoundRoms AssembleGyrussSoundRoms(const MachineResources& resources);
