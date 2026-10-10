// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "machine_registry.h"

// Returns the process-wide MachineRegistry, populated (on first call) with
// every MachineDefinition this Native build knows about.
//
// The catalog lists Machine *types* (board/console implementations, each
// with its C++ factory). A new board is added by registering its
// MachineDefinition in default_machine_catalog.cpp. Game XML files (xml/*.xml)
// do not define Machines: each one only names a registered machine/variant
// and declares that game's ROM resources and tracks.
//
// The registry is read-mostly and immutable once populated; it holds no
// per-instance playback state, so sharing it across instances does not
// create shared mutable state.
MachineRegistry& DefaultMachineRegistry();
