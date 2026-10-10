// license:BSD-3-Clause
// copyright-holders:Couriersud
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/sound/ay8910.cpp
//   src/devices/sound/ay8910.h
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#include "ay8910_device_adapter.h"

#include <algorithm>
#include <utility>

namespace {
// AY-3-8910 envelope: 16 steps, pacing at half the tone/noise sample rate
// (MAME's set_type(): `m_step = 2` for PSG_TYPE_AY, 1 for YM2149).
constexpr std::uint32_t kEnvelopeStepMultiplier = 2;
constexpr std::uint8_t kEnvelopeStepMask = 0x0F;
}  // namespace

Ay8910DeviceAdapter::Ay8910DeviceAdapter(PortWriteFn portAWrite, PortWriteFn portBWrite, PortReadFn portARead,
                                          PortReadFn portBRead, float loadResistanceOhms)
    : port_a_write_(std::move(portAWrite)),
      port_b_write_(std::move(portBWrite)),
      port_a_read_(std::move(portARead)),
      port_b_read_(std::move(portBRead)),
      load_resistance_ohms_(loadResistanceOhms) {
    BuildVolumeTables();
    Reset();
}

void Ay8910DeviceAdapter::Reset() {
    active_ = false;
    register_latch_ = 0;
    rng_ = 1;
    noise_out_ = 0;
    for (Tone& tone : tone_) tone = Tone{};
    envelope_ = Envelope{};
    count_noise_ = 0;
    prescale_noise_ = 0;
    partial_input_clocks_ = 0;
    // As in ay8910_reset_ym(): makes the AY_ENABLE change detection in
    // WriteRegister() fire on the first write, even if that write is 0x00.
    last_enable_ = 0xC0;
    for (std::uint8_t reg = 0; reg < kRegPortA; ++reg) {
        WriteRegister(reg, 0);
    }
}

void Ay8910DeviceAdapter::WriteAddress(std::uint8_t value) {
    // 4-bit register address mask: any of the upper 4 bits set deactivates
    // the latch until a valid (0x00-0x0F) address is written again.
    active_ = (value >> 4) == 0;
    if (active_) {
        register_latch_ = value & 0x0F;
    }
}

void Ay8910DeviceAdapter::WriteData(std::uint8_t value) {
    if (active_) {
        WriteRegister(register_latch_, value);
    }
}

std::uint8_t Ay8910DeviceAdapter::ReadData() {
    if (!active_) return 0xFF;  // high impedance
    const std::uint8_t r = register_latch_;
    // Inaccessible bits in each register read back as 0 on real hardware.
    static constexpr std::uint8_t kReadMask[16] = {0xFF, 0x0F, 0xFF, 0x0F, 0xFF, 0x0F, 0x1F, 0xFF,
                                                     0x1F, 0x1F, 0x1F, 0xFF, 0xFF, 0x0F, 0xFF, 0xFF};
    // PORTA/PORTB reads pull a fresh value from the callback when one is set,
    // even if AY_ENABLE configures that port as output (as in MAME). Without
    // a callback the stored register byte is returned.
    if (r == kRegPortA && port_a_read_) {
        regs_[kRegPortA] = port_a_read_();
    } else if (r == kRegPortB && port_b_read_) {
        regs_[kRegPortB] = port_b_read_();
    }
    return static_cast<std::uint8_t>(regs_[r] & kReadMask[r]);
}

void Ay8910DeviceAdapter::WriteRegister(std::uint8_t reg, std::uint8_t value) {
    reg &= 0x0F;
    regs_[reg] = value;

    switch (reg) {
        case kRegAFine:
        case kRegACoarse:
            tone_[0].period = static_cast<std::uint32_t>(regs_[kRegAFine]) |
                               (static_cast<std::uint32_t>(regs_[kRegACoarse] & 0x0F) << 8);
            break;
        case kRegBFine:
        case kRegBCoarse:
            tone_[1].period = static_cast<std::uint32_t>(regs_[kRegBFine]) |
                               (static_cast<std::uint32_t>(regs_[kRegBCoarse] & 0x0F) << 8);
            break;
        case kRegCFine:
        case kRegCCoarse:
            tone_[2].period = static_cast<std::uint32_t>(regs_[kRegCFine]) |
                               (static_cast<std::uint32_t>(regs_[kRegCCoarse] & 0x0F) << 8);
            break;
        case kRegNoisePeriod:
            break;  // read on demand via NoisePeriod() - no derived state to update
        case kRegAVol:
            tone_[0].volume = regs_[kRegAVol];
            break;
        case kRegBVol:
            tone_[1].volume = regs_[kRegBVol];
            break;
        case kRegCVol:
            tone_[2].volume = regs_[kRegCVol];
            break;
        case kRegEnvFine:
        case kRegEnvCoarse:
            envelope_.SetPeriod(regs_[kRegEnvFine], regs_[kRegEnvCoarse]);
            break;
        case kRegEnable: {
            // Port direction bits (0x40 = A, 0x80 = B): a change fires the
            // write callback with the currently-latched port register value
            // (or 0xFF/high-impedance if the direction just became input).
            const std::uint8_t enableA = regs_[kRegEnable] & 0x40;
            if (enableA != (last_enable_ & 0x40) && port_a_write_) {
                port_a_write_(enableA ? regs_[kRegPortA] : 0xFF);
            }
            const std::uint8_t enableB = regs_[kRegEnable] & 0x80;
            if (enableB != (last_enable_ & 0x80) && port_b_write_) {
                port_b_write_(enableB ? regs_[kRegPortB] : 0xFF);
            }
            last_enable_ = regs_[kRegEnable];
            break;
        }
        case kRegEnvShape:
            envelope_.SetShape(regs_[kRegEnvShape]);
            break;
        case kRegPortA:
            if ((regs_[kRegEnable] & 0x40) && port_a_write_) {
                port_a_write_(regs_[kRegPortA]);
            }
            break;
        case kRegPortB:
            if ((regs_[kRegEnable] & 0x80) && port_b_write_) {
                port_b_write_(regs_[kRegPortB]);
            }
            break;
        default:
            break;
    }
}

std::uint8_t Ay8910DeviceAdapter::NoisePeriod() const {
    return regs_[kRegNoisePeriod] & 0x1F;
}

bool Ay8910DeviceAdapter::ToneEnabled(std::uint32_t channel) const {
    // AY_ENABLE is active-low (0 = channel enabled). A SET bit means the
    // tone generator is excluded from gating this channel's output (MAME's
    // tone_enable() naming).
    return (regs_[kRegEnable] & (1u << channel)) != 0;
}

bool Ay8910DeviceAdapter::NoiseEnabled(std::uint32_t channel) const {
    return (regs_[kRegEnable] & (1u << (3 + channel))) != 0;
}

void Ay8910DeviceAdapter::NoiseRngTick() {
    // 17-bit LFSR, feedback = bit0 XOR bit3.
    const std::uint32_t feedback = (rng_ & 1) ^ ((rng_ >> 3) & 1);
    rng_ = (rng_ >> 1) | (feedback << 16);
}

void Ay8910DeviceAdapter::TickOneSample() {
    for (Tone& tone : tone_) {
        const std::uint32_t period = std::max<std::uint32_t>(1, tone.period);
        tone.count += 1;
        while (static_cast<std::uint32_t>(tone.count) >= period) {
            tone.output ^= 1;
            tone.count -= static_cast<std::int32_t>(period);
        }
    }

    if (++count_noise_ >= NoisePeriod()) {
        count_noise_ = 0;
        prescale_noise_ ^= 1;
        if (!prescale_noise_) NoiseRngTick();
    }
    noise_out_ = static_cast<std::uint8_t>(rng_ & 1);

    for (std::uint32_t ch = 0; ch < kChannelCount; ++ch) {
        const bool toneBit = tone_[ch].output != 0;
        // (ToneOn | ToneDisable) & (NoiseOn | NoiseDisable): if both tone and
        // noise are disabled for a channel, its output is 1 (pass-through,
        // not silence), modulated only by the volume register.
        vol_enabled_[ch] = (toneBit || ToneEnabled(ch)) && ((noise_out_ != 0) || NoiseEnabled(ch));
    }

    // Single shared envelope generator.
    if (envelope_.holding == 0) {
        const std::uint32_t period = envelope_.period * kEnvelopeStepMultiplier;
        if (++envelope_.count >= static_cast<std::int32_t>(period)) {
            envelope_.count = 0;
            --envelope_.step;
            if (envelope_.step < 0) {
                if (envelope_.hold) {
                    if (envelope_.alternate) envelope_.attack ^= kEnvelopeStepMask;
                    envelope_.holding = 1;
                    envelope_.step = 0;
                } else {
                    if (envelope_.alternate && (envelope_.step & (kEnvelopeStepMask + 1))) {
                        envelope_.attack ^= kEnvelopeStepMask;
                    }
                    envelope_.step &= static_cast<std::int8_t>(kEnvelopeStepMask);
                }
            }
        }
    }
    envelope_.volume = static_cast<std::uint32_t>(static_cast<std::uint8_t>(envelope_.step)) ^ envelope_.attack;
}

void Ay8910DeviceAdapter::BuildVolumeTables() {
    // AY-3-8910 resistor-ladder DAC model (MAME's `ay8910_param` +
    // `build_single_table()`), scaled from oscilloscope measurements. kRUp/
    // kRDown are the chip-internal pull-up/pull-down resistances;
    // kResistors[level] is the per-volume-level channel resistance. One
    // shared table serves the fixed-volume and envelope cases for all channels.
    constexpr double kRUp = 800000.0;
    constexpr double kRDown = 8000000.0;
    const double kLoadResistance = static_cast<double>(load_resistance_ohms_);
    constexpr double kResistors[16] = {15950, 15350, 15090, 14760, 14275, 13620, 12890, 11370,
                                        10600, 8590,  7190,  5985,  4820,  3945,  3017,  2345};

    auto buildTable = [&](float* table) {
        for (int level = 0; level < 16; ++level) {
            double rt = 1.0 / kRDown + 1.0 / kLoadResistance;
            double rw = 1.0 / kResistors[level];
            rt += 1.0 / kResistors[level];
            if (level != 0) {  // zero_is_off: level 0 excludes the pull-up (chip "off")
                rw += 1.0 / kRUp;
                rt += 1.0 / kRUp;
            }
            table[level] = static_cast<float>(rw / rt);
        }
    };

    for (float(&channelTable)[16] : volume_table_) buildTable(channelTable);
    buildTable(envelope_table_);
}

std::size_t Ay8910DeviceAdapter::Render(std::uint64_t inputClocks, std::vector<std::vector<float>>& channelSamples) {
    partial_input_clocks_ += inputClocks;
    const std::uint64_t sampleCount = partial_input_clocks_ / kInputClocksPerSample;
    partial_input_clocks_ -= sampleCount * kInputClocksPerSample;

    for (std::uint64_t i = 0; i < sampleCount; ++i) {
        TickOneSample();
        if (legacy_normalized_mix_) {
            RenderLegacyNormalizedSample(channelSamples);
            continue;
        }
        for (std::uint32_t ch = 0; ch < kChannelCount; ++ch) {
            const Tone& tone = tone_[ch];
            const bool useEnvelope = ((tone.volume >> 4) & 1) != 0;  // volume register bit 4 (M)
            const float sample = useEnvelope
                                      ? envelope_table_[vol_enabled_[ch] ? envelope_.volume : 0]
                                      : volume_table_[ch][vol_enabled_[ch] ? (tone.volume & 0x0F) : 0];
            // The resistor-ladder model's level-0 ("off") voltage is a constant DC
            // offset that real hardware's output coupling removes; subtracting it
            // keeps idle channels at zero and changes no audible content.
            const float t0 = volume_table_[ch][0];
            channelSamples[ch].push_back(legacy_normalized_separate_
                                             ? 0.5f * (sample - t0) / (volume_table_[ch][15] - t0)
                                             : sample - t0);
        }
    }
    return static_cast<std::size_t>(sampleCount);
}

namespace {
// Same resistor-ladder constants as BuildVolumeTables(), for the shared-network
// formula of the legacy 3-channel mix (MAME's build_3D_table()).
constexpr double kMixRUp = 800000.0;
constexpr double kMixRDown = 8000000.0;
constexpr double kMixResistors[16] = {15950, 15350, 15090, 14760, 14275, 13620, 12890, 11370,
                                       10600, 8590,  7190,  5985,  4820,  3945,  3017,  2345};

double SharedNetworkVoltage(const int (&level)[3], const bool (&on)[3], double loadOhms) {
    double n = 0.0;
    for (int i = 0; i < 3; ++i) n += on[i] ? 1.0 : 0.0;
    double rt = n / kMixRUp + 3.0 / kMixRDown + 1.0 / loadOhms;
    double rw = n / kMixRUp;
    for (int i = 0; i < 3; ++i) {
        rw += 1.0 / kMixResistors[level[i]];
        rt += 1.0 / kMixResistors[level[i]];
    }
    return rw / rt;
}
}  // namespace

void Ay8910DeviceAdapter::RenderLegacyNormalizedSample(std::vector<std::vector<float>>& channelSamples) {
    const double load = static_cast<double>(load_resistance_ohms_);
    static const int kZeroLevel[3] = {0, 0, 0};
    static const bool kAllOff[3] = {false, false, false};
    static const int kMaxLevel[3] = {15, 15, 15};
    static const bool kAllOn[3] = {true, true, true};
    const double vMin = SharedNetworkVoltage(kZeroLevel, kAllOff, load);
    const double vMax = SharedNetworkVoltage(kMaxLevel, kAllOn, load);

    int level[3];
    bool on[3];
    double conductance[3];
    double conductanceSum = 0.0;
    for (std::uint32_t ch = 0; ch < kChannelCount; ++ch) {
        const Tone& tone = tone_[ch];
        const bool useEnvelope = ((tone.volume >> 4) & 1) != 0;
        level[ch] = vol_enabled_[ch] ? (useEnvelope ? envelope_.volume : (tone.volume & 0x0F)) : 0;
        on[ch] = level[ch] != 0 || useEnvelope;  // MAME: envelope channels always count as "on"
        conductance[ch] = on[ch] ? 1.0 / kMixResistors[level[ch]] : 0.0;
        conductanceSum += conductance[ch];
    }
    const double total = 3.0 * (SharedNetworkVoltage(level, on, load) - vMin) / (vMax - vMin);
    for (std::uint32_t ch = 0; ch < kChannelCount; ++ch) {
        channelSamples[ch].push_back(conductanceSum > 0.0 ? static_cast<float>(total * conductance[ch] / conductanceSum) : 0.0f);
    }
}
