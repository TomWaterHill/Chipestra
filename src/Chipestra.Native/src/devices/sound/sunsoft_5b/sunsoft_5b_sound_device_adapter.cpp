// license:BSD-3-Clause
// copyright-holders:Couriersud
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/sound/ay8910.cpp  [copyright-holders:Couriersud]
//   src/devices/sound/ay8910.h  [copyright-holders:Couriersud]
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#include "devices/sound/sunsoft_5b/sunsoft_5b_sound_device_adapter.h"

#include <algorithm>

namespace {

// MAME's ym2149_param (fixed volume, 16 levels) and ym2149_param_env (envelope, 32 levels): the pull-up and
// pull-down resistances and the resistance of each level, measured on a YM2149.
struct Param {
    double rUp, rDown;
    int count;
    double res[32];
};
const Param kYm = {630, 801, 16, {73770, 37586, 27458, 21451, 15864, 12371, 8922, 6796, 4763, 3521, 2403, 1737, 1123, 762, 438, 251}};
const Param kYmEnv = {630, 801, 32, {103350, 73770, 52657, 37586, 32125, 27458, 24269, 21451, 18447, 15864, 14009, 12371, 10506, 8922, 7787, 6796,
                                      5689, 4763, 4095, 3521, 2909, 2403, 2043, 1737, 1397, 1123, 925, 762, 578, 438, 332, 251}};

constexpr double kLoadOhms = 1000.0;  // MAME's default resistor load
constexpr double kFactor = 3.0;       // build_3D_table()'s factor for a normalized single output
constexpr std::uint8_t kStepMask = 0x1F;

// The voltage of the shared resistor network (MAME's build_3D_table() before the normalization). The chip is not a
// "zero is off" type, so all three pull-ups always count.
double Voltage(const int (&level)[3], const bool (&envelope)[3]) {
    const Param* p[3] = {envelope[0] ? &kYmEnv : &kYm, envelope[1] ? &kYmEnv : &kYm, envelope[2] ? &kYmEnv : &kYm};
    const double n = 3.0;
    double rt = n / kYm.rUp + 3.0 / kYm.rDown + 1.0 / kLoadOhms;
    double rw = n / kYm.rUp;
    for (int i = 0; i < 3; ++i) {
        rw += 1.0 / p[i]->res[level[i]];
        rt += 1.0 / p[i]->res[level[i]];
    }
    return rw / rt;
}

struct Range {
    double min = 10.0, max = 0.0;
};
// The smallest and largest voltage over the whole table, as MAME's table build finds them.
const Range& TableRange() {
    static const Range range = [] {
        Range r;
        for (int e = 0; e < 8; ++e) {
            const bool env[3] = {(e & 1) != 0, (e & 2) != 0, (e & 4) != 0};
            const int n[3] = {env[0] ? 32 : 16, env[1] ? 32 : 16, env[2] ? 32 : 16};
            for (int a = 0; a < n[0]; ++a)
                for (int b = 0; b < n[1]; ++b)
                    for (int c = 0; c < n[2]; ++c) {
                        const int level[3] = {a, b, c};
                        const double v = Voltage(level, env);
                        r.min = std::min(r.min, v);
                        r.max = std::max(r.max, v);
                    }
        }
        return r;
    }();
    return range;
}

}  // namespace

Sunsoft5bSoundDeviceAdapter::Sunsoft5bSoundDeviceAdapter() { Reset(); }

void Sunsoft5bSoundDeviceAdapter::Reset() {
    active_ = false;
    register_latch_ = 0;
    rng_ = 1;
    for (Tone& tone : tone_) tone = Tone{};
    envelope_ = Envelope{};
    count_noise_ = 0;
    prescale_noise_ = 0;
    cycles_ = 0;
    samples_ = 0;
    for (std::uint8_t reg = 0; reg < 14; ++reg) WriteRegister(reg, 0);
}

void Sunsoft5bSoundDeviceAdapter::WriteAddress(std::uint8_t value) {
    active_ = true;  // the board gives the chip the low four bits only, so the upper address bits always match
    register_latch_ = value & 15;
}

void Sunsoft5bSoundDeviceAdapter::WriteData(std::uint8_t value) {
    if (active_) WriteRegister(register_latch_, value);
}

void Sunsoft5bSoundDeviceAdapter::WriteRegister(std::uint8_t reg, std::uint8_t value) {
    reg &= 15;
    regs_[reg] = value;
    switch (reg) {
    case 0: case 1: tone_[0].period = regs_[0] | (static_cast<std::uint32_t>(regs_[1] & 15) << 8); break;
    case 2: case 3: tone_[1].period = regs_[2] | (static_cast<std::uint32_t>(regs_[3] & 15) << 8); break;
    case 4: case 5: tone_[2].period = regs_[4] | (static_cast<std::uint32_t>(regs_[5] & 15) << 8); break;
    case 8: tone_[0].volume = value; break;
    case 9: tone_[1].volume = value; break;
    case 10: tone_[2].volume = value; break;
    case 11: case 12: envelope_.period = regs_[11] | (static_cast<std::uint32_t>(regs_[12]) << 8); break;
    case 13: {
        // envelope_t::set_shape()
        envelope_.attack = (value & 0x04) ? kStepMask : 0x00;
        if ((value & 0x08) == 0) {
            envelope_.hold = 1;
            envelope_.alternate = envelope_.attack;
        } else {
            envelope_.hold = value & 0x01;
            envelope_.alternate = value & 0x02;
        }
        envelope_.step = static_cast<std::int8_t>(kStepMask);
        envelope_.holding = 0;
        envelope_.volume = static_cast<std::uint32_t>(static_cast<std::uint8_t>(envelope_.step)) ^ envelope_.attack;
        break;
    }
    default: break;  // noise period and enable are read when needed; the I/O ports do not exist on this board
    }
}

void Sunsoft5bSoundDeviceAdapter::TickOneSample() {
    for (Tone& tone : tone_) {
        const std::int32_t period = std::max<std::int32_t>(1, static_cast<std::int32_t>(tone.period));
        tone.count += 1;
        while (tone.count >= period) {
            tone.output ^= 1;
            tone.count -= period;
        }
    }

    if (++count_noise_ >= static_cast<std::int32_t>(regs_[6] & 0x1F)) {
        count_noise_ = 0;
        prescale_noise_ ^= 1;
        if (!prescale_noise_) rng_ = (rng_ >> 1) | ((((rng_ >> 0) & 1) ^ ((rng_ >> 3) & 1)) << 16);  // 17-bit LFSR, bit 0 xor bit 3
    }

    for (std::size_t ch = 0; ch < kChannelCount; ++ch) {
        const bool tone = (tone_[ch].output | ((regs_[7] >> ch) & 1)) != 0;
        const bool noise = ((rng_ & 1) | ((regs_[7] >> (3 + ch)) & 1)) != 0;
        vol_enabled_[ch] = tone && noise;
    }

    if (envelope_.holding == 0) {
        const std::uint32_t period = envelope_.period;  // the YM envelope steps once per period
        if (static_cast<std::uint32_t>(++envelope_.count) >= period) {
            envelope_.count = 0;
            --envelope_.step;
            if (envelope_.step < 0) {
                if (envelope_.hold) {
                    if (envelope_.alternate) envelope_.attack ^= kStepMask;
                    envelope_.holding = 1;
                    envelope_.step = 0;
                } else {
                    if (envelope_.alternate && (envelope_.step & (kStepMask + 1))) envelope_.attack ^= kStepMask;
                    envelope_.step &= static_cast<std::int8_t>(kStepMask);
                }
            }
        }
    }
    envelope_.volume = static_cast<std::uint32_t>(static_cast<std::uint8_t>(envelope_.step)) ^ envelope_.attack;
}

float Sunsoft5bSoundDeviceAdapter::MixedOutput(const int (&level)[3], const bool (&envelope)[3]) {
    const Range& range = TableRange();
    return static_cast<float>(1.0 * ((Voltage(level, envelope) - range.min) / (range.max - range.min)) * kFactor);
}

void Sunsoft5bSoundDeviceAdapter::MakeSample(std::array<std::vector<float>, kChannelCount>& channelOut) {
    int level[3];
    int muted_level[3];
    bool envelope[3];
    bool muted_envelope[3];
    for (std::size_t ch = 0; ch < kChannelCount; ++ch) {
        envelope[ch] = ((tone_[ch].volume >> 4) & 1) != 0;
        level[ch] = envelope[ch] ? (vol_enabled_[ch] ? static_cast<int>(envelope_.volume) : 0) : (vol_enabled_[ch] ? (tone_[ch].volume & 15) : 0);
        const bool muted = (muted_mask_ >> ch) & 1;
        muted_level[ch] = muted ? 0 : level[ch];
        muted_envelope[ch] = muted ? false : envelope[ch];
    }
    // A channel's share of the output follows its conductance in the network.
    auto split = [&](const int (&lv)[3], const bool (&env)[3], float total, float (&share)[3]) {
        double g[3];
        double sum = 0.0;
        for (int i = 0; i < 3; ++i) {
            g[i] = 1.0 / (env[i] ? kYmEnv : kYm).res[lv[i]];
            sum += g[i];
        }
        for (int i = 0; i < 2; ++i) share[i] = static_cast<float>(total * g[i] / sum);
        share[2] = total - share[0] - share[1];  // the last channel takes the rest, so the three add up to the output
    };
    float full[3];
    split(level, envelope, MixedOutput(level, envelope), full);
    if (muted_mask_ == 0) {
        for (std::size_t ch = 0; ch < kChannelCount; ++ch) channelOut[ch].push_back(full[ch]);
        return;
    }
    float without[3];
    split(muted_level, muted_envelope, MixedOutput(muted_level, muted_envelope), without);
    for (std::size_t ch = 0; ch < kChannelCount; ++ch) channelOut[ch].push_back(((muted_mask_ >> ch) & 1) ? full[ch] : without[ch]);
}

std::size_t Sunsoft5bSoundDeviceAdapter::Render(std::uint64_t cpuCycles, std::array<std::vector<float>, kChannelCount>& channelOut) {
    cycles_ += cpuCycles;
    // The samples before the current cycle: ceil(cycles / 16).
    return RenderToSampleCount((cycles_ + kCpuCyclesPerSample - 1) / kCpuCyclesPerSample, channelOut);
}

std::size_t Sunsoft5bSoundDeviceAdapter::RenderToSampleCount(std::uint64_t totalSamples, std::array<std::vector<float>, kChannelCount>& channelOut) {
    std::size_t made = 0;
    while (samples_ < totalSamples) {
        ++samples_;
        ++made;
        TickOneSample();
        MakeSample(channelOut);
    }
    return made;
}
