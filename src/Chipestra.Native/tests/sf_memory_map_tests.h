// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include <cstdint>
#include <cstdio>
#include <vector>

void SfMemoryMapMusicCpuRomRamAndDevices();
void SfMemoryMapSamplesCpuBankWindow();
void SfMemoryMapShortImagesAreZeroPadded();
void SfMemoryMapWithoutDevicesReadsZero();
