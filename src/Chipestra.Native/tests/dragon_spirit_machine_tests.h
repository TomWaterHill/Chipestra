// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// DragonSpiritMachine native tests, called from native_lifecycle_tests.cpp's main().
//
// Boot warm-up, command reaches audiocpu via TRIRAM, VBLANK IRQ assertion + the software-ack wiring at
// 0xE000, bank switching (8 distinct banks), each device reachable via the memory map, determinism,
// SendCommand re-entrancy safety. All ROM bytes are hand-written synthetic programs (no real Dragon
// Spirit ROM data); a NOP-filled ROM is sufficient because every test observes wiring through
// DragonSpiritMachine's test accessors, not by running a hand-crafted driver's interrupt handler.

void DragonSpiritMachineRegisteredInDefaultCatalog();
void DragonSpiritMachineExposes18LogicalChannels();
void DragonSpiritMachineSendCommandWritesTriRam();
void DragonSpiritMachineSendCommandIsReentrantSafe();
void DragonSpiritMachineVblankIrqAssertsAndCanBeAcked();
void DragonSpiritMachineBankSwitchSelectsAmongEightBanks();
void DragonSpiritMachineDevicesReachableViaMemoryMap();
void DragonSpiritMachineRenderIsDeterministicAcrossInstances();

// MCU integration: a separate NOP-filled synthetic ROM for the MCU (mcu_rom, the constructor's optional
// second argument), same "hand-written synthetic program, no real ROM data" convention.
void DragonSpiritMachineMcuBootsAlongsideAudiocpu();
void DragonSpiritMachineTriRamIsSharedBetweenAudiocpuAndMcu();
void DragonSpiritMachineMcuVblankIrq1AssertsAndCanBeAcked();
void DragonSpiritMachineMcuBankSwitchSelectsAmongTwentyFourBanks();
void DragonSpiritMachineDac0Dac1ReceiveValuesViaMcuBusWrites();

// AssembleDragonSpiritSoundRoms: mcu/voice ROM resource wiring.
void DragonSpiritAssembleSoundRomsThrowsWhenRequiredResourceMissing();
void DragonSpiritAssembleSoundRomsMirrorsVoice0AcrossChip0();
void DragonSpiritAssembleSoundRomsPlacesVoiceChipsAtCorrectOffsets();
void DragonSpiritViaDefinitionXmlRealRomBringUp();

// Reset on song switch (SendCommand() resets the Machine once anything has been sent before).
void DragonSpiritFirstCommandAfterInitializeDoesNotReset();
void DragonSpiritLaterCommandsReset();
void DragonSpiritResetKeepsChannelMuteGainPan();
void DragonSpiritSwitchingSongsMatchesAFreshMachine();

// MusicCommandKind::Wsg (<track type="wsg">).
void DragonSpiritWsgWritesTheRequestTableAndLeavesTriRamAlone();
void DragonSpiritAcceptsWsgOnlyUpToTheLastNumber();
void DragonSpiritWsgAfterACommandResetsFirst();
void DragonSpiritWsgRealRomPlaysEveryListedNumber();
