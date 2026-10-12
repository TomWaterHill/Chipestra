// license:BSD-3-Clause
// copyright-holders:Olivier Galibert
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Portions derived from MAME (src/mame/capcom/sf.cpp); see THIRD-PARTY-NOTICES.md.
#pragma once

#include "resource_resolver.h"

#include <cstdint>
#include <vector>

// The two Z80 ROM images Street Fighter's sound subsystem needs, assembled from
// individually-resolved resources: "soundcpu" is the music Z80's program; "samples0" and "samples1" are
// the two 128KB halves of the sample Z80's 256KB image.
struct SfSoundRoms {
    std::vector<std::uint8_t> AudioCpuProgram;  // "soundcpu" logical resource (real board: 32KB, "audiocpu" region)
    std::vector<std::uint8_t> SamplesProgram;   // "samples0" and "samples1" logical resources (real board: 256KB total)
};

// Maps a resolved MachineResources set onto Street Fighter's two sound Z80 ROM
// images. A missing resource leaves its field empty or zero-padded as appropriate;
// the memory maps zero-pad an empty/short image.
SfSoundRoms AssembleSfSoundRoms(const MachineResources& resources);
