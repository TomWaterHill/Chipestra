// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// RP2A03 CPU (IM6502Core + MAME-derived backend) native tests. Declared here so
// native_lifecycle_tests.cpp's main() can call them - same pattern as
// m6809_adapter_tests.h/h6280_adapter_tests.h.
//
// CPU core only. Every program below is a hand-written synthetic byte sequence.

void Rp2a03ResetState();
void Rp2a03ResetBusSequence();
void Rp2a03LoadStoreFlagsAndCycles();
void Rp2a03OfficialOpcodeCycleCounts();
void Rp2a03UndocumentedOpcodeCycleCounts();
void Rp2a03PageCrossPenaltyAndDummyReads();
void Rp2a03AdcSbcExhaustiveBinary();
void Rp2a03DecimalFlagIsIgnored();
void Rp2a03LogicCompareBitExhaustive();
void Rp2a03ShiftRotateIncDec();
void Rp2a03AddressingWraparound();
void Rp2a03BranchesTakenAndPageCross();
void Rp2a03StackAndSubroutines();
void Rp2a03BrkAndRti();
void Rp2a03IrqTakenMaskedAndLevelTriggered();
void Rp2a03NmiEdgeAndPriority();
void Rp2a03NmiIgnoredBeforeFirstCycle();
void Rp2a03NmiDuringBrkTakesNmiVector();
void Rp2a03SetOverflowPin();
void Rp2a03UndocumentedReadModifyWriteOpcodes();
void Rp2a03UndocumentedLoadStoreAndImmediateOpcodes();
void Rp2a03UndocumentedHighByteStoreOpcodes();
void Rp2a03KilJamsTheCpu();
void Rp2a03CyclesConsumedThisCallAdvancesInsideExecute();
void Rp2a03OpcodeObserverSeesEveryInstruction();
void Rp2a03DeterministicAcrossRuns();
