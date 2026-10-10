// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "devices/taito/darius/darius_rom_layout.h"

DariusSoundRoms AssembleDariusSoundRoms(const MachineResources& resources) {
    DariusSoundRoms roms;

    if (const ResolvedResource* soundcpu = resources.Find("soundcpu")) {
        roms.AudioCpuProgram.assign(soundcpu->Data(), soundcpu->Data() + soundcpu->Size());
    }
    if (const ResolvedResource* adpcm = resources.Find("adpcm")) {
        roms.AdpcmProgram.assign(adpcm->Data(), adpcm->Data() + adpcm->Size());
    }

    return roms;
}
