// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "devices/sega/outrun/outrun_rom_layout.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace {

constexpr std::size_t kSegaPcmRegionSize = 0x80000;  // 512KB (BANK_512)
constexpr std::size_t kSegaPcmSocketPitch = 0x10000;  // one ROM_LOAD + its ROM_RELOAD mirror
constexpr std::size_t kSegaPcmSocketRomSize = 0x8000;  // the physical ROM's own size

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

OutRunSoundRoms AssembleOutRunSoundRoms(const MachineResources& resources) {
    OutRunSoundRoms roms;

    if (const ResolvedResource* soundcpu = resources.Find("soundcpu")) {
        roms.Z80Program.assign(soundcpu->Data(), soundcpu->Data() + soundcpu->Size());
    }

    roms.SegaPcmSamples.assign(kSegaPcmRegionSize, 0xFF);  // ROMREGION_ERASEFF
    const std::array<const char*, 6> pcmSockets{"pcm1", "pcm2", "pcm3", "pcm4", "pcm5", "pcm6"};
    for (std::size_t socket = 0; socket < pcmSockets.size(); ++socket) {
        const std::size_t offset = socket * kSegaPcmSocketPitch;
        // The ROM (offset .. offset+0x8000) and its ROM_RELOAD mirror
        // (offset+0x8000 .. offset+0x10000) are filled from the same resource.
        CopyResource(resources, pcmSockets[socket], roms.SegaPcmSamples.data() + offset,
                     kSegaPcmRegionSize - offset);
        CopyResource(resources, pcmSockets[socket], roms.SegaPcmSamples.data() + offset + kSegaPcmSocketRomSize,
                     kSegaPcmRegionSize - offset - kSegaPcmSocketRomSize);
    }

    return roms;
}
