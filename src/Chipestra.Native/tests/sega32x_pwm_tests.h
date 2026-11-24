// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// Sega 32X PWM sound unit native tests. Each check says in its text whether it holds because MAME does the same ("as MAME")
// or because the hardware description says so ("judged correct").

void Sega32xPwmFifoStatusAndRegisters();
void Sega32xPwmSamplePeriodLevelAndRouting();
void Sega32xPwmInterruptIntervalAndRunningConditions();
void Sega32xPwmRasterizesTheChangesToAnOutputRate();
