// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "devices/taito/ninjaw/ninja_warriors_rom_layout.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace {

constexpr std::size_t kAdpcmARegionSize = 0x180000;  // 3 x 512KB
constexpr std::size_t kAdpcmAChunkSize = 0x80000;    // 512KB per ROM chip

void CopyResource(const MachineResources& resources, const char* logicalName, std::uint8_t* dest,
                  std::size_t destCapacity) {
    const ResolvedResource* resource = resources.Find(logicalName);
    if (resource == nullptr) {
        return;
    }
    const std::size_t count = std::min(resource->Size(), destCapacity);
    std::memcpy(dest, resource->Data(), count);
}

}  // namespace

NinjaWarriorsSoundRoms AssembleNinjaWarriorsSoundRoms(const MachineResources& resources) {
    NinjaWarriorsSoundRoms roms;

    if (const ResolvedResource* soundcpu = resources.Find("soundcpu")) {
        roms.AudioCpuProgram.assign(soundcpu->Data(), soundcpu->Data() + soundcpu->Size());
    }

    // b31-09.18 @0x000000, b31-10.17 @0x080000, b31-11.16 @0x100000.
    roms.AdpcmASamples.assign(kAdpcmARegionSize, 0);
    const std::array<const char*, 3> adpcmAChunks{"adpcma1", "adpcma2", "adpcma3"};
    for (std::size_t chunk = 0; chunk < adpcmAChunks.size(); ++chunk) {
        const std::size_t offset = chunk * kAdpcmAChunkSize;
        CopyResource(resources, adpcmAChunks[chunk], roms.AdpcmASamples.data() + offset,
                     kAdpcmARegionSize - offset);
    }

    if (const ResolvedResource* adpcmb = resources.Find("adpcmb")) {
        roms.AdpcmBSamples.assign(adpcmb->Data(), adpcmb->Data() + adpcmb->Size());
    }

    return roms;
}
