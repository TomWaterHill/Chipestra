// license:BSD-3-Clause
// copyright-holders:cam900, Brad Smith, Brezza
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/sound/rp2c33_snd.cpp  [copyright-holders:cam900, Brad Smith, Brezza]
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#include "devices/sound/rp2c33/rp2c33_sound_device_adapter.h"

#include <algorithm>
#include <iterator>

namespace {

constexpr int kModInc[8] = {0, 1, 2, 4, -4, -4, -2, -1};

bool Bit(int value, int bit) { return ((value >> bit) & 1) != 0; }

}  // namespace

Rp2c33SoundDeviceAdapter::Rp2c33SoundDeviceAdapter() { Reset(); }

void Rp2c33SoundDeviceAdapter::WriteIo(std::uint16_t address, std::uint8_t value) {
    if (!io_enabled_) return;
    if (address >= 0x4040 && address < 0x4080) {
        WaveWrite(static_cast<std::uint8_t>(address - 0x4040), value);
    } else if (address == 0x4080 || (address >= 0x4082 && address <= 0x408a)) {
        Write(static_cast<std::uint8_t>(address - 0x4080), value);
    }
}

std::uint8_t Rp2c33SoundDeviceAdapter::ReadIo(std::uint16_t address) {
    if (!io_enabled_) return 0;
    if (address >= 0x4040 && address < 0x4080) return WaveRead(static_cast<std::uint8_t>(address - 0x4040));
    if (address == 0x4090 || address == 0x4092) return Read(static_cast<std::uint8_t>(address - 0x4080));
    return 0;
}

void Rp2c33SoundDeviceAdapter::Reset() {
    io_enabled_ = false;
    std::fill(std::begin(regs_), std::end(regs_), std::uint8_t{0});
    std::fill(std::begin(wave_), std::end(wave_), std::uint8_t{0});
    std::fill(std::begin(mod_table_), std::end(mod_table_), std::uint8_t{0});
    vol_env_disable_ = true;
    vol_env_mode_ = false;
    vol_env_spd_ = 0;
    vol_env_clk_ = 0;
    vol_env_out_ = 0;
    wave_halt_ = true;
    env_halt_ = true;
    wave_freq_ = 0;
    wave_acc_ = 0;
    wave_addr_ = 0;
    mod_env_disable_ = true;
    mod_env_mode_ = false;
    mod_env_spd_ = 0;
    mod_env_clk_ = 0;
    mod_env_out_ = 0;
    mod_halt_ = true;
    mod_freq_ = 0;
    mod_acc_ = 0;
    mod_addr_ = 0;
    mod_pos_ = 0;
    env_spd_ = 0;
    wave_write_ = true;
    mvol_ = 0;
    output_ = 0;
    // normalize output to 16 bit
    for (int i = 0; i < 4; i++) mvol_table_[i] = static_cast<int>((65536.0 / (32.0 * 64.0)) * 2.0 / static_cast<double>(i + 2));
}

void Rp2c33SoundDeviceAdapter::WaveWrite(std::uint8_t offset, std::uint8_t value) {
    if (wave_write_) wave_[offset & 0x3f] = value & 0x3f;
}

std::uint8_t Rp2c33SoundDeviceAdapter::WaveRead(std::uint8_t offset) const {
    return static_cast<std::uint8_t>((wave_[offset & 0x3f] & 0x3f) | 0x40);
}

std::uint8_t Rp2c33SoundDeviceAdapter::Read(std::uint8_t offset) {
    std::uint8_t ret = 0;
    switch (0x4080 + offset) {
        case 0x4090:  // volume envelope output
            ret = static_cast<std::uint8_t>((vol_env_out_ & 0x3f) | 0x40);
            break;
        case 0x4091:  // bit 12-19 of accumulator
            ret = static_cast<std::uint8_t>((wave_addr_ << 4) + (wave_acc_ >> 12));
            break;
        case 0x4092:  // modulator envelope output
            ret = static_cast<std::uint8_t>((mod_env_out_ & 0x3f) | 0x40);
            break;
        case 0x4093:  // bit 5-11 of modtable address
            ret = static_cast<std::uint8_t>((mod_addr_ >> 5) & 0x7f);
            break;
        case 0x4094:  // mod counter + gain result
            break;
        case 0x4095:  // mod counter increment
            ret = static_cast<std::uint8_t>(kModInc[mod_table_[(mod_addr_ >> 1) & 0x1f]] & 0xf);
            break;
        case 0x4096:  // wavetable value
            ret = static_cast<std::uint8_t>((wave_[wave_addr_ & 0x3f] & 0x3f) | 0x40);
            break;
        case 0x4097:  // modulator position
            ret = static_cast<std::uint8_t>(mod_pos_ & 0x7f);
            break;
        default:
            break;
    }
    return ret;
}

void Rp2c33SoundDeviceAdapter::Write(std::uint8_t offset, std::uint8_t data) {
    regs_[offset & 0xf] = data;
    switch (0x4080 + (offset & 0xf)) {
        case 0x4080:  // volume envelope
            vol_env_disable_ = Bit(data, 7);
            vol_env_mode_ = Bit(data, 6);
            vol_env_spd_ = data & 0x3f;
            vol_env_clk_ = 0;
            if (vol_env_disable_) vol_env_out_ = vol_env_spd_;
            break;
        case 0x4082:  // wave frequency low
            wave_freq_ = (wave_freq_ & 0xf00) | (data & 0xff);
            break;
        case 0x4083:  // wave frequency high
            wave_halt_ = Bit(data, 7);
            env_halt_ = Bit(data, 6);
            wave_freq_ = (wave_freq_ & 0x0ff) | ((data & 0xf) << 8);
            if (wave_halt_) wave_acc_ = 0;
            if (env_halt_) vol_env_clk_ = mod_env_clk_ = 0;
            break;
        case 0x4084:  // modulator envelope
            mod_env_disable_ = Bit(data, 7);
            mod_env_mode_ = Bit(data, 6);
            mod_env_spd_ = data & 0x3f;
            mod_env_clk_ = 0;
            if (mod_env_disable_) mod_env_out_ = mod_env_spd_;
            break;
        case 0x4085:  // modulator position
            mod_pos_ = data & 0x7f;
            break;
        case 0x4086:  // modulator frequency low
            mod_freq_ = (mod_freq_ & 0xf00) | (data & 0xff);
            break;
        case 0x4087:  // modulator frequency high
            mod_halt_ = Bit(data, 7);
            mod_freq_ = (mod_freq_ & 0x0ff) | ((data & 0xf) << 8);
            if (mod_halt_) mod_acc_ = 0;
            break;
        case 0x4088:  // modulator table
            if (mod_halt_) {
                mod_table_[(mod_addr_ >> 1) & 0x1f] = data & 7;
                mod_addr_ = static_cast<std::uint8_t>(mod_addr_ + 2);
            }
            break;
        case 0x4089:  // wave write, master volume
            wave_write_ = Bit(data, 7);
            mvol_ = data & 3;
            break;
        case 0x408a:  // envelope speed
            env_spd_ = data;
            break;
        default:
            break;
    }
}

std::size_t Rp2c33SoundDeviceAdapter::Render(std::uint64_t cpuCycles, std::vector<float>& out) {
    out.reserve(out.size() + static_cast<std::size_t>(cpuCycles));
    for (std::uint64_t i = 0; i < cpuCycles; ++i) {
        output_ = 0;
        if (!env_halt_ && !wave_halt_) {
            ExecVolEnv();
            ExecModEnv();
        }
        ExecMod();
        ExecWave();
        out.push_back(static_cast<float>(output_ * mvol_table_[mvol_]) / 32768.0f);
    }
    return static_cast<std::size_t>(cpuCycles);
}

// volume envelope: input clock / ((overall speed + 1) * (volume envelope speed + 1) * 8)
void Rp2c33SoundDeviceAdapter::ExecVolEnv() {
    if (!vol_env_disable_) {
        const std::uint32_t period = static_cast<std::uint32_t>((vol_env_spd_ + 1) * (env_spd_ + 1)) << 3;
        if (++vol_env_clk_ > period) {
            if (vol_env_mode_) {
                if (vol_env_out_ < 32) ++vol_env_out_;
            } else {
                if (vol_env_out_ > 0) --vol_env_out_;
            }
            vol_env_clk_ = 0;
        }
    }
}

// modulator envelope: input clock / ((overall speed + 1) * (modulator envelope speed + 1) * 8)
void Rp2c33SoundDeviceAdapter::ExecModEnv() {
    if (!mod_env_disable_) {
        const std::uint32_t period = static_cast<std::uint32_t>((mod_env_spd_ + 1) * (env_spd_ + 1)) << 3;
        if (++mod_env_clk_ > period) {
            if (mod_env_mode_) {
                if (mod_env_out_ < 32) ++mod_env_out_;
            } else {
                if (mod_env_out_ > 0) --mod_env_out_;
            }
            mod_env_clk_ = 0;
        }
    }
}

// modulator table: input clock * frequency / 65536
void Rp2c33SoundDeviceAdapter::ExecMod() {
    if (!mod_halt_) {
        mod_acc_ += static_cast<std::uint32_t>(mod_freq_);
        while (mod_acc_ > 0xffff) {
            const int val = mod_table_[(mod_addr_++ >> 1) & 0x1f] & 7;
            mod_acc_ -= (1u << 16);
            if (val == 4)
                mod_pos_ = 0;
            else
                mod_pos_ = (mod_pos_ + kModInc[val]) & 0x7f;
        }
    }
}

// wavetable output
void Rp2c33SoundDeviceAdapter::ExecWave() {
    if (!wave_halt_) {
        int mod = 0;
        if (mod_env_out_ != 0)  // skip if modulator off
        {
            // convert mod_pos to 7-bit signed
            const int pos = (mod_pos_ & 0x3f) - (mod_pos_ & 0x40);

            // multiply pos by gain, shift off 4 bits but with odd "rounding" behaviour
            int temp = pos * mod_env_out_;
            int rem = temp & 0x0f;
            temp >>= 4;
            if ((rem > 0) && ((temp & 0x80) == 0)) {
                if (pos < 0)
                    temp -= 1;
                else
                    temp += 2;
            }

            // wrap if range is exceeded
            while (temp >= 192) temp -= 256;
            while (temp < -64) temp += 256;

            // multiply result by pitch, shift off 6 bits, round to nearest
            temp = wave_freq_ * temp;
            rem = temp & 0x3f;
            temp >>= 6;
            if (rem >= 32) temp += 1;

            mod = temp;
        }

        // accumulate: input clock * (frequency + modulator output) / 65536
        wave_acc_ += static_cast<std::uint32_t>(std::max<int>(0, wave_freq_ + mod));
        wave_addr_ = static_cast<std::uint8_t>(wave_addr_ + (wave_acc_ >> 16));
        wave_acc_ &= 0xffff;
    }

    // calculate output
    if (!wave_write_) output_ = (wave_[wave_addr_ & 0x3f] - 0x20) * std::min<int>(32, vol_env_out_);
}
