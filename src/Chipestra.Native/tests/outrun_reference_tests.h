// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// Out Run reference tests: xml/outrun.xml validity
// (always runs), and a real-ROM track-switch regression test (SKIPs when
// CHIPESTRA_ROM_ROOT/the ROMs aren't available, matching AfterBurner2ReferenceBringUp's
// own SKIP-when-absent convention - afterburner2_reference_tests.h/.cpp).
// Declared here so native_lifecycle_tests.cpp's main() can call them.

void OutRunDefinitionXmlIsValid();
void OutRunTrackSwitchReferenceBringUp();
