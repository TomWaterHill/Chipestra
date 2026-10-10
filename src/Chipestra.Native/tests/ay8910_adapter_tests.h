// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// Ay8910DeviceAdapter native tests. Declared here so
// native_lifecycle_tests.cpp's main() can call them (one shared test
// executable with a single main()).
//
// Ordered: register semantics -> tone/noise/envelope generator output ->
// channel output. All stimulus is synthetic (hand-written register writes);
// nothing reads a real ROM.

void Ay8910RegisterSemanticsUpdateInternalState();
void Ay8910PortWriteCallbackFiresOnEnableEdge();
void Ay8910ToneGeneratorOutputMatchesProgrammedPeriod();
void Ay8910NoiseGeneratorAdvancesThePseudoRandomSequence();
void Ay8910EnvelopeGeneratorFollowsAKnownShape();
void Ay8910ChannelOutputReachesPerChannelBuffers();
void Ay8910LegacyNormalizedMixHasNoDcOffsetAndMatchesMameLevels();
void Ay8910RenderIsDeterministicAndLosesNoChipTime();
