// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "dragon_spirit_memory_map.h"

#include <cstdint>

// Turns a raw command byte into the TRIRAM write the (un-emulated) main CPU performs: command byte at
// offset 0x100, then 0x40 at offset 0x101. The audio CPU polls offset 0x101 each frame and, on a change,
// reads offset 0x100 and dispatches. No interrupt line is involved, so Send() is a plain synchronous write.
class DragonSpiritCommandInterface {
public:
    static constexpr std::uint32_t kCommandOffset = 0x100;
    static constexpr std::uint32_t kReadyFlagOffset = 0x101;
    static constexpr std::uint8_t kReadyFlagValue = 0x40;
    static constexpr std::uint8_t kIdleFlagValue = 0x00;

    // `memory_map` must outlive this object (non-owning).
    explicit DragonSpiritCommandInterface(DragonSpiritMemoryMap& memory_map);

    // Writes (command, kReadyFlagValue) to TRIRAM 0x100/0x101.
    void Send(std::uint8_t command);

    // Clears the ready flag so a stale, never-consumed command from before a Reset() is not mistaken for a new one.
    void Reset();

private:
    DragonSpiritMemoryMap& memory_map_;
};
