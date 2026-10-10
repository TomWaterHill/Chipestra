// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// Real Vapor Trail ROM bring-up native tests, called from native_lifecycle_tests.cpp's main().
//
// ROM handling: this file never hard-codes a ROM Root path, and only prints derived metadata (peak
// levels, timing), never ROM-derived sample data. The crc32/size/filename constants in
// xml/vaportra.xml are MAME's public ROM-catalog metadata (ROM_START(vaportra)), not ROM byte
// content.

void VaporTrailDefinitionXmlIsValid();
void VaporTrailViaDefinitionXmlRealRomBringUp();
