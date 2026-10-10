// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "devices/taito/ninjaw/ninja_warriors_rom_layout.h"
#include "resource_resolver.h"

// Maps a resolved MachineResources set onto Metal Black's sound ROM sockets. NinjaWarriorsSoundRoms is
// reused as the result type because Metal Black's sound board (Z80 @ 4MHz, YM2610 @ 8MHz, TC0140SYT) is
// wired identically to The Ninja Warriors and is played through an unmodified NinjaWarriorsMachine.
//
// Unlike Ninja Warriors, the ADPCM-A sample ROM is a single 1MB file, so this is a plain single-resource
// copy rather than a multi-socket concatenation.
NinjaWarriorsSoundRoms AssembleMetalBlackSoundRoms(const MachineResources& resources);
