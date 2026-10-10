// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// IH6280Core + MAME-derived HuC6280 backend native tests. Declared here so
// native_lifecycle_tests.cpp's main() can call them - same pattern as
// z80_adapter_tests.h/i8039_adapter_tests.h.

void H6280ResetState();
void H6280LoadImmediateFlagsAndCycles();
void H6280MemoryMapperTamTma();
void H6280ZeroPageAndStackUseMapperRegister1();
void H6280AddressingModesLoadFromMappedRam();
void H6280BlockTransferTiiCyclesAndData();
void H6280BlockTransferTinTddTiaTai();
void H6280BranchesAndBitBranches();
void H6280DecimalAndBinaryArithmeticFlags();
void H6280TransferFlagAndRegisterSwaps();
void H6280BitOpsTstTsbTrbRmbSmb();
void H6280StackSubroutinesAndTransfers();
void H6280InternalIoPageIsNotForwarded();
void H6280InterruptsVectorsMaskPriorityAndRti();
void H6280TimerFiresTiqAndScaleDoublesPeriod();
void H6280SpeedModeCshCsl();
void H6280CyclesConsumedThisCallAdvancesInsideExecute();
void H6280DeterministicAcrossRuns();
