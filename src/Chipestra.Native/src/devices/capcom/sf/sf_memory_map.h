// license:BSD-3-Clause
// copyright-holders:Olivier Galibert
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Portions derived from MAME (src/mame/capcom/sf.cpp); see THIRD-PARTY-NOTICES.md.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

// The memory maps of the two sound Z80s.
//
// Z80 #1 (music):
//   0x0000-0x7FFF  ROM (32KB)
//   0xC000-0xC7FF  RAM (2KB)
//   0xC800         command latch (read)
//   0xE000-0xE001  YM2151 (write: address/data; read: status from either address)
// Z80 #2 (samples) has no RAM:
//   0x0000-0x7FFF  ROM (first 32KB of the 256KB sample image)
//   0x8000-0xFFFF  32KB window onto the sample image, selected by I/O port 0x02
//   writes to memory are dropped
//   I/O 0x00 / 0x01 write: MSM5205 #1 / #2;  0x01 read: command latch;  0x02 write: bank select
// Unmapped reads return 0x00.
class SfSoundMemoryMap {
public:
    static constexpr std::size_t kMusicRomSize = 0x8000;
    static constexpr std::uint16_t kMusicRamStart = 0xC000;
    static constexpr std::size_t kMusicRamSize = 0x800;
    static constexpr std::uint16_t kLatchAddress = 0xC800;
    static constexpr std::uint16_t kYm2151Address = 0xE000;

    static constexpr std::size_t kSampleImageSize = 0x40000;
    static constexpr std::size_t kSampleRomWindowSize = 0x8000;
    static constexpr std::size_t kSampleBankSize = 0x8000;
    // The bank register is 8 bits wide, but the image only holds banks 0..6 (bank n shows image bytes
    // 0x8000 + n*0x8000 ...). A bank that points past the image reads 0xFF.
    static constexpr std::uint8_t kPastImageByte = 0xFF;

    using LatchReadFn = std::function<std::uint8_t()>;
    using YmReadFn = std::function<std::uint8_t()>;
    using YmWriteFn = std::function<void(std::uint8_t offset, std::uint8_t value)>;

    SfSoundMemoryMap();

    // Copy at most the region size; the rest of the region is zero. An empty input leaves it zero.
    void LoadMusicRom(const std::uint8_t* data, std::size_t size);
    void LoadSampleImage(const std::uint8_t* data, std::size_t size);

    void SetLatchRead(LatchReadFn fn) { latch_read_ = std::move(fn); }
    void SetYm2151(YmReadFn read, YmWriteFn write) {
        ym_read_ = std::move(read);
        ym_write_ = std::move(write);
    }

    // Power-on state of everything this class owns except the ROM contents: RAM cleared, bank 0.
    void ResetState();

    std::uint8_t ReadMusic(std::uint16_t address) const;
    void WriteMusic(std::uint16_t address, std::uint8_t value);

    std::uint8_t ReadSamples(std::uint16_t address) const;
    void SetSampleBank(std::uint8_t bank) { sample_bank_ = bank; }
    std::uint8_t SampleBank() const { return sample_bank_; }

private:
    std::array<std::uint8_t, kMusicRomSize> music_rom_{};
    std::array<std::uint8_t, kMusicRamSize> music_ram_{};
    std::vector<std::uint8_t> sample_image_;
    std::uint8_t sample_bank_ = 0;
    LatchReadFn latch_read_;
    YmReadFn ym_read_;
    YmWriteFn ym_write_;
};
