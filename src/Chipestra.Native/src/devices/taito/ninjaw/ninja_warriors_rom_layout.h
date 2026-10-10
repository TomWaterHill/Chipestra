// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "resource_resolver.h"

#include <cstdint>
#include <vector>

// The sound-relevant ROM images Ninja Warriors's sound subsystem needs,
// assembled from individually-resolved resources.
struct NinjaWarriorsSoundRoms {
    std::vector<std::uint8_t> AudioCpuProgram;  // "soundcpu" logical resource (real board: 128KB, "audiocpu" region)
    std::vector<std::uint8_t> AdpcmASamples;    // 3 logical resources concatenated (real board: 0x180000, "ymsnd:adpcma")
    std::vector<std::uint8_t> AdpcmBSamples;    // "adpcmb" logical resource (real board: 0x80000, "ymsnd:adpcmb")
};

// Maps a resolved MachineResources set onto Ninja Warriors's sound ROM sockets.
// "soundcpu"/"adpcma1"/"adpcma2"/"adpcma3"/"adpcmb" are Chipestra's logical
// names; the three ADPCM-A files are assembled at board-fixed offsets. A missing
// resource leaves its bytes zero; the consumers tolerate short/empty vectors.
NinjaWarriorsSoundRoms AssembleNinjaWarriorsSoundRoms(const MachineResources& resources);
