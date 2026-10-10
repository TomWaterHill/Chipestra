// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "devices/konami/gyruss/gyruss_rom_layout.h"

#include "devices/konami/gyruss/gyruss_sound_memory_map.h"

#include <algorithm>
#include <cstring>

namespace {

// Copies `resource` (if resolved) into `dest` at `destOffset`, clamped to
// `dest`'s bounds.
void CopyResourceAt(const MachineResources& resources, const char* logicalName, std::vector<std::uint8_t>& dest,
                     std::size_t destOffset) {
    const ResolvedResource* resource = resources.Find(logicalName);
    if (resource == nullptr || destOffset >= dest.size()) {
        return;
    }
    const std::size_t count = std::min(resource->Size(), dest.size() - destOffset);
    std::memcpy(dest.data() + destOffset, resource->Data(), count);
}

}  // namespace

GyrussSoundRoms AssembleGyrussSoundRoms(const MachineResources& resources) {
    GyrussSoundRoms roms;

    // gyrussk.1a -> 0x0000, gyrussk.2a -> 0x2000: sequential concatenation.
    // Sized to the full 24KB ROM window; 0x4000-0x5FFF stays zero.
    roms.Z80Program.assign(GyrussSoundMemoryMap::kRomSize, 0);
    CopyResourceAt(resources, "soundcpu_lo", roms.Z80Program, 0x0000);
    CopyResourceAt(resources, "soundcpu_hi", roms.Z80Program, 0x2000);

    if (const ResolvedResource* audio2 = resources.Find("audio2")) {
        roms.I8039Program.assign(audio2->Data(), audio2->Data() + audio2->Size());
    }

    return roms;
}
