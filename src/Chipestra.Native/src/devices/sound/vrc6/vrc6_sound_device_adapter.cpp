// license:BSD-3-Clause
// copyright-holders:R. Belmont
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/sound/vrc6.cpp  [copyright-holders:R. Belmont]
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#include "devices/sound/vrc6/vrc6_sound_device_adapter.h"

Vrc6SoundDeviceAdapter::Vrc6SoundDeviceAdapter() { Reset(); }

void Vrc6SoundDeviceAdapter::Reset() {
    freq_ctrl_ = 0;
    pulse_ctrl_[0] = pulse_ctrl_[1] = 0;
    pulse_frql_[0] = pulse_frql_[1] = 0;
    pulse_frqh_[0] = pulse_frqh_[1] = 0;
    saw_accum_ = 0;
    saw_frql_ = 0;
    saw_frqh_ = 0;
    saw_clock_ = 0;
    saw_rate_ = 0;
    ticks_[0] = ticks_[1] = ticks_[2] = 0;
    output_[0] = output_[1] = output_[2] = 0;
    pulse_duty_[0] = pulse_duty_[1] = 15;
    master_freq_ = 0;
}

int Vrc6SoundDeviceAdapter::PulsePeriod(std::size_t pulse) const {
    return (pulse_frql_[pulse] | ((pulse_frqh_[pulse] & 0xf) << 8)) >> master_freq_;
}

int Vrc6SoundDeviceAdapter::SawPeriod() const { return (saw_frql_ | ((saw_frqh_ & 0xf) << 8)) >> master_freq_; }

void Vrc6SoundDeviceAdapter::Write(unsigned offset, std::uint8_t data) {
    switch (offset >> 8) {
        case 0:
            switch (offset & 3) {
                case 0:
                    pulse_ctrl_[0] = data;
                    break;
                case 1:
                    pulse_frql_[0] = data;
                    if (!(pulse_frqh_[0] & 0x80)) ticks_[0] = PulsePeriod(0);
                    break;
                case 2:
                    pulse_frqh_[0] = data;
                    // if disabling channel, reset phase
                    if (!(data & 0x80)) {
                        pulse_duty_[0] = 15;
                        ticks_[0] = PulsePeriod(0);
                    }
                    break;
                case 3:
                    freq_ctrl_ = data;
                    if (freq_ctrl_ & 4) master_freq_ = 0x8;
                    else if (freq_ctrl_ & 2) master_freq_ = 0x4;
                    else master_freq_ = 0;
                    if (!(pulse_frqh_[0] & 0x80)) ticks_[0] = PulsePeriod(0);
                    if (!(pulse_frqh_[1] & 0x80)) ticks_[1] = PulsePeriod(1);
                    if (!(saw_frqh_ & 0x80)) ticks_[2] = SawPeriod();
                    break;
            }
            break;
        case 1:
            switch (offset & 3) {
                case 0:
                    pulse_ctrl_[1] = data;
                    break;
                case 1:
                    pulse_frql_[1] = data;
                    if (!(pulse_frqh_[1] & 0x80)) ticks_[1] = PulsePeriod(1);
                    break;
                case 2:
                    pulse_frqh_[1] = data;
                    if (!(data & 0x80)) {
                        pulse_duty_[1] = 15;
                        ticks_[1] = PulsePeriod(1);
                    }
                    break;
                default:
                    break;
            }
            break;
        case 2:
            switch (offset & 3) {
                case 0:
                    saw_rate_ = data;
                    break;
                case 1:
                    saw_frql_ = data;
                    if (!(saw_frqh_ & 0x80)) ticks_[2] = SawPeriod();
                    break;
                case 2:
                    saw_frqh_ = data;
                    if (!(data & 0x80)) {
                        saw_accum_ = 0;
                        ticks_[2] = SawPeriod();
                    }
                    break;
                default:
                    break;
            }
            break;
        default:
            break;
    }
}

void Vrc6SoundDeviceAdapter::WriteIo(std::uint16_t address, std::uint8_t value) {
    const unsigned reg = address & 3;
    switch (address & 0xF000) {
        case 0x9000:
            Write(reg, value);
            break;
        case 0xA000:
            if (reg < 3) Write(0x100 | reg, value);
            break;
        case 0xB000:
            if (reg < 3) Write(0x200 | reg, value);
            break;
        default:
            break;
    }
}

void Vrc6SoundDeviceAdapter::TickPulse(std::size_t c) {
    if (pulse_frqh_[c] & 0x80) {
        if (ticks_[c] == 0) {
            ticks_[c] = PulsePeriod(c);
            pulse_duty_[c] = (pulse_duty_[c] - 1) & 0xf;
            if (pulse_ctrl_[c] & 0x80) {
                output_[c] = pulse_ctrl_[c] & 0xf;
            } else if (pulse_duty_[c] <= ((pulse_ctrl_[c] >> 4) & 0x7)) {
                output_[c] = pulse_ctrl_[c] & 0xf;
            } else {
                output_[c] = 0;
            }
        } else {
            --ticks_[c];
        }
    } else {
        output_[c] = 0;
    }
}

void Vrc6SoundDeviceAdapter::TickSaw() {
    if (saw_frqh_ & 0x80) {
        if (ticks_[2] == 0) {
            ticks_[2] = SawPeriod();
            // only update on even steps
            if (saw_clock_ > 0 && !(saw_clock_ & 1)) {
                saw_accum_ = static_cast<std::uint8_t>(saw_accum_ + (saw_rate_ & 0x3f));
                output_[2] = saw_accum_ >> 3;
            }
            ++saw_clock_;
            if (saw_clock_ >= 14) {
                saw_clock_ = 0;
                saw_accum_ = 0;
                output_[2] = 0;
            }
        } else {
            --ticks_[2];
        }
    } else {
        output_[2] = 0;
    }
}

std::size_t Vrc6SoundDeviceAdapter::Render(std::uint64_t cpuCycles, std::array<std::vector<float>, kChannelCount>& channelOut) {
    for (std::uint64_t i = 0; i < cpuCycles; ++i) {
        if (freq_ctrl_ & 1) {  // global halt: the original leaves its stream untouched (silence)
            for (auto& v : channelOut) v.push_back(0.0f);
            continue;
        }
        TickPulse(0);
        TickPulse(1);
        TickSaw();
        for (std::size_t c = 0; c < kChannelCount; ++c) {
            const bool muted = ((muted_mask_ >> c) & 1) != 0;
            channelOut[c].push_back(muted ? 0.0f : static_cast<float>(output_[c]) / 128.0f);
        }
    }
    return static_cast<std::size_t>(cpuCycles);
}
