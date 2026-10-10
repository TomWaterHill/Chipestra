// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "resource_resolver.h"

#include <cstdint>
#include <vector>

// The two Z80 ROM images Darius's sound subsystem needs, assembled from
// individually-resolved resources.
struct DariusSoundRoms {
    std::vector<std::uint8_t> AudioCpuProgram;  // "soundcpu" logical resource (real board: 64KB, "audiocpu" region)
    std::vector<std::uint8_t> AdpcmProgram;     // "adpcm" logical resource (real board: 64KB, "adpcm" region)
};

// Maps a resolved MachineResources set onto Darius's two sound Z80 ROM
// images. A missing resource leaves its field empty; the memory maps
// zero-pad an empty/short image.
DariusSoundRoms AssembleDariusSoundRoms(const MachineResources& resources);
