// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "dragon_spirit_command_interface.h"

DragonSpiritCommandInterface::DragonSpiritCommandInterface(DragonSpiritMemoryMap& memory_map)
    : memory_map_(memory_map) {}

void DragonSpiritCommandInterface::Send(std::uint8_t command) {
    memory_map_.Write8(DragonSpiritMemoryMap::kTriRamStart + kCommandOffset, command);
    memory_map_.Write8(DragonSpiritMemoryMap::kTriRamStart + kReadyFlagOffset, kReadyFlagValue);
}

void DragonSpiritCommandInterface::Reset() {
    memory_map_.Write8(DragonSpiritMemoryMap::kTriRamStart + kReadyFlagOffset, kIdleFlagValue);
}
