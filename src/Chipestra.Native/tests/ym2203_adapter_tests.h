// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// Ym2203DeviceAdapter native tests. Declared here so
// native_lifecycle_tests.cpp's main() can call them - matches the
// ym2151_adapter_tests.h convention.
//
// Tests are ordered: register semantics -> timer behavior -> channel output
// (FM + SSG) -> determinism. All stimulus is synthetic (hand-written
// register writes); nothing reads a real ROM.

void Ym2203RegisterAccessAndStatusBusy();
void Ym2203TimerAFiresAfterProgrammedPeriod();
void Ym2203TimerBFiresAfterProgrammedPeriod();
void Ym2203ChannelOutputReachesPerChannelBuffers();
void Ym2203RenderIsDeterministicAndLosesNoChipTime();
