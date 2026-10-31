// license:BSD-3-Clause
// copyright-holders:cam900, Brad Smith, Brezza
// Ricoh RP2C33 Sound emulation
// Based on:
// - NSFplay github code by Brad Smith/Brezza
// - Information from NESDev wiki (https://www.nesdev.org/wiki/FDS_audio)
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/sound/rp2c33_snd.cpp  [copyright-holders:cam900, Brad Smith, Brezza]
//   src/devices/sound/rp2c33_snd.h  [copyright-holders:cam900, Brad Smith, Brezza]
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#pragma once

#include "device_manager.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// Port of MAME's rp2c33_sound_device: the wavetable sound generator of the Famicom Disk System's
// RAM adapter (a 64-entry 6-bit wavetable with a volume envelope and a frequency modulator).
// No MAME type appears here.
//
// Time model: the chip is advanced by CPU cycles, never by a wall clock. One output sample is
// produced per CPU cycle (the original's stream runs at its clock, 21.477272 MHz / 12 = the
// Famicom CPU clock, about 1.79 MHz). A Machine calls Render() up to the CPU's current cycle
// before every register access, as the original updates its stream first in every read and write.
//
// Output: one value per sample, the original's mixed value divided by 32768 (|value| <= 1, no DC
// offset: the wavetable is centred on 32). The host decides the gain; the original adds the chip
// to the CPU's output at 0.2 on a mono output.
//
// The original marks the chip's register behaviours as not fully verified and does not model a
// low-pass filter; none is added here.
class Rp2c33SoundDeviceAdapter final : public IDevice {
public:
    // Input clock of the real chip (NTSC): 21.477272 MHz / 12.
    static constexpr std::uint32_t kClockHz = 1789772;

    Rp2c33SoundDeviceAdapter();

    // Power-on state: tables cleared, the wave and envelopes halted, the wavetable writable.
    void Reset();

    // $4080-$408A: `offset` 0x00-0x0A (other offsets from 0x00-0x0F are stored but do nothing).
    void Write(std::uint8_t offset, std::uint8_t value);
    // $4090-$4097: `offset` 0x10-0x17 (the original also answers $4093-$4097).
    std::uint8_t Read(std::uint8_t offset);

    // Wavetable RAM $4040-$407F. A write only takes effect while the wave write enable ($4089 bit 7)
    // is set. A read returns the entry with bit 6 set.
    void WaveWrite(std::uint8_t offset, std::uint8_t value);
    std::uint8_t WaveRead(std::uint8_t offset) const;

    // The disk adapter's bus side, by CPU address. The registers only answer while the sound I/O enable
    // ($4023 bit 1, set with SetSoundIoEnabled) is set; it is clear after Reset(). WriteIo routes
    // $4040-$407F to the wavetable and $4080, $4082-$408A to Write(); ReadIo answers the wavetable
    // ($4040-$407F), $4090 and $4092 (the envelope outputs) and 0 elsewhere or while disabled.
    void SetSoundIoEnabled(bool enabled) { io_enabled_ = enabled; }
    bool SoundIoEnabled() const { return io_enabled_; }
    void WriteIo(std::uint16_t address, std::uint8_t value);
    std::uint8_t ReadIo(std::uint16_t address);

    // Advances the chip by `cpuCycles` CPU cycles and appends one value per cycle to `out`.
    // Returns the number of samples appended.
    std::size_t Render(std::uint64_t cpuCycles, std::vector<float>& out);

    // Test-only observation points - not part of production playback.
    int VolumeEnvelopeOutForTest() const { return vol_env_out_; }
    int ModulatorEnvelopeOutForTest() const { return mod_env_out_; }
    int ModulatorPositionForTest() const { return mod_pos_; }
    std::uint8_t WaveAddressForTest() const { return wave_addr_; }
    bool WaveHaltedForTest() const { return wave_halt_; }

private:
    void ExecVolEnv();
    void ExecModEnv();
    void ExecMod();
    void ExecWave();

    bool io_enabled_ = false;
    std::uint8_t regs_[16] = {};
    std::uint8_t wave_[64] = {};  // 64 entries of wavetable, 6 bit unsigned

    bool vol_env_disable_ = true;
    bool vol_env_mode_ = false;
    int vol_env_spd_ = 0;
    std::uint32_t vol_env_clk_ = 0;
    int vol_env_out_ = 0;
    bool wave_halt_ = true;
    bool env_halt_ = true;
    int wave_freq_ = 0;
    std::uint32_t wave_acc_ = 0;  // .16 fixed point
    std::uint8_t wave_addr_ = 0;

    std::uint8_t mod_table_[32] = {};  // 32 entries, 3 bit unsigned
    bool mod_env_disable_ = true;
    bool mod_env_mode_ = false;
    int mod_env_spd_ = 0;
    std::uint32_t mod_env_clk_ = 0;
    int mod_env_out_ = 0;
    bool mod_halt_ = true;
    int mod_freq_ = 0;
    std::uint32_t mod_acc_ = 0;  // .16 fixed point
    std::uint8_t mod_addr_ = 0;
    int mod_pos_ = 0;  // 7 bit signed

    int env_spd_ = 0;
    bool wave_write_ = true;  // wavetable writable (no sound) or playing
    int mvol_ = 0;
    int output_ = 0;
    int mvol_table_[4] = {};
};
