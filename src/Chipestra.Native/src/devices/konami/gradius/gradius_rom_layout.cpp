// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "devices/konami/gradius/gradius_rom_layout.h"

#include "devices/sound/k005289/k005289_device_adapter.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

namespace {

// Copies `resource` (if resolved) into `dest` at `destOffset`, clamped to `dest`'s bounds.
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

GradiusSoundRoms AssembleGradiusSoundRoms(const MachineResources& resources) {
    GradiusSoundRoms roms;

    if (const ResolvedResource* soundcpu = resources.Find("soundcpu")) {
        roms.Z80Program.assign(soundcpu->Data(), soundcpu->Data() + soundcpu->Size());
    }

    // wave0 -> first 256-byte half (voice 0), wave1 -> second (voice 1), as in MAME's
    // combined "k005289" region.
    roms.K005289WaveformProm.assign(K005289DeviceAdapter::kWaveformPromTotalBytes, 0);
    CopyResourceAt(resources, "wave0", roms.K005289WaveformProm, 0);
    CopyResourceAt(resources, "wave1", roms.K005289WaveformProm, K005289DeviceAdapter::kWaveformPromBytesPerVoice);

    return roms;
}

namespace {

// De-interleaves a "_hi"/"_lo" pair into `dest`: dest[2i] = hi[i] (68000 big-endian high
// byte), dest[2i+1] = lo[i]. A missing resource contributes zero bytes.
void InterleaveHiLo(const MachineResources& resources, const char* hiName, const char* loName,
                     std::vector<std::uint8_t>& dest) {
    const ResolvedResource* hi = resources.Find(hiName);
    const ResolvedResource* lo = resources.Find(loName);
    const std::size_t halfSize = dest.size() / 2;
    for (std::size_t i = 0; i < halfSize; ++i) {
        dest[2 * i] = (hi != nullptr && i < hi->Size()) ? hi->Data()[i] : 0;
        dest[2 * i + 1] = (lo != nullptr && i < lo->Size()) ? lo->Data()[i] : 0;
    }
}

}  // namespace

GradiusMainCpuRoms AssembleGradiusMainCpuRoms(const MachineResources& resources) {
    // Fail loudly if resources were supplied but a required main-CPU ROM is missing
    // (see the header comment); an entirely empty set still returns zero-filled ROMs.
    if (resources.Count() > 0) {
        static constexpr const char* kRequiredNames[] = {"maincpu_hi", "maincpu_lo", "maindata_hi", "maindata_lo"};
        std::string missing;
        for (const char* name : kRequiredNames) {
            if (resources.Find(name) == nullptr) {
                if (!missing.empty()) missing += ", ";
                missing += name;
            }
        }
        if (!missing.empty()) {
            throw std::runtime_error(
                "AssembleGradiusMainCpuRoms: resources were supplied but the following required main-CPU ROM "
                "resource(s) are missing: " +
                missing +
                " - GradiusMachine cannot complete its shared-RAM boot handshake without the real main CPU "
                "program. Check that the game's definition XML declares these <rom> resources.");
        }
    }

    GradiusMainCpuRoms roms;
    roms.MainProgram.assign(0x10000, 0);
    roms.MainData.assign(0x40000, 0);

    InterleaveHiLo(resources, "maincpu_hi", "maincpu_lo", roms.MainProgram);
    InterleaveHiLo(resources, "maindata_hi", "maindata_lo", roms.MainData);

    return roms;
}
