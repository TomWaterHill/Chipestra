// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "resource_resolver.h"

#include <cstdint>
#include <vector>

// The ROM images the Gradius (GX400/Bubble System) sound subsystem needs, assembled from
// individually resolved resources.
struct GradiusSoundRoms {
    std::vector<std::uint8_t> Z80Program;  // "soundcpu" logical resource (real board: 8KB, 0x0000-0x1FFF)

    // K005289's combined 512-byte waveform PROM (wave0 then wave1). Zero-filled if
    // either resource did not resolve.
    std::vector<std::uint8_t> K005289WaveformProm;
};

// Maps a resolved MachineResources set onto the sound board's fixed socket layout
// (resource logical name -> board socket). "soundcpu"/"wave0"/"wave1" are the logical
// names used in the game XML (MAME calls the regions "audiocpu"/"k005289"). A missing
// resource leaves its field empty/zero-filled; the caller checks resolution first.
GradiusSoundRoms AssembleGradiusSoundRoms(const MachineResources& resources);

// The 68000 main CPU's ROM images: "maincpu" (64KB at 0x000000) and "maindata" (256KB
// at 0x080000). The sound Z80 does not need them, but the main CPU maps them as ROM.
struct GradiusMainCpuRoms {
    std::vector<std::uint8_t> MainProgram;  // 0x10000 bytes
    std::vector<std::uint8_t> MainData;     // 0x40000 bytes
};

// De-interleaves the four physical ROM chips (ROM_LOAD16_BYTE pairs) into the 16-bit-wide
// byte streams the 68000 reads: "maincpu_hi"/"maincpu_lo" -> even/odd bytes of
// MainProgram; "maindata_hi"/"maindata_lo" -> even/odd bytes of MainData (big-endian:
// the "_hi" ROM supplies the even address).
//
// If any resources were supplied but one of these four is missing, throws
// std::runtime_error naming it: silently zero-filling would leave the main CPU spinning
// over an empty ROM and never completing the shared-RAM handshake the sound Z80 waits
// for. An entirely empty MachineResources (a registered but not yet playable Machine)
// is not an error and returns zero-filled ROMs.
GradiusMainCpuRoms AssembleGradiusMainCpuRoms(const MachineResources& resources);
