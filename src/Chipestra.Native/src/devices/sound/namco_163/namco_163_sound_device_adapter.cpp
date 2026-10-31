// license:BSD-3-Clause
// copyright-holders:cam900
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/sound/namco_163.cpp  [copyright-holders:cam900]
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#include "devices/sound/namco_163/namco_163_sound_device_adapter.h"

Namco163SoundDeviceAdapter::Namco163SoundDeviceAdapter() { Reset(); }

void Namco163SoundDeviceAdapter::Reset() {
    ram_.fill(0);
    reg_addr_ = 0x78;
    addr_ = 0;
    inc_ = false;
    disabled_ = false;
    cycles_ = 0;
    samples_ = 0;
}

void Namco163SoundDeviceAdapter::WriteAddress(std::uint8_t value) {
    inc_ = (value & 0x80) != 0;
    addr_ = value & 0x7F;
}

void Namco163SoundDeviceAdapter::WriteData(std::uint8_t value) {
    ram_[addr_] = value;
    if (inc_) addr_ = static_cast<std::uint8_t>((addr_ + 1) & 0x7F);
}

std::uint8_t Namco163SoundDeviceAdapter::ReadData() {
    const std::uint8_t value = ram_[addr_];
    if (inc_) addr_ = static_cast<std::uint8_t>((addr_ + 1) & 0x7F);
    return value;
}

std::size_t Namco163SoundDeviceAdapter::Render(std::uint64_t cpuCycles, std::array<std::vector<float>, kChannelCount>& channelOut) {
    cycles_ += cpuCycles;
    // The first sample at or after the current time: ceil(cycles * 119318 / 1789772).
    const std::uint64_t due = (cycles_ * kOutputSampleRateHz + kCpuClockHz - 1) / kCpuClockHz;
    std::size_t made = 0;
    while (samples_ < due) {
        ++samples_;
        ++made;
        std::size_t channel = kChannelCount;
        float value = 0.0f;
        if (!disabled_) {
            const std::uint8_t* r = &ram_[reg_addr_];
            std::uint32_t phase = (static_cast<std::uint32_t>(r[5]) << 16) | (static_cast<std::uint32_t>(r[3]) << 8) | r[1];
            const std::uint32_t freq = ((static_cast<std::uint32_t>(r[4]) & 0x3) << 16) | (static_cast<std::uint32_t>(r[2]) << 8) | r[0];
            const std::uint32_t length = 256u - (r[4] & 0xFCu);
            const std::uint32_t offset = r[6];
            const std::uint32_t vol = r[7] & 0xFu;
            phase = (phase + freq) % (length << 16);
            const std::uint32_t wave = (phase >> 16) + offset;
            const int sample = ((ram_[(wave >> 1) & 0x7F] >> ((wave & 1) << 2)) & 0xF) - 8;
            channel = (reg_addr_ - 0x40) / 8;
            if (!(muted_mask_ & (1u << channel))) value = static_cast<float>(sample * static_cast<int>(vol)) / 128.0f;
            ram_[reg_addr_ + 1] = static_cast<std::uint8_t>(phase & 0xFF);
            ram_[reg_addr_ + 3] = static_cast<std::uint8_t>((phase >> 8) & 0xFF);
            ram_[reg_addr_ + 5] = static_cast<std::uint8_t>((phase >> 16) & 0xFF);
            reg_addr_ = static_cast<std::uint8_t>(reg_addr_ + 8);
            if (reg_addr_ >= 0x80) reg_addr_ = static_cast<std::uint8_t>(0x78 - ((ram_[0x7F] & 0x70) >> 1));
        }
        for (std::size_t c = 0; c < kChannelCount; ++c) channelOut[c].push_back(c == channel ? value : 0.0f);
    }
    return made;
}
