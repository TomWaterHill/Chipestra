// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "vapor_trail_rom_layout.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

VaporTrailSoundRoms AssembleVaporTrailSoundRoms(const MachineResources& resources) {
    if (resources.Count() > 0) {
        static constexpr const char* kRequiredNames[] = {"soundcpu", "oki1", "oki2"};
        std::string missing;
        for (const char* name : kRequiredNames) {
            if (resources.Find(name) == nullptr) {
                if (!missing.empty()) missing += ", ";
                missing += name;
            }
        }
        if (!missing.empty()) {
            throw std::runtime_error(
                "AssembleVaporTrailSoundRoms: resources were supplied but the following required sound-ROM "
                "resource(s) are missing: " +
                missing +
                " - VaporTrailMachine cannot initialize its H6280 sound driver or OKIM6295 sample playback "
                "without these resources. Check that the game's definition XML declares these <rom> "
                "resources.");
        }
    }

    VaporTrailSoundRoms roms;
    roms.SoundCpuProgram.assign(0x10000, 0);

    if (const ResolvedResource* soundcpu = resources.Find("soundcpu")) {
        const std::size_t count = std::min(soundcpu->Size(), roms.SoundCpuProgram.size());
        std::memcpy(roms.SoundCpuProgram.data(), soundcpu->Data(), count);
    }
    if (const ResolvedResource* oki1 = resources.Find("oki1")) {
        roms.Oki1Program.assign(oki1->Data(), oki1->Data() + oki1->Size());
    }
    if (const ResolvedResource* oki2 = resources.Find("oki2")) {
        roms.Oki2Program.assign(oki2->Data(), oki2->Data() + oki2->Size());
    }

    return roms;
}
