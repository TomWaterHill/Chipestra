// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "resource_resolver.h"

#include <cstdint>
#include <vector>

// The ROM images DragonSpiritMachine needs. The audio CPU region is two discrete 64KB chips
// ("soundcpu0"/"soundcpu1") concatenated into one 0x20000 region; the MCU program and the external
// "voice" ROM (0xC0000 bytes across 6 chips) that the MCU streams to the DACs are carried here too.
struct DragonSpiritSoundRoms {
    // Full 8-bank (0x20000) audio CPU program ROM: soundcpu0 at 0x00000, soundcpu1 at 0x10000.
    std::vector<std::uint8_t> SoundCpuProgram;

    // The MCU's 4KB internal ROM ("mcu").
    std::vector<std::uint8_t> McuProgram;

    // Full 0xC0000 "voice" external ROM (6 chips x 0x20000 each).
    std::vector<std::uint8_t> VoiceRom;
};

// Maps a resolved MachineResources set onto Dragon Spirit's fixed socket layout. An empty `resources`
// returns zero-filled images of full size (registered but not playable). Once `resources` is non-empty,
// all of "soundcpu0"/"soundcpu1"/"mcu"/"voice0"-"voice4" must be present or std::runtime_error names the
// missing ones.
//
// "voice0" (chip 0) is loaded twice: the board's ROM_LOAD_HS mirrors that 0x10000-byte file across the
// whole 0x20000-byte chip-0 slot, while voice1-voice4 each fill their own 0x20000 chip.
DragonSpiritSoundRoms AssembleDragonSpiritSoundRoms(const MachineResources& resources);
