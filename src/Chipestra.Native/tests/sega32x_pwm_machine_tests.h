// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// The 32X PWM Machine (the cartridge's SH-2 programs play the sampled sounds): the <driver> settings, and, with the real
// cartridge, the sounds that come out for a request.

void Sega32xPwmOutputFilterIsTheBoardsInputNetwork();
void Sega32xPwmMixerFiltersAreTheBoardsCircuit();
void Sega32xPwmDriverConfigIsParsedAndChecked();
void Sega32xPwmMachinePlaysTheGamesSamples();
void Sega32xPwmMachineRunsFasterThanRealTime();
void Sega32xMusicMachinePlaysBothUnits();
void Sega32xMusicMachineMatchesTheGamesSongWrites();
void Sega32xMusicMachinePlaysTheStartJinglesOfTheMaster();
void Sega32xZ80MachineSendsTheEventsOfTheOriginal();
void Sega32xMusicMachineHandsTheDriversSamplesToThePwmUnit();
void Sega32xMusicMachineTellsTheSlaveTheZ80DriversRequests();
void Sega32xZ80DriverTempoMatchesTheOriginal();
void Sega32xStarWarsArcadeMatchesTheOriginal();
void Sega32xStarWarsArcadePwmMatchesTheOriginal();
void Sega32xSpaceHarrierLogoMatchesTheOriginal();
void Sega32xAfterBurnerLogoMatchesTheOriginal();
void Sega32xZ80GamePlaysItsSongs();
