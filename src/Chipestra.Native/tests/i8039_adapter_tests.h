// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// I8039Core + MAME-derived i8039 backend native tests. Declared here
// so native_lifecycle_tests.cpp's main() can call them - same pattern as
// z80_adapter_tests.h/m68000_adapter_tests.h.

void I8039BasicMovAndCycleCounting();
void I8039AddCarryAuxCarryFlags();
void I8039JumpWithinPage();
void I8039DjnzLoop();
void I8039CallAndReturnRoundTrip();
void I8039Port1OutputAndReadback();
void I8039ExternalBusMovx();
void I8039TimerOverflowServicesInterrupt();
void I8039ExternalIrqServicedAndRetrRestoresPsw();
void I8039MemoryBankSelectAffectsJump();
