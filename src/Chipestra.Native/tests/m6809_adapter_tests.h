// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// MC6809E (IM6809Core + MAME-derived backend) native tests. Declared here so
// native_lifecycle_tests.cpp's main() can call them - same pattern as
// h6280_adapter_tests.h/z80_adapter_tests.h/i8039_adapter_tests.h.
//
// CPU core only. Every program below is a hand-written synthetic byte sequence.

void M6809ResetState();
void M6809ImmediateLoadFlagsAndCycles();
void M6809AddSubFlagsOverflowAndCarry();
void M6809DirectAndExtendedAddressing();
void M6809IndexedAddressingVariants();
void M6809ShiftAndRotateOps();
void M6809IncDecNegComClr();
void M6809SixteenBitLoadStoreAndArithmetic();
void M6809BranchesShortAndLong();
void M6809JsrBsrRts();
void M6809PushPullMultipleRegisters();
void M6809LeaSetsFlagsAndLdsTracking();
void M6809TfrAndExg();
void M6809SwiAndRtiRoundTrip();
void M6809NmiMaskedUntilFirstStackWrite();
void M6809FirqPushesPartialState();
void M6809IrqMaskingAndPriorityOverFirq();
void M6809DaaAfterBcdAddition();
void M6809MulOperation();
void M6809SexSignExtends();
void M6809SyncWaitsForPendingInterrupt();
void M6809CwaiPushesStateAndWaits();
void M6809CyclesConsumedThisCallAdvancesInsideExecute();
void M6809DeterministicAcrossRuns();
