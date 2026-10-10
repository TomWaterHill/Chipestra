// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// Real Gradius (Bubble System)/GX400 ROM bring-up. Declared here, defined in
// gradius_reference_tests.cpp, called from native_lifecycle_tests.cpp's
// main() (matching outrun_reference_tests.h/afterburner2_reference_tests.h's
// convention).

void GradiusAy8910AndK005289RealRomBringUp();

// xml/gradius.xml validation (matches OutRunDefinitionXmlIsValid's pattern).
// GradiusAy8910AndK005289RealRomBringUp above builds its own resource list
// by hand rather than going through the XML.
void GradiusDefinitionXmlIsValid();

// Real-ROM bring-up built from gradius.xml's OWN declared <resources> (via
// ResourceResolver + DefaultMachineRegistry().Create(), the same two steps
// chipestra_load_machine performs), unlike the real-ROM test above which
// hand-builds its own RequiredResource list. Regression guard against the
// XML's list missing the four main-CPU ROM resources GradiusMachine needs.
void GradiusViaDefinitionXmlRealRomBringUp();
