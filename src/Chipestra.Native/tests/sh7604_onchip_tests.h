// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// SH7604 on-chip peripherals (free-running timer, watchdog timer, divide unit, interrupt controller, bus state controller)
// native tests. Synthetic: the peripherals run against a recording stand-in for the CPU core.

void Sh7604RegistersAreReadBackAtAnyWidth();
void Sh7604DivideUnit();
void Sh7604FreeRunningTimerAndItsInterrupts();
void Sh7604WatchdogAndBusController();
void Sh7604DmaController();
