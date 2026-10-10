// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "resource_resolver.h"

#include <cstdint>
#include <vector>

// The two ROM images the Out Run sound subsystem needs, assembled from
// individually-resolved resources.
// individually-resolved resources.
struct OutRunSoundRoms {
    std::vector<std::uint8_t> Z80Program;      // "soundcpu" region (only 0x0000-0xEFFF is CPU-mapped)
    std::vector<std::uint8_t> SegaPcmSamples;  // "pcm" region: 0x80000 bytes, ERASEFF, six 0x8000 sockets (each mirrored)
};

// Maps a resolved MachineResources set onto the sound board's fixed socket
// layout:
//   "soundcpu"      -> Z80Program (as-is; OutRunMemoryMap maps only 0xEFFF of it)
//   "pcm1".."pcm6"  -> SegaPcmSamples, each socket's 0x8000 bytes copied at
//                      offset N*0x10000 and mirrored at offset N*0x10000+0x8000
//                      (the board's ROM_LOAD followed by ROM_RELOAD), in a 0x80000
//                      buffer pre-filled with 0xFF (ERASEFF)
//
// This mapping is Machine-side board configuration, not game XML. A resource
// absent from `resources` leaves its socket(s) empty.
OutRunSoundRoms AssembleOutRunSoundRoms(const MachineResources& resources);
