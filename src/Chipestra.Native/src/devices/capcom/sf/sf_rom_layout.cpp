// license:BSD-3-Clause
// copyright-holders:Olivier Galibert
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Portions derived from MAME (src/mame/capcom/sf.cpp); see THIRD-PARTY-NOTICES.md.
#include "devices/capcom/sf/sf_rom_layout.h"

#include <algorithm>

SfSoundRoms AssembleSfSoundRoms(const MachineResources& resources) {
    SfSoundRoms roms;

    // Audio CPU program: copy "soundcpu" directly
    if (const ResolvedResource* soundcpu = resources.Find("soundcpu")) {
        roms.AudioCpuProgram.assign(soundcpu->Data(), soundcpu->Data() + soundcpu->Size());
    }

    // Samples program: assemble from "samples0" and "samples1"
    // Only resize if at least one resource exists
    bool has_samples0 = (resources.Find("samples0") != nullptr);
    bool has_samples1 = (resources.Find("samples1") != nullptr);

    if (has_samples0 || has_samples1) {
        roms.SamplesProgram.resize(0x40000);  // 256KB total

        if (const ResolvedResource* samples0 = resources.Find("samples0")) {
            std::size_t copy_size = std::min(samples0->Size(), std::size_t(0x20000));
            std::copy(samples0->Data(), samples0->Data() + copy_size, roms.SamplesProgram.data() + 0x00000);
        }

        if (const ResolvedResource* samples1 = resources.Find("samples1")) {
            std::size_t copy_size = std::min(samples1->Size(), std::size_t(0x20000));
            std::copy(samples1->Data(), samples1->Data() + copy_size, roms.SamplesProgram.data() + 0x20000);
        }
    }

    return roms;
}
