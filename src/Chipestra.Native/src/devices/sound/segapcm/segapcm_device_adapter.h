// license:BSD-3-Clause
// copyright-holders:Hiromitsu Shioya, Olivier Galibert
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/sound/segapcm.cpp
//   src/devices/sound/segapcm.h
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#pragma once

#include "address_space.h"
#include "clock.h"
#include "device_manager.h"

#include <cstdint>
#include <vector>

// Port of MAME's sega_315_5218_device (SegaPCM, 16-channel 8-bit PCM). The
// voice tick logic and register semantics are ported from MAME's segapcm.cpp;
// see THIRD-PARTY-NOTICES.md. No MAME type appears in this header or the
// .cpp: the register file, the 16 voices and the sample ROM are owned here,
// and the chip is driven by a plain Render() call.
//
// Deterministic time model: never reads a wall clock. Playback advances a
// 16MHz-master-clock counter only inside Render(); one output sample is
// kOutputClockDivider master clocks (one voice tick per voice).
//
// Implements IDevice so a Machine's DeviceManager owns it, and
// AddressSpaceEndpoint so a Machine's memory map can map its register window
// directly (the sound Z80 sees SegaPCM at memory 0xF000-0xF0FF, mirror 0x0700,
// on X-Board).
class SegaPcmDeviceAdapter final : public IDevice, public AddressSpaceEndpoint {
public:
    // sega_315_5218 = 16 voices (segapcm.h: segapcm_device<16>).
    static constexpr std::uint32_t kVoiceCount = 16;

    // 315-5218 master input (16.000MHz). MAME wires SOUND_CLOCK/4 (= 4MHz)
    // with CLOCK_DIVIDER = 16*8 = 128; 16MHz with a /512 divider is the
    // identical 31250Hz stream. The 16MHz form keeps the master clock explicit.
    static constexpr ClockRate kMasterClockRate{16'000'000};
    static constexpr std::uint32_t kOutputClockDivider = 512;
    static constexpr std::uint32_t kOutputSampleRateHz = 16'000'000u / 512u;  // 31250

    // Bank decode selectors, mirroring MAME sega_315_5218_device::BANK_*.
    // X-Board uses BANK_512.
    static constexpr int kBank256 = 11;
    static constexpr int kBank512 = 12;
    static constexpr int kBank12M = 13;

    // `sampleRom` is the PCM sample data (copied in). `bank` is one of the
    // kBank* selectors above, chosen by the owning Machine (bank/layout is
    // Machine config, never game XML).
    SegaPcmDeviceAdapter(std::vector<std::uint8_t> sampleRom, int bank);

    SegaPcmDeviceAdapter(const SegaPcmDeviceAdapter&) = delete;
    SegaPcmDeviceAdapter& operator=(const SegaPcmDeviceAdapter&) = delete;

    // Power-on-equivalent reset: register RAM and all voice registers back to
    // 0xFF / disabled, playback position cleared.
    void Reset();

    // Register-file access (layout: segapcm.cpp's register table). `offset`
    // is 0x00-0xFF; voice index = bits 3-6, register = offset & 0x87.
    void WriteRegister(std::uint8_t offset, std::uint8_t value);
    std::uint8_t ReadRegister(std::uint8_t offset) const;

    // AddressSpaceEndpoint: the mapped window carries the 0x0700 mirror, so
    // only the low 8 bits of `offset` select a register.
    std::uint8_t Read8(std::uint64_t offset) const override;
    void Write8(std::uint64_t offset, std::uint8_t value) override;

    // Advances the chip by `masterClocks` 16MHz cycles and appends each
    // voice's mono float sample to `voiceSamples` (a kVoiceCount-long vector
    // of buffers; entry v gets voice v). The sample is the voice's raw
    // waveform normalized to [-1, 1], WITHOUT the per-voice left/right volume
    // applied - the owning Machine reads VoiceVolumeLeft/Right() and folds the
    // balance into the LogicalChannel's pan/routing. Partial cycles
    // (< kOutputClockDivider) carry to the next call. Returns the number of
    // output samples appended to each buffer.
    std::size_t Render(std::uint64_t masterClocks, std::vector<std::vector<float>>& voiceSamples);

    // Per-voice 7-bit left / right output volumes (registers 0x02 / 0x03).
    // The Machine maps (l, r) onto the LogicalChannel's pan and routing gain;
    // MAME scales each voice as sample * (vol & 0x7f) / 32768 per side.
    std::uint8_t VoiceVolumeLeft(std::uint32_t voice) const;
    std::uint8_t VoiceVolumeRight(std::uint32_t voice) const;

    // Test-only observation: output samples generated since construction/
    // Reset(). Not a production code path.
    std::uint64_t GeneratedSampleCount() const;

    // Diagnostics: how many times the register file was written / read since
    // construction.
    struct AccessCounts {
        std::uint64_t Writes = 0;
        std::uint64_t Reads = 0;
    };
    AccessCounts RegisterAccessCounts() const { return {register_write_count_, register_read_count_}; }

    // Diagnostic: output samples where some voice's left and right differed
    // (the driver used per-voice pan).
    std::uint64_t StereoSampleCount() const { return stereo_samples_; }

private:
    // One PCM voice. Field names/semantics track MAME segapcm_base_device::
    // voice_t. `addr` is 16.8 fixed point (low 8 bits fractional).
    struct Voice {
        std::uint32_t addr = 0xFFFFFFFFu;
        std::uint16_t loop = 0xFFFFu;
        std::uint8_t end = 0xFFu;
        std::uint8_t freq = 0xFFu;
        std::uint8_t lvol = 0xFFu;
        std::uint8_t rvol = 0xFFu;
        std::uint8_t ctrl = 0xFFu;  // bit0 set = disabled (power-on state)
    };

    // Bank decode, mirroring MAME sega_315_5218_device::set_bank():
    // bank offset for a voice = (ctrl & mask) << shift.
    struct BankDecode {
        std::uint32_t shift;
        std::uint32_t mask;
    };
    static BankDecode DecodeBank(int bank);

    void TickVoice(Voice& voice, std::int32_t& lout, std::int32_t& rout) const;
    std::uint8_t ReadSample(std::uint32_t byteAddr) const;

    std::vector<std::uint8_t> sample_rom_;
    BankDecode bank_;
    Voice voices_[kVoiceCount];
    std::uint8_t register_ram_[256];
    std::uint64_t partial_master_clocks_ = 0;
    std::uint64_t generated_samples_ = 0;
    std::uint64_t register_write_count_ = 0;
    mutable std::uint64_t register_read_count_ = 0;
    std::uint64_t stereo_samples_ = 0;
};
