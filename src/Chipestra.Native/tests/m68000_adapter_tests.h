// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// IM68000Core + MAME-derived M68000 backend native tests - declared
// here, defined in m68000_adapter_tests.cpp, called from
// native_lifecycle_tests.cpp's main() (matching z80_adapter_tests.h's own
// convention).

void M68000ResetVectorLoadsInitialSpAndPc();
void M68000MoveqAndAddSetsFlagsCorrectly();
void M68000SubtractionSetsCarryAndOverflow();
void M68000BranchAndSubroutineCallRoundTrips();
void M68000DbccLoopCountsDownCorrectly();
void M68000AutovectorLevel7InterruptTakesExpectedFrame();
void M68000MaskedInterruptIsNotTakenUntilUnmasked();
void M68000MovemPredecrementAndPostincrementRoundTrip();
void M68000ShiftAndRotateMatchStandardSemantics();
void M68000ExecuteIsDeterministic();
void M68000MovepTransfersOddBytesOnly();
void M68000AbcdSbcdNbcdPerformDecimalCorrection();
void M68000AddqSubqWordToAddressRegisterUsesFull32Bits();
