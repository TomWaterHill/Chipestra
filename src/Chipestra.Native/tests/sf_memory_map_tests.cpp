// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "devices/capcom/sf/sf_memory_map.h"
#include "test_support.h"
#include <cstdint>
#include <cstdio>
#include <vector>
#include <utility>

void SfMemoryMapMusicCpuRomRamAndDevices() {
    std::printf("SfMemoryMapMusicCpuRomRamAndDevices\n");
    SfSoundMemoryMap map;
    std::vector<std::uint8_t> rom(0x8000, 0x5A);
    map.LoadMusicRom(rom.data(), rom.size());

    Check(map.ReadMusic(0x0000) == 0x5A, "ROM read at 0x0000 is 0x5A");
    Check(map.ReadMusic(0x7FFF) == 0x5A, "ROM read at 0x7FFF is 0x5A");
    map.WriteMusic(0x0000, 0xFF);
    Check(map.ReadMusic(0x0000) == 0x5A, "Write to ROM at 0x0000 is ignored");

    map.ResetState();
    Check(map.ReadMusic(0xC000) == 0, "RAM at 0xC000 is 0 after ResetState()");
    Check(map.ReadMusic(0xC7FF) == 0, "RAM at 0xC7FF is 0 after ResetState()");
    map.WriteMusic(0xC000, 0x77);
    Check(map.ReadMusic(0xC000) == 0x77, "RAM at 0xC000 can be written and read back");
    map.WriteMusic(0xC7FF, 0x88);
    Check(map.ReadMusic(0xC7FF) == 0x88, "RAM at 0xC7FF can be written and read back");

    std::uint8_t latch_value = 0x77;
    map.SetLatchRead([&]() { return latch_value; });
    Check(map.ReadMusic(0xC800) == 0x77, "Latch read at 0xC800 calls latch function");

    Check(map.ReadMusic(0xBFFF) == 0, "Unmapped read at 0xBFFF returns 0");
    Check(map.ReadMusic(0xC801) == 0, "Unmapped read at 0xC801 returns 0");
    Check(map.ReadMusic(0xE002) == 0, "Unmapped read at 0xE002 returns 0");

    std::uint8_t ym_value = 0x33;
    map.SetYm2151(
        [&]() { return ym_value; },
        [](std::uint8_t, std::uint8_t) {
            Check(false, "YM write callback should not be called in this test");
        });
    Check(map.ReadMusic(0xE000) == 0x33, "YM read at 0xE000 calls YM read function");
    Check(map.ReadMusic(0xE001) == 0x33, "YM read at 0xE001 calls YM read function");

    std::vector<std::pair<std::uint8_t, std::uint8_t>> write_log;
    map.SetYm2151(
        []() { return std::uint8_t{0}; },
        [&write_log](std::uint8_t offset, std::uint8_t value) {
            write_log.emplace_back(offset, value);
        });
    map.WriteMusic(0xE000, 1);
    map.WriteMusic(0xE001, 2);

    Check(write_log.size() == 2, "YM write function called exactly twice");
    Check(write_log[0].first == 0 && write_log[0].second == 1, "First write: offset 0, value 1");
    Check(write_log[1].first == 1 && write_log[1].second == 2, "Second write: offset 1, value 2");

    // Verify that writes to YM registers don't touch RAM
    map.WriteMusic(0xC000, 0x11);
    map.WriteMusic(0xE000, 1);
    Check(map.ReadMusic(0xC000) == 0x11, "RAM is not affected by YM writes");

    map.ResetState();
    Check(map.ReadMusic(0xC000) == 0, "RAM is cleared after ResetState()");
}

void SfMemoryMapSamplesCpuBankWindow() {
    std::printf("SfMemoryMapSamplesCpuBankWindow\n");
    SfSoundMemoryMap map;

    std::vector<std::uint8_t> sample_image(SfSoundMemoryMap::kSampleImageSize);
    for (std::size_t i = 0; i < sample_image.size(); ++i) {
        sample_image[i] = static_cast<std::uint8_t>((i >> 8) & 0xFF) ^ static_cast<std::uint8_t>(i & 0xFF);
    }
    map.LoadSampleImage(sample_image.data(), sample_image.size());

    Check(map.ReadSamples(0x0000) == sample_image[0], "ReadSamples(0x0000) equals image[0]");
    Check(map.ReadSamples(0x7FFF) == sample_image[0x7FFF], "ReadSamples(0x7FFF) equals image[0x7FFF]");

    Check(map.ReadSamples(0x8000) == sample_image[0x8000], "Bank 0: ReadSamples(0x8000) equals image[0x8000]");
    Check(map.ReadSamples(0xFFFF) == sample_image[0xFFFF], "Bank 0: ReadSamples(0xFFFF) equals image[0xFFFF]");

    map.SetSampleBank(1);
    Check(map.ReadSamples(0x8000) == sample_image[0x10000], "Bank 1: ReadSamples(0x8000) equals image[0x10000]");

    map.SetSampleBank(6);
    Check(map.ReadSamples(0xFFFF) == sample_image[0x3FFFF], "Bank 6: ReadSamples(0xFFFF) equals image[0x3FFFF]");

    map.SetSampleBank(7);
    Check(map.ReadSamples(0x8000) == SfSoundMemoryMap::kPastImageByte, "Bank 7: ReadSamples(0x8000) returns 0xFF");

    map.SetSampleBank(255);
    Check(map.ReadSamples(0x8000) == SfSoundMemoryMap::kPastImageByte, "Bank 255: ReadSamples(0x8000) returns 0xFF");

    map.ResetState();
    Check(map.SampleBank() == 0, "ResetState() sets bank back to 0");
}

void SfMemoryMapShortImagesAreZeroPadded() {
    std::printf("SfMemoryMapShortImagesAreZeroPadded\n");
    SfSoundMemoryMap map;

    std::vector<std::uint8_t> rom(4, 0xEE);
    map.LoadMusicRom(rom.data(), rom.size());
    Check(map.ReadMusic(3) == 0xEE, "LoadMusicRom with 4 bytes: ReadMusic(3) == 0xEE");
    Check(map.ReadMusic(4) == 0, "LoadMusicRom with 4 bytes: ReadMusic(4) == 0");

    map.LoadSampleImage(nullptr, 0);
    Check(map.ReadSamples(0) == 0, "LoadSampleImage with nullptr,0: ReadSamples(0) == 0");
    map.SetSampleBank(0);
    Check(map.ReadSamples(0x8000) == 0, "LoadSampleImage with nullptr,0: ReadSamples(0x8000) == 0");

    std::vector<std::uint8_t> long_image(0x50000, 0x11);
    map.LoadSampleImage(long_image.data(), long_image.size());
    Check(map.ReadSamples(0x7FFF) == 0x11, "LoadSampleImage with 0x50000 bytes: ReadSamples(0x7FFF) == 0x11");
}

void SfMemoryMapWithoutDevicesReadsZero() {
    std::printf("SfMemoryMapWithoutDevicesReadsZero\n");
    SfSoundMemoryMap map;

    Check(map.ReadMusic(0xC800) == 0, "ReadMusic(0xC800) returns 0 without latch function");
    Check(map.ReadMusic(0xE000) == 0, "ReadMusic(0xE000) returns 0 without YM function");
    map.WriteMusic(0xE000, 1);
    Check(true, "WriteMusic(0xE000, 1) does not crash without YM write function");
}
