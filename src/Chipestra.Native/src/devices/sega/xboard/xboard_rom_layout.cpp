// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "devices/sega/xboard/xboard_rom_layout.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace {

constexpr std::size_t kSegaPcmRegionSize = 0x80000;   // 512KB (BANK_512)
constexpr std::size_t kSegaPcmSocketSize = 0x20000;   // one PCM ROM socket

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

XBoardSoundRoms AssembleXBoardSoundRoms(const MachineResources& resources) {
    XBoardSoundRoms roms;

    if (const ResolvedResource* soundcpu = resources.Find("soundcpu")) {
        roms.Z80Program.assign(soundcpu->Data(), soundcpu->Data() + soundcpu->Size());
    }

    roms.SegaPcmSamples.assign(kSegaPcmRegionSize, 0xFF);  // ROMREGION_ERASEFF
    const std::array<const char*, 3> pcmSockets{"pcm1", "pcm2", "pcm3"};
    for (std::size_t socket = 0; socket < pcmSockets.size(); ++socket) {
        const std::size_t offset = socket * kSegaPcmSocketSize;
        CopyResource(resources, pcmSockets[socket], roms.SegaPcmSamples.data() + offset,
                     kSegaPcmRegionSize - offset);
    }

    return roms;
}
