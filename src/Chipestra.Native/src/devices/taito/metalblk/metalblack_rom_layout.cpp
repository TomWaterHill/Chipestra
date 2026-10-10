// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "devices/taito/metalblk/metalblack_rom_layout.h"

NinjaWarriorsSoundRoms AssembleMetalBlackSoundRoms(const MachineResources& resources) {
    NinjaWarriorsSoundRoms roms;

    if (const ResolvedResource* soundcpu = resources.Find("soundcpu")) {
        roms.AudioCpuProgram.assign(soundcpu->Data(), soundcpu->Data() + soundcpu->Size());
    }
    if (const ResolvedResource* adpcma = resources.Find("adpcma")) {
        roms.AdpcmASamples.assign(adpcma->Data(), adpcma->Data() + adpcma->Size());
    }
    if (const ResolvedResource* adpcmb = resources.Find("adpcmb")) {
        roms.AdpcmBSamples.assign(adpcmb->Data(), adpcmb->Data() + adpcmb->Size());
    }

    return roms;
}
