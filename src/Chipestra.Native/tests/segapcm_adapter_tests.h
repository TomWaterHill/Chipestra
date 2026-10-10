// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// SegaPcmDeviceAdapter native tests. Declared here so
// native_lifecycle_tests.cpp's main() can call them (one shared test
// executable).
//
// Tests are ordered: register semantics -> voice enable/bank -> loop/address
// delta -> channel output, then determinism and the end-to-end
// XBoardMachine -> resampler -> Mixer path. All PCM data is hand-written
// synthetic.

void SegaPcmVoiceRegisterReadback();
void SegaPcmVoiceEnableAndBankSelect();
void SegaPcmLoopAndStopAtEndAddress();
void SegaPcmVoiceChannelOutputAndPan();
void SegaPcmRenderIsDeterministicAndLosesNoMasterClocks();
void XBoardMachineSegaPcmAudioReachesMixer();
