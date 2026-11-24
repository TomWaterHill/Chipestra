// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// Sega 32X SH-2 side native tests: the boot from the cartridge header, the communication registers shared with the 68000,
// the interrupt masks and clears, and the PWM unit driven by SH-2 code. The programs are hand-assembled.

void Sega32xSystemBootsFromTheCartridgeHeader();
void Sega32xSystemCommunicationRegisters();
void Sega32xSystemPwmInterruptServiceFeedsTheFifo();
void Sega32xSystemVerticalInterruptMaskAndClear();
