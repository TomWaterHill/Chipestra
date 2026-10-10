// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "devices/konami/bubblesystem/bubble_system_rom_layout.h"

#include "devices/sound/k005289/k005289_device_adapter.h"

#include <cstring>
#include <stdexcept>
#include <string>

namespace {

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

BubbleSystemSoundRoms AssembleBubbleSystemSoundRoms(const MachineResources& resources) {
    BubbleSystemSoundRoms roms;

    if (const ResolvedResource* soundcpu = resources.Find("soundcpu")) {
        roms.Z80Program.assign(soundcpu->Data(), soundcpu->Data() + soundcpu->Size());
    }

    roms.K005289WaveformProm.assign(K005289DeviceAdapter::kWaveformPromTotalBytes, 0);
    CopyResourceAt(resources, "wave0", roms.K005289WaveformProm, 0);
    CopyResourceAt(resources, "wave1", roms.K005289WaveformProm, K005289DeviceAdapter::kWaveformPromBytesPerVoice);

    return roms;
}

BubbleSystemMainCpuRoms AssembleBubbleSystemMainCpuRoms(const MachineResources& resources) {
    // Fail loudly if resources were supplied but a required ROM is missing, instead of
    // silently zero-filling.
    if (resources.Count() > 0) {
        static constexpr const char* kRequiredNames[] = {"bootcode", "bubblememory"};
        std::string missing;
        for (const char* name : kRequiredNames) {
            if (resources.Find(name) == nullptr) {
                if (!missing.empty()) missing += ", ";
                missing += name;
            }
        }
        if (!missing.empty()) {
            throw std::runtime_error(
                "AssembleBubbleSystemMainCpuRoms: resources were supplied but the following required main-CPU ROM "
                "resource(s) are missing: " +
                missing +
                " - BubbleSystemMachine cannot boot without the real boot program and cartridge image. "
                "Check that the game's definition XML declares these <rom> resources.");
        }
    }

    BubbleSystemMainCpuRoms roms;

    if (const ResolvedResource* bootcode = resources.Find("bootcode")) {
        roms.BootCode.assign(bootcode->Data(), bootcode->Data() + bootcode->Size());
    }

    // ROM_LOAD16_WORD_SWAP: MAME byte-swaps every 16-bit word of the raw dumped file while
    // loading the "bubblememory" region. The resolved resource is the raw file content, so
    // the swap is undone once here, to match the byte order BubbleSystemMainMemoryMap's
    // K005297 page-copy logic expects.
    if (const ResolvedResource* bubblememory = resources.Find("bubblememory")) {
        const std::uint8_t* src = bubblememory->Data();
        const std::size_t size = bubblememory->Size();
        roms.BubbleMemory.assign(size, 0);
        const std::size_t pairCount = size / 2;
        for (std::size_t i = 0; i < pairCount; ++i) {
            roms.BubbleMemory[2 * i] = src[2 * i + 1];
            roms.BubbleMemory[2 * i + 1] = src[2 * i];
        }
        if ((size & 1) != 0) {
            // Odd trailing byte: keep the function total rather than dropping data.
            roms.BubbleMemory[size - 1] = src[size - 1];
        }
    }

    return roms;
}
