// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// SH-2 CPU (ISh2Core + MAME-derived backend) native tests. CPU core only: every program is a hand-written synthetic
// sequence of opcodes.

void Sh2ResetReadsTheVectors();
void Sh2ArithmeticFlagsAgainstAReferenceModel();
void Sh2ShiftsRotatesAndSwaps();
void Sh2MultiplyAndMultiplyAccumulate();
void Sh2Div1UnsignedDivision();
void Sh2DelayedBranchesAndSubroutines();
void Sh2MemoryAccessForms();
void Sh2ExceptionsInterruptsAndReturn();
void Sh2CycleCountsPerInstruction();
