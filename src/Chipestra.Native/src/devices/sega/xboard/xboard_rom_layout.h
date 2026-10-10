// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "resource_resolver.h"

#include <cstdint>
#include <vector>

// The two ROM images the X-Board sound subsystem needs, assembled from
// individually-resolved resources.
struct XBoardSoundRoms {
    std::vector<std::uint8_t> Z80Program;      // "soundcpu" region (only 0x0000-0xEFFF is CPU-mapped)
    std::vector<std::uint8_t> SegaPcmSamples;  // "pcm" region: 0x80000 bytes, ERASEFF, three 0x20000 sockets
};

// Maps a resolved MachineResources set onto the sound board's fixed socket
// layout:
//   "soundcpu"           -> Z80Program (as-is; XBoardMemoryMap maps only 0xEFFF of it)
//   "pcm1"/"pcm2"/"pcm3" -> SegaPcmSamples at 0x00000 / 0x20000 / 0x40000
//                           in a 0x80000 buffer pre-filled with 0xFF (ERASEFF)
//
// This mapping is Machine-side board configuration, not game XML. A resource
// absent from `resources` leaves its socket empty.
XBoardSoundRoms AssembleXBoardSoundRoms(const MachineResources& resources);
