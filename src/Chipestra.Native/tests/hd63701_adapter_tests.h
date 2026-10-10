// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// HD63701V0 (IHd63701Core + MAME-derived backend) native tests. Declared here so
// native_lifecycle_tests.cpp's main() can call them - same pattern as
// h6280_adapter_tests.h/m6809_adapter_tests.h/i8039_adapter_tests.h.
//
// CPU core only. Every program below is a hand-written synthetic byte sequence.

void Hd63701ResetState();
void Hd63701ImmediateLoadFlagsAndCycles();
void Hd63701AddSubAdcSbcFlags();
void Hd63701DirectIndexedExtendedAddressing();
void Hd63701ShiftAndRotateOps();
void Hd63701IncDecNegComClrAccumulatorAndMemory();
void Hd63701SixteenBitLoadStoreAndArithmetic();
void Hd63701ConditionalBranches();
void Hd63701JsrBsrRts();
void Hd63701PushPullStack();
void Hd63701SwiAndRtiRoundTrip();
void Hd63701NmiEdgeTriggeredAndPriorityOverIrq1();
void Hd63701Irq1MaskedByCcI();
void Hd63701WaiPushesStateAndWaitsForInterrupt();
void Hd63701SlpWaitsWithoutPrePush();
void Hd63701TapDelaysInterruptRecognitionByOneInstruction();
void Hd63701CliDelaysInterruptRecognitionByOneInstruction();
void Hd63701DaaAfterBcdAddition();
void Hd63701MulOperation();
void Hd63701TrapOnIllegalOpcode();
void Hd63701AimOimEimTimBitManipulation();
void Hd63701XgdxAndUndocumentedOpcodes();
void Hd63701PortGpioReadWrite();
void Hd63701TimerGeneratesIrq2OnOutputCompare();
void Hd63701CyclesConsumedThisCallAdvancesInsideExecute();
void Hd63701DeterministicAcrossRuns();
