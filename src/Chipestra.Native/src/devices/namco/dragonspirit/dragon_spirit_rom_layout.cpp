// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "dragon_spirit_rom_layout.h"

#include "dragon_spirit_mcu_memory_map.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

DragonSpiritSoundRoms AssembleDragonSpiritSoundRoms(const MachineResources& resources) {
    if (resources.Count() > 0) {
        static constexpr const char* kRequiredNames[] = {"soundcpu0", "soundcpu1", "mcu",
                                                           "voice0",   "voice1",    "voice2", "voice3", "voice4"};
        std::string missing;
        for (const char* name : kRequiredNames) {
            if (resources.Find(name) == nullptr) {
                if (!missing.empty()) missing += ", ";
                missing += name;
            }
        }
        if (!missing.empty()) {
            throw std::runtime_error(
                "AssembleDragonSpiritSoundRoms: resources were supplied but the following required sound-ROM "
                "resource(s) are missing: " +
                missing +
                " - DragonSpiritMachine cannot initialize its MC6809E/HD63701V0 sound drivers without these "
                "resources. Check that the game's definition XML declares these <rom> resources.");
        }
    }

    DragonSpiritSoundRoms roms;
    roms.SoundCpuProgram.assign(0x20000, 0);
    roms.McuProgram.assign(DragonSpiritMcuMemoryMap::kInternalRomSize, 0);
    roms.VoiceRom.assign(DragonSpiritMcuMemoryMap::kFullVoiceRomSize, 0);

    if (const ResolvedResource* soundcpu0 = resources.Find("soundcpu0")) {
        const std::size_t count = std::min(soundcpu0->Size(), static_cast<std::size_t>(0x10000));
        std::memcpy(roms.SoundCpuProgram.data(), soundcpu0->Data(), count);
    }
    if (const ResolvedResource* soundcpu1 = resources.Find("soundcpu1")) {
        const std::size_t count = std::min(soundcpu1->Size(), static_cast<std::size_t>(0x10000));
        std::memcpy(roms.SoundCpuProgram.data() + 0x10000, soundcpu1->Data(), count);
    }
    if (const ResolvedResource* mcu = resources.Find("mcu")) {
        const std::size_t count = std::min(mcu->Size(), DragonSpiritMcuMemoryMap::kInternalRomSize);
        std::memcpy(roms.McuProgram.data(), mcu->Data(), count);
    }

    // voice0 is mirrored across the whole 0x20000-byte chip-0 slot (ROM_LOAD_HS); voice1-voice4 are one
    // file per chip with no mirroring.
    if (const ResolvedResource* voice0 = resources.Find("voice0")) {
        const std::size_t count = std::min(voice0->Size(), static_cast<std::size_t>(0x10000));
        std::memcpy(roms.VoiceRom.data(), voice0->Data(), count);
        std::memcpy(roms.VoiceRom.data() + 0x10000, voice0->Data(), count);  // ROM_RELOAD(start+length, length)
    }
    static constexpr const char* kVoiceChipNames[] = {"voice1", "voice2", "voice3", "voice4"};
    for (std::size_t i = 0; i < 4; ++i) {
        if (const ResolvedResource* voiceChip = resources.Find(kVoiceChipNames[i])) {
            const std::size_t offset = 0x20000 * (i + 1);
            const std::size_t count = std::min(voiceChip->Size(), static_cast<std::size_t>(0x20000));
            std::memcpy(roms.VoiceRom.data() + offset, voiceChip->Data(), count);
        }
    }
    // Physical chip 5 (0xA0000-0xBFFFF) has no ROM on this board and stays zero-filled.

    return roms;
}
