// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// DariusCommandInterface native tests, called from native_lifecycle_tests.cpp's main().

void DariusCommandInterfaceSlaveNmiGatedByEnableFlag();
void DariusCommandInterfaceSendDeliversByteViaTwoNibbles();
void DariusCommandInterfaceSlaveReadClearsStatusAndNmi();
