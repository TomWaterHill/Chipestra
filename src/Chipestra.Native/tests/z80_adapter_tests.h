// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// IZ80Core + MAME-derived Z80 backend native tests. Declared here
// so native_lifecycle_tests.cpp's main() can call them - see that file's
// top-of-file comment for why this project uses one shared test
// executable with a single main() instead of a full test framework.

void Z80BasicLoadAndCycleCounting();
void Z80IncDecBoundaryFlags();
void Z80DjnzDoesNotAffectFlags();
void Z80AddCarryOverflowFlags();
void Z80AddHl16BitFlags();
void Z80SbcAdcHl16BitZeroFlag();
void Z80JumpSkipsInterveningCode();
void Z80CallAndReturnRoundTrip();
void Z80PushPopAfRoundTripsFlags();
void Z80LdAiAndLdArReadTheCorrectRegister();
void Z80CbBitSetResOnMemory();
void Z80IndexedIxLoadAndStore();
void Z80IndexedLoadStoreKeepsRealHlOperand();
void Z80NmiServicedCorrectly();
void Z80MaskableInterruptImOneWithEiDelay();
void Z80MaskableInterruptImTwoVectorTable();
void Z80MaskableInterruptImZeroCall();
void Z80MaskableInterruptImZeroJp();
void Z80MaskableInterruptImZeroRst();
void Z80MaskableInterruptImZeroEi();
void Z80MaskableInterruptImZeroUnhandledByteDoesNothing();
void Z80HaltWakesOnInterrupt();
void Z80AddressSpaceIntegration();
void Z80ExecuteIsDeterministic();
