// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// OutRunMachine native tests. Declared here so
// native_lifecycle_tests.cpp's main() can call them - see that file's
// top-of-file comment for why this project uses one shared test
// executable with a single main() instead of a full test framework.

void OutRunMachineBootSequenceReadsAndWrites();
void OutRunMachineCommandInjectionTriggersNmi();
void OutRunMachineRepeatedCommandsReArmNmi();
void OutRunMachineRegisteredInDefaultCatalog();
