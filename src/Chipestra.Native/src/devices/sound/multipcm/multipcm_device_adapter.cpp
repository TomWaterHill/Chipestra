// license:BSD-3-Clause
// copyright-holders:Miguel Angel Horna
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/sound/multipcm.cpp
//   src/devices/sound/gew.cpp
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#include "devices/sound/multipcm/multipcm_device_adapter.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr std::uint32_t TL_SHIFT = 12;
constexpr std::uint32_t EG_SHIFT = 16;
constexpr std::uint32_t LFO_SHIFT = 8;

// Times are based on a 44100Hz timebase. It is adjusted to the actual sampling rate on startup.
const double BASE_TIMES[64] = {
    0,       0,       0,       0,
    6222.95, 4978.37, 4148.66, 3556.01,
    3111.47, 2489.21, 2074.33, 1778.00,
    1555.74, 1244.63, 1037.19, 889.02,
    777.87,  622.31,  518.59,  444.54,
    388.93,  311.16,  259.32,  222.27,
    194.47,  155.60,  129.66,  111.16,
    97.23,   77.82,   64.85,   55.60,
    48.62,   38.91,   32.43,   27.80,
    24.31,   19.46,   16.24,   13.92,
    12.15,   9.75,    8.12,    6.98,
    6.08,    4.90,    4.08,    3.49,
    3.04,    2.49,    2.13,    1.90,
    1.72,    1.41,    1.18,    1.04,
    0.91,    0.73,    0.59,    0.50,
    0.45,    0.45,    0.45,    0.45};

const float LFO_FREQ[8] = {0.168f, 2.019f, 3.196f, 4.206f, 5.215f, 5.888f, 6.224f, 7.066f};              // in Hertz
const float PHASE_SCALE_LIMIT[8] = {0.0f, 3.378f, 5.065f, 6.750f, 10.114f, 20.170f, 40.180f, 79.307f};  // in cents
const float AMPLITUDE_SCALE_LIMIT[8] = {0.0f, 0.4f, 0.8f, 1.5f, 3.0f, 6.0f, 12.0f, 24.0f};              // in decibels

const std::int32_t VALUE_TO_CHANNEL[32] = {
    0,  1,  2,  3,  4,  5,  6,  -1,
    7,  8,  9,  10, 11, 12, 13, -1,
    14, 15, 16, 17, 18, 19, 20, -1,
    21, 22, 23, 24, 25, 26, 27, -1,
};

std::uint32_t ValueToFixed(std::uint32_t bits, float value) {
    const float floatShift = static_cast<float>(1 << bits);
    return static_cast<std::uint32_t>(floatShift * value);
}

enum class EnvState : std::uint8_t { Attack, Decay1, Decay2, Release };

struct Sample {
    std::uint32_t start = 0, loop = 0, end = 0;
    std::uint8_t attackReg = 0, decay1Reg = 0, decay2Reg = 0, decayLevel = 0, releaseReg = 0, keyRateScale = 0;
    std::uint8_t lfoVibratoReg = 0, lfoAmplitudeReg = 0, format = 0;
};

struct Envelope {
    std::int32_t volume = 0;
    EnvState state = EnvState::Attack;
    std::uint8_t reverb = 0;
    std::int32_t attackRate = 0, decay1Rate = 0, decay2Rate = 0, releaseRate = 0, decayLevel = 0;
};

// The tables an LFO reads are fixed; the LFO keeps which pair it uses (0 = none, 1 = pitch, 2 = amplitude, and the scale).
struct Lfo {
    std::uint16_t phase = 0;
    std::uint32_t phaseStep = 0;
    int kind = 0;
    int scale = 0;
};

struct Slot {
    std::uint8_t regs[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    bool playing = false;
    Sample sample;
    std::uint32_t offset = 0;
    std::uint8_t octave = 0;
    std::uint16_t pitch = 0;
    std::uint32_t step = 0;
    bool reverse = false;
    std::uint32_t pan = 0;
    std::uint32_t totalLevel = 0;
    std::uint32_t destTotalLevel = 0;
    std::int32_t totalLevelStep = 0;
    std::int32_t prevSample = 0;
    Envelope env;
    std::uint8_t lfoFrequency = 0;
    Lfo pitchLfo;
    std::uint8_t vibrato = 0;
    Lfo amplitudeLfo;
    std::uint8_t tremolo = 0;
    float gainLeft = 0.0f, gainRight = 0.0f;  // the last rendered sample's pan and volume factors
};

}  // namespace

struct MultiPcmDeviceAdapter::SavedState {
    std::vector<Slot> slots;
    std::uint32_t curSlot = 0, address = 0;
    std::uint64_t partialClocks = 0, generatedSamples = 0;
};

struct MultiPcmDeviceAdapter::Impl {
    SampleReadFn read;
    float rate;
    std::vector<Slot> slots;
    std::uint32_t curSlot = 0, address = 0;
    std::uint64_t partialClocks = 0, generatedSamples = 0;

    std::vector<std::uint32_t> attackStep, decayReleaseStep, freqStepTable;
    std::vector<std::int32_t> leftPanTable, rightPanTable, linearToExpVolume;
    std::int32_t totalLevelSteps[2] = {0, 0};
    std::vector<std::int32_t> pitchTable, amplitudeTable;
    std::vector<std::int32_t> pitchScaleTables[8], amplitudeScaleTables[8];

    Impl(std::uint32_t clockHz, SampleReadFn r) : read(std::move(r)) {
        rate = static_cast<float>(clockHz) / static_cast<float>(MultiPcmDeviceAdapter::kClockDivider);
        BuildTables();
        slots.assign(MultiPcmDeviceAdapter::kVoiceCount, Slot{});
    }

    std::uint8_t Byte(std::uint32_t addr) const { return read ? read(addr) : std::uint8_t{0}; }

    void BuildTables() {
        // Volume + pan table
        leftPanTable.assign(0x800, 0);
        rightPanTable.assign(0x800, 0);
        for (std::int32_t level = 0; level < 0x80; ++level) {
            const float volDb = static_cast<float>(level) * (-24.0f) / 64.0f;
            const float totalLevel = powf(10.0f, volDb / 20.0f) / 4.0f;
            for (std::int32_t pan = 0; pan < 0x10; ++pan) {
                float panLeft, panRight;
                if (pan == 0x8) {
                    panLeft = 0.0f;
                    panRight = 0.0f;
                } else if (pan == 0x0) {
                    panLeft = 1.0f;
                    panRight = 1.0f;
                } else if (pan & 0x8) {
                    panLeft = 1.0f;
                    const std::int32_t invertedPan = 0x10 - pan;
                    const float panVolDb = static_cast<float>(invertedPan) * (-12.0f) / 4.0f;
                    panRight = static_cast<float>(pow(10.0f, panVolDb / 20.0f));
                    if ((invertedPan & 0x7) == 7) {
                        panRight = 0.0f;
                    }
                } else {
                    panRight = 1.0f;
                    const float panVolDb = static_cast<float>(pan) * (-12.0f) / 4.0f;
                    panLeft = static_cast<float>(pow(10.0f, panVolDb / 20.0f));
                    if ((pan & 0x7) == 7) {
                        panLeft = 0.0f;
                    }
                }
                leftPanTable[(pan << 7) | level] = static_cast<std::int32_t>(ValueToFixed(TL_SHIFT, panLeft * totalLevel));
                rightPanTable[(pan << 7) | level] = static_cast<std::int32_t>(ValueToFixed(TL_SHIFT, panRight * totalLevel));
            }
        }

        // Pitch steps
        freqStepTable.assign(0x400, 0);
        for (std::int32_t i = 0; i < 0x400; ++i) {
            const float fcent = rate * (1024.0f + static_cast<float>(i)) / 1024.0f;
            freqStepTable[i] = ValueToFixed(TL_SHIFT, fcent);
        }

        // Envelope steps (the times are in 44100Hz units)
        attackStep.assign(0x40, 0);
        decayReleaseStep.assign(0x40, 0);
        const double attackDecayRatio = 14.32833;
        for (std::int32_t i = 4; i < 0x40; ++i) {
            attackStep[i] = static_cast<std::uint32_t>(static_cast<float>(0x400 << EG_SHIFT) /
                                                       static_cast<float>(BASE_TIMES[i] * 44100.0 / 1000.0));
            decayReleaseStep[i] = static_cast<std::uint32_t>(
                static_cast<float>(0x400 << EG_SHIFT) / static_cast<float>(BASE_TIMES[i] * attackDecayRatio * 44100.0 / 1000.0));
        }
        attackStep[0] = attackStep[1] = attackStep[2] = attackStep[3] = 0;
        attackStep[0x3f] = 0x400 << EG_SHIFT;
        decayReleaseStep[0] = decayReleaseStep[1] = decayReleaseStep[2] = decayReleaseStep[3] = 0;

        // Total level interpolation steps
        totalLevelSteps[0] = static_cast<std::int32_t>(-static_cast<float>(0x80 << TL_SHIFT) / (78.2f * 44100.0f / 1000.0f));
        totalLevelSteps[1] = static_cast<std::int32_t>(static_cast<float>(0x80 << TL_SHIFT) / (78.2f * 2 * 44100.0f / 1000.0f));

        // The linear->exponential ramp
        linearToExpVolume.assign(0x400, 0);
        for (std::int32_t i = 0; i < 0x400; ++i) {
            const float db = -(96.0f - (96.0f * static_cast<float>(i) / static_cast<float>(0x400)));
            const float expVolume = powf(10.0f, db / 20.0f);
            linearToExpVolume[i] = static_cast<std::int32_t>(ValueToFixed(TL_SHIFT, expVolume));
        }

        // LFO tables
        pitchTable.assign(256, 0);
        amplitudeTable.assign(256, 0);
        for (std::int32_t i = 0; i < 256; ++i) {
            if (i < 64) {
                pitchTable[i] = i * 2 + 128;
            } else if (i < 128) {
                pitchTable[i] = 383 - i * 2;
            } else if (i < 192) {
                pitchTable[i] = 384 - i * 2;
            } else {
                pitchTable[i] = i * 2 - 383;
            }
            if (i < 128) {
                amplitudeTable[i] = 255 - (i * 2);
            } else {
                amplitudeTable[i] = (i * 2) - 256;
            }
        }
        for (std::int32_t table = 0; table < 8; ++table) {
            float limit = PHASE_SCALE_LIMIT[table];
            pitchScaleTables[table].assign(256, 0);
            for (std::int32_t i = -128; i < 128; ++i) {
                const float value = (limit * static_cast<float>(i)) / 128.0f;
                const float converted = powf(2.0f, value / 1200.0f);
                pitchScaleTables[table][i + 128] = static_cast<std::int32_t>(ValueToFixed(LFO_SHIFT, converted));
            }
            limit = -AMPLITUDE_SCALE_LIMIT[table];
            amplitudeScaleTables[table].assign(256, 0);
            for (std::int32_t i = 0; i < 256; ++i) {
                const float value = (limit * static_cast<float>(i)) / 256.0f;
                const float converted = powf(10.0f, value / 20.0f);
                amplitudeScaleTables[table][i] = static_cast<std::int32_t>(ValueToFixed(LFO_SHIFT, converted));
            }
        }
    }

    // ---- envelope ----
    std::uint32_t GetRate(const std::vector<std::uint32_t>& steps, std::int32_t rateValue, std::uint32_t val) const {
        if (val == 0) return steps[0];
        if (val == 0xf) return steps[0x3f];
        const int r = std::clamp(4 * static_cast<int>(val) + rateValue, 0, 0x3f);
        return steps[r];
    }

    void EnvelopeCalc(Slot& slot) const {
        std::int32_t octave = slot.octave;
        if (octave & 8) octave = octave - 16;
        std::int32_t r;
        if (slot.sample.keyRateScale != 0xf) {
            r = (octave + slot.sample.keyRateScale) * 2 + ((slot.pitch >> 9) & 1);
        } else {
            r = 0;
        }
        slot.env.attackRate = static_cast<std::int32_t>(GetRate(attackStep, r, slot.sample.attackReg));
        slot.env.decay1Rate = static_cast<std::int32_t>(GetRate(decayReleaseStep, r, slot.sample.decay1Reg));
        slot.env.decay2Rate = static_cast<std::int32_t>(GetRate(decayReleaseStep, r, slot.sample.decay2Reg));
        slot.env.releaseRate = static_cast<std::int32_t>(GetRate(decayReleaseStep, r, slot.sample.releaseReg));
        slot.env.decayLevel = 0xf - slot.sample.decayLevel;
        slot.env.reverb = 0;
    }

    std::int32_t EnvelopeUpdate(Slot& slot) const {
        Envelope& e = slot.env;
        switch (e.state) {
            case EnvState::Attack:
                e.volume += e.attackRate;
                if (e.volume >= (0x3ff << EG_SHIFT)) {
                    e.state = EnvState::Decay1;
                    if (e.decay1Rate >= (0x400 << EG_SHIFT)) {  // skip DECAY1, go directly to DECAY2
                        e.state = EnvState::Decay2;
                    }
                    e.volume = 0x3ff << EG_SHIFT;
                }
                break;
            case EnvState::Decay1:
                e.volume -= e.decay1Rate;
                if (e.volume <= 0) e.volume = 0;
                if (e.volume >> (EG_SHIFT + 6) <= e.decayLevel) e.state = EnvState::Decay2;
                break;
            case EnvState::Decay2:
                e.volume -= e.decay2Rate;
                if (e.volume <= 0) e.volume = 0;
                break;
            case EnvState::Release:
                e.volume -= e.releaseRate;
                if (e.volume <= 0) {
                    e.volume = 0;
                    slot.playing = false;
                }
                break;
            default:
                return 1 << TL_SHIFT;
        }
        return linearToExpVolume[e.volume >> EG_SHIFT];
    }

    // ---- LFO ----
    void LfoComputeStep(Lfo& lfo, std::uint32_t lfoFrequency, std::uint32_t lfoScale, bool amplitude) const {
        const float step = static_cast<float>(LFO_FREQ[lfoFrequency]) * 256.0f / rate;
        lfo.phaseStep = static_cast<std::uint32_t>(static_cast<float>(1 << LFO_SHIFT) * step);
        lfo.kind = amplitude ? 2 : 1;
        lfo.scale = static_cast<int>(lfoScale);
    }

    std::int32_t LfoStep(Lfo& lfo) const {
        lfo.phase = static_cast<std::uint16_t>(lfo.phase + lfo.phaseStep);
        const std::vector<std::int32_t>& table = (lfo.kind == 2) ? amplitudeTable : pitchTable;
        std::int32_t p = table[(lfo.phase >> LFO_SHIFT) & 0xff];
        const std::vector<std::int32_t>& scale = (lfo.kind == 2) ? amplitudeScaleTables[lfo.scale] : pitchScaleTables[lfo.scale];
        p = scale[p];
        return p << (TL_SHIFT - LFO_SHIFT);
    }

    // ---- voices ----
    void RetriggerSample(Slot& slot) const {
        slot.offset = 0;
        slot.prevSample = 0;
        slot.totalLevel = slot.destTotalLevel << TL_SHIFT;
        EnvelopeCalc(slot);
        slot.env.state = EnvState::Attack;
        slot.env.volume = 0;
    }

    void UpdateStep(Slot& slot) const {
        const std::uint8_t oct = static_cast<std::uint8_t>((slot.octave - 1) & 0xf);
        std::uint32_t pitch = freqStepTable[slot.pitch];
        if (oct & 0x8) {
            pitch >>= (16 - oct);
        } else {
            pitch <<= oct;
        }
        slot.step = static_cast<std::uint32_t>(pitch / rate);
    }

    void InitSample(Sample& s, std::uint32_t index) const {
        const std::uint32_t base = index * 12;
        s.start = (static_cast<std::uint32_t>(Byte(base)) << 16) | (Byte(base + 1) << 8) | Byte(base + 2);
        s.format = static_cast<std::uint8_t>((s.start >> 20) & 0xfe);
        s.start &= 0x3fffff;
        s.loop = (Byte(base + 3) << 8) | Byte(base + 4);
        s.end = 0x10000 - ((Byte(base + 5) << 8) | Byte(base + 6));
        s.attackReg = (Byte(base + 8) >> 4) & 0xf;
        s.decay1Reg = Byte(base + 8) & 0xf;
        s.decay2Reg = Byte(base + 9) & 0xf;
        s.decayLevel = (Byte(base + 9) >> 4) & 0xf;
        s.releaseReg = Byte(base + 10) & 0xf;
        s.keyRateScale = (Byte(base + 10) >> 4) & 0xf;
        s.lfoVibratoReg = Byte(base + 7);
        s.lfoAmplitudeReg = Byte(base + 11) & 0xf;
    }

    void WriteSlot(Slot& slot, std::int32_t reg, std::uint8_t data) {
        slot.regs[reg] = data;
        switch (reg) {
            case 0:  // PANPOT
                slot.pan = (data >> 4) & 0xf;
                break;
            case 1:  // Sample
                // A sample write causes some base parameters to be written to the registers (envelope and LFOs).
                InitSample(slot.sample, slot.regs[1] | ((slot.regs[2] & 1) << 8));
                WriteSlot(slot, 6, slot.sample.lfoVibratoReg);
                WriteSlot(slot, 7, slot.sample.lfoAmplitudeReg);
                // retrigger if key is on
                if (slot.playing) RetriggerSample(slot);
                break;
            case 2:  // Pitch
            case 3:
                slot.octave = slot.regs[3] >> 4;
                slot.pitch = static_cast<std::uint16_t>(((slot.regs[3] & 0xf) << 6) | (slot.regs[2] >> 2));
                UpdateStep(slot);
                break;
            case 4:  // KeyOn/Off
                if (data & 0x80) {
                    slot.playing = true;
                    RetriggerSample(slot);
                } else if (slot.playing) {
                    if (slot.sample.releaseReg != 0xf) {
                        slot.env.state = EnvState::Release;
                    } else {
                        slot.playing = false;
                    }
                }
                break;
            case 5:  // TL + Interpolation
                slot.destTotalLevel = (data >> 1) & 0x7f;
                if (!(data & 1)) {  // interpolate TL
                    if ((slot.totalLevel >> TL_SHIFT) > slot.destTotalLevel) {
                        slot.totalLevelStep = totalLevelSteps[0];  // decrease
                    } else {
                        slot.totalLevelStep = totalLevelSteps[1];  // increase
                    }
                } else {
                    slot.totalLevel = slot.destTotalLevel << TL_SHIFT;
                }
                break;
            case 6:  // LFO frequency + pitch LFO
            case 7:  // amplitude LFO
                slot.lfoFrequency = (slot.regs[6] >> 3) & 7;
                slot.vibrato = slot.regs[6] & 7;
                slot.tremolo = slot.regs[7] & 7;
                if (data) {
                    LfoComputeStep(slot.pitchLfo, slot.lfoFrequency, slot.vibrato, false);
                    LfoComputeStep(slot.amplitudeLfo, slot.lfoFrequency, slot.tremolo, true);
                }
                break;
        }
    }

    // One output sample of every voice; the voice's value (already multiplied by the sum of its two pan factors) goes to out[v].
    void RenderSample(float* out) {
        for (std::uint32_t sl = 0; sl < MultiPcmDeviceAdapter::kVoiceCount; ++sl) {
            Slot& slot = slots[sl];
            if (!slot.playing) {
                out[sl] = 0.0f;
                slot.gainLeft = slot.gainRight = 0.0f;
                continue;
            }
            const std::uint32_t vol = (slot.totalLevel >> TL_SHIFT) | (slot.pan << 7);
            std::uint32_t spos = slot.offset >> TL_SHIFT;
            std::uint32_t step = slot.step;
            std::int32_t csample = 0;
            const std::int32_t fpart = static_cast<std::int32_t>(slot.offset & ((1 << TL_SHIFT) - 1));

            if (slot.reverse) spos = slot.sample.end - spos - 1;

            if (slot.sample.format & 4) {  // 12-bit linear
                const std::uint32_t adr = slot.sample.start + (spos >> 1) * 3;
                if (!(spos & 1)) {  // ab.c ..
                    const std::int16_t w0 = static_cast<std::int16_t>(Byte(adr) << 8 | ((Byte(adr + 1) & 0xf) << 4));
                    csample = w0;
                } else {  // ..C. AB
                    const std::int16_t w0 = static_cast<std::int16_t>((Byte(adr + 2) << 8) | (Byte(adr + 1) & 0xf0));
                    csample = w0;
                }
            } else {
                csample = static_cast<std::int16_t>(Byte(slot.sample.start + spos) << 8);
            }

            std::int32_t sample = (csample * fpart + slot.prevSample * ((1 << TL_SHIFT) - fpart)) >> TL_SHIFT;

            if (slot.vibrato) {  // vibrato enabled
                step = step * static_cast<std::uint32_t>(LfoStep(slot.pitchLfo));
                step >>= TL_SHIFT;
            }

            slot.offset += step;

            if (spos ^ (slot.offset >> TL_SHIFT)) slot.prevSample = csample;

            if (slot.offset >= (slot.sample.end << TL_SHIFT)) {
                slot.offset -= (slot.sample.end - slot.sample.loop) << TL_SHIFT;
                // The looped silence at the end of some samples is the same whether reversed or not.
                slot.reverse = false;
            }

            if ((slot.totalLevel >> TL_SHIFT) != slot.destTotalLevel) {
                slot.totalLevel += static_cast<std::uint32_t>(slot.totalLevelStep);
            }

            if (slot.tremolo) {  // tremolo enabled
                sample = sample * LfoStep(slot.amplitudeLfo);
                sample >>= TL_SHIFT;
            }

            sample = (sample * EnvelopeUpdate(slot)) >> 10;

            const float gl = static_cast<float>(leftPanTable[vol]) / static_cast<float>(1 << TL_SHIFT);
            const float gr = static_cast<float>(rightPanTable[vol]) / static_cast<float>(1 << TL_SHIFT);
            slot.gainLeft = gl;
            slot.gainRight = gr;
            out[sl] = static_cast<float>(sample) * (gl + gr) / 32768.0f;
        }
    }
};

MultiPcmDeviceAdapter::MultiPcmDeviceAdapter(std::uint32_t clockHz, SampleReadFn sampleRead)
    : impl_(std::make_unique<Impl>(clockHz, std::move(sampleRead))) {}

MultiPcmDeviceAdapter::~MultiPcmDeviceAdapter() = default;

void MultiPcmDeviceAdapter::Reset() {
    for (Slot& slot : impl_->slots) slot.playing = false;
}

void MultiPcmDeviceAdapter::Write(std::uint32_t offset, std::uint8_t data) {
    switch (offset) {
        case 0:  // data write
            impl_->WriteSlot(impl_->slots[impl_->curSlot], static_cast<std::int32_t>(impl_->address), data);
            break;
        case 1: {
            const std::int32_t channel = VALUE_TO_CHANNEL[data & 0x1f];
            // The original indexes its slot array with this value even when it is -1; no voice is selected then.
            impl_->curSlot = channel < 0 ? kVoiceCount - 1 : static_cast<std::uint32_t>(channel);
            break;
        }
        case 2:
            impl_->address = (data > 7) ? 7 : data;
            break;
        default:
            break;
    }
}

std::size_t MultiPcmDeviceAdapter::Render(std::uint64_t clocks, std::vector<std::vector<float>>& voiceSamples) {
    if (voiceSamples.size() < kVoiceCount) voiceSamples.resize(kVoiceCount);
    const std::uint64_t available = impl_->partialClocks + clocks;
    const std::size_t count = static_cast<std::size_t>(available / kClockDivider);
    impl_->partialClocks = available % kClockDivider;
    if (count == 0) return 0;
    for (std::uint32_t v = 0; v < kVoiceCount; ++v) {
        voiceSamples[v].resize(voiceSamples[v].size() + count);
    }
    float one[kVoiceCount];
    for (std::size_t i = 0; i < count; ++i) {
        impl_->RenderSample(one);
        for (std::uint32_t v = 0; v < kVoiceCount; ++v) {
            voiceSamples[v][voiceSamples[v].size() - count + i] = one[v];
        }
    }
    impl_->generatedSamples += count;
    return count;
}

float MultiPcmDeviceAdapter::VoiceGainLeft(std::uint32_t voice) const { return voice < kVoiceCount ? impl_->slots[voice].gainLeft : 0.0f; }
float MultiPcmDeviceAdapter::VoiceGainRight(std::uint32_t voice) const { return voice < kVoiceCount ? impl_->slots[voice].gainRight : 0.0f; }
bool MultiPcmDeviceAdapter::VoicePlaying(std::uint32_t voice) const { return voice < kVoiceCount && impl_->slots[voice].playing; }
std::uint32_t MultiPcmDeviceAdapter::VoiceTotalLevel(std::uint32_t voice) const {
    if (voice >= kVoiceCount || !impl_->slots[voice].playing) return 127;
    return std::min<std::uint32_t>(impl_->slots[voice].totalLevel >> TL_SHIFT, 127);
}
std::uint64_t MultiPcmDeviceAdapter::GeneratedSampleCount() const { return impl_->generatedSamples; }
float MultiPcmDeviceAdapter::OutputRateHz() const { return impl_->rate; }

std::shared_ptr<MultiPcmDeviceAdapter::SavedState> MultiPcmDeviceAdapter::SaveState() const {
    auto state = std::make_shared<SavedState>();
    state->slots = impl_->slots;
    state->curSlot = impl_->curSlot;
    state->address = impl_->address;
    state->partialClocks = impl_->partialClocks;
    state->generatedSamples = impl_->generatedSamples;
    return state;
}

void MultiPcmDeviceAdapter::RestoreState(const SavedState& state) {
    impl_->slots = state.slots;
    impl_->curSlot = state.curSlot;
    impl_->address = state.address;
    impl_->partialClocks = state.partialClocks;
    impl_->generatedSamples = state.generatedSamples;
}
