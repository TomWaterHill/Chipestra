// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// Famicom Disk System tests: the disk image reader (fds_disk) and FdsMachine, declared here so
// native_lifecycle_tests.cpp's main() can call them - same pattern as the other Machine tests.
//
// The synthetic tests build their own disk images and driver; the real-disk test needs
// CHIPESTRA_ROM_ROOT (the disk) and CHIPESTRA_REFERENCE_DIR (recordings of the real game).

void FdsDiskReadsFilesWithAndWithoutHeader();
void FdsDiskRejectsBrokenImages();
void FdsMachineInputsAreCheckedByNameAndCrc();
void FdsMachineLoadsFilesAndHandlesRequests();
void FdsMachineSetsOverlayInOrderAndSwitchingResets();
void FdsMachineHeaderDoesNotChangeTheSound();
void FdsMachineCountsBiosAndDiskAccesses();
void FdsMachineSoundChannelsAndMute();
void FdsMachineViaDefinitionXmlRealDisk();
