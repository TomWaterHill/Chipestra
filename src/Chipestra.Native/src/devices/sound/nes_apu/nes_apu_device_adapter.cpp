// license:GPL-2.0+
// copyright-holders:Matthew Conte
// Based on the Nofrendo/Nosefart NES RP2A03 sound emulation core written by Matthew Conte and
// redesigned for use in MAME by Who Wants to Know?; released under the GNU Public License.
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/sound/nes_apu.cpp  [copyright-holders:Matthew Conte]
//   src/devices/sound/nes_apu.h  [copyright-holders:Matthew Conte]
//   src/devices/sound/nes_defs.h  [copyright-holders:Matthew Conte]
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#include "nes_apu_device_adapter.h"

#include <utility>

namespace {

// vblank length table used for squares, triangle, noise
const std::uint8_t kVblLength[32] = {
    10, 254, 20, 2, 40, 4, 80, 6, 160, 8, 60, 10, 14, 12, 26, 14,
    12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30};

// frequency limit of square channels
const int kFreqLimit[8] = {0x3FF, 0x555, 0x666, 0x71C, 0x787, 0x7C1, 0x7E0, 0x7F0};

// noise period; each fundamental is determined as: freq = master / period / 93
const int kNoiseFreq[2][16] = {
    {4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068},  // NTSC
    {4, 8, 14, 30, 60, 88, 118, 148, 188, 236, 354, 472, 708, 944, 1890, 3778}};  // PAL

// dpcm (cpu) cycle period; each frequency is determined as: freq = master / period
const int kDpcmClocks[2][16] = {
    {428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 84, 72, 54},  // NTSC
    {398, 354, 316, 298, 276, 236, 210, 198, 176, 148, 132, 118, 98, 78, 66, 50}};  // PAL

// ratios of pos/neg pulse for square waves: 12.5%, 25%, 50%, 25% negated
const int kDutyLut[4] = {0b01000000, 0b01100000, 0b01111000, 0b10011111};

inline int Bit(unsigned value, unsigned bit) { return static_cast<int>((value >> bit) & 1u); }

}  // namespace

NesApuDeviceAdapter::NesApuDeviceAdapter(bool pal, Variant variant) : is_pal_(pal), variant_(variant) {
    // calculate mixer output
    //   pulse channel output:   95.88 / (8128 / (pulse 1 + pulse 2) + 100)
    //   triangle/noise/DMC:     159.79 / (1 / (tri/8227 + noise/12241 + dmc/22638) + 100)
    for (int i = 0; i < 31; i++) {
        const double pulseOut = (i == 0) ? 0.0 : 95.88 / ((8128.0 / i) + 100.0);
        square_lut_[i] = static_cast<float>(pulseOut);
    }
    for (int t = 0; t < 16; t++) {
        for (int n = 0; n < 16; n++) {
            for (int d = 0; d < 128; d++) {
                double tndOut = (t / 8227.0) + (n / 12241.0) + (d / 22638.0);
                tndOut = (tndOut == 0.0) ? 0.0 : 159.79 / ((1.0 / tndOut) + 100.0);
                tnd_lut_[t][n][d] = static_cast<float>(tndOut);
            }
        }
    }
    Reset();
}

void NesApuDeviceAdapter::CalculateRates() {
    samps_per_sync_ = frame_clocks_ / 4;  // FIXME in the original: tables are 4-step mode ONLY

    // initialize sample times in terms of vsyncs
    for (unsigned i = 0; i < kSyncsMax1; i++) {
        vbl_times_[i] = kVblLength[i] * samps_per_sync_ / 2;
        sync_times1_[i] = samps_per_sync_ * (i + 1);
    }
    for (unsigned i = 0; i < kSyncsMax2; i++) sync_times2_[i] = (samps_per_sync_ * i) >> 2;
}

void NesApuDeviceAdapter::Reset() {
    // device_start()
    apu_ = Apu{};
    frame_clocks_ = is_pal_ ? 33254 : 29830;
    apu_.step_mode = 4;
    apu_.frame_irq_enabled = true;
    CalculateRates();
    frame_timer_active_ = false;
    frame_countdown_ = 0;
    cycles_into_sample_ = 0;

    // device_reset()
    Write(0x15, 0x00);
    apu_.tri.adder = 0;
    if (apu_.frame_irq_enabled) {
        frame_timer_active_ = true;
        frame_countdown_ = frame_clocks_;
    }
}

void NesApuDeviceAdapter::FrameTimerFired() {
    if (apu_.step_mode == 4 && apu_.frame_irq_enabled) {
        apu_.frame_irq_occurred = true;
        SetIrq(true);
    }
}

std::uint8_t NesApuDeviceAdapter::ChannelLevelForTest(Channel channel) const {
    switch (channel) {
    case kPulse1: return apu_.squ[0].output;
    case kPulse2: return apu_.squ[1].output;
    case kTriangle: return apu_.tri.output;
    case kNoise: return apu_.noi.output;
    case kDmc: return apu_.dpcm.output;
    }
    return 0;
}

std::int32_t NesApuDeviceAdapter::LengthForTest(Channel channel) const {
    switch (channel) {
    case kPulse1: return apu_.squ[0].vbl_length;
    case kPulse2: return apu_.squ[1].vbl_length;
    case kTriangle: return apu_.tri.vbl_length;
    case kNoise: return apu_.noi.vbl_length;
    case kDmc: return static_cast<std::int32_t>(apu_.dpcm.length);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Channel generators (one call per output sample)
// ---------------------------------------------------------------------------

// OUTPUT SQUARE WAVE SAMPLE (VALUES FROM 0 to +15)
void NesApuDeviceAdapter::ApuSquare(Square& chan) {
    // reg0: 0-3=volume, 4=envelope, 5=hold, 6-7=duty cycle
    // reg1: 0-2=sweep shifts, 3=sweep inc/dec, 4-6=sweep length, 7=sweep on
    // reg2: 8 bits of freq
    // reg3: 0-2=high freq, 7-4=vbl length counter
    if (!chan.enabled) {
        chan.output = 0;
        return;
    }

    // enveloping: decay is at a rate of (env_regs + 1) / 240 secs
    const int envDelay = static_cast<int>(sync_times1_[chan.regs[0] & 0x0f]);
    chan.env_phase -= 4;
    while (chan.env_phase < 0) {
        chan.env_phase += static_cast<float>(envDelay);
        if (chan.regs[0] & 0x20)
            chan.env_vol = (chan.env_vol + 1) & 15;
        else if (chan.env_vol < 15)
            chan.env_vol++;
    }

    // vbl length counter
    if (chan.vbl_length > 0 && !(chan.regs[0] & 0x20)) chan.vbl_length--;

    if (!chan.vbl_length) {
        chan.output = 0;
        return;
    }

    // freqsweeps
    if ((chan.regs[1] & 0x80) && (chan.regs[1] & 7)) {
        const int sweepDelay = static_cast<int>(sync_times1_[(chan.regs[1] >> 4) & 7]);
        chan.sweep_phase -= 2;
        while (chan.sweep_phase < 0) {
            chan.sweep_phase += static_cast<float>(sweepDelay);
            if (chan.regs[1] & 8)
                chan.freq -= chan.freq >> (chan.regs[1] & 7);
            else
                chan.freq += chan.freq >> (chan.regs[1] & 7);
        }
    }

    if ((!(chan.regs[1] & 8) && (chan.freq >> 16) > kFreqLimit[chan.regs[1] & 7]) || (chan.freq >> 16) < 4) {
        chan.output = 0;
        return;
    }

    chan.phaseacc -= 4;
    while (chan.phaseacc < 0) {
        chan.phaseacc += static_cast<float>(chan.freq >> 16);
        chan.adder = (chan.adder + 1) & 0x0f;
    }

    if (chan.regs[0] & 0x10)  // fixed volume
        chan.output = chan.regs[0] & 0x0f;
    else
        chan.output = static_cast<std::uint8_t>(0x0f - chan.env_vol);

    chan.output = static_cast<std::uint8_t>(chan.output * Bit(static_cast<unsigned>(kDutyLut[chan.regs[0] >> 6]),
                                                                7u - ((chan.adder >> 1) & 7u)));
}

// OUTPUT TRIANGLE WAVE SAMPLE (VALUES FROM 0 to +15)
void NesApuDeviceAdapter::ApuTriangle(Triangle& chan) {
    // reg0: 7=holdnote, 6-0=linear length counter
    // reg2: low 8 bits of frequency
    // reg3: 7-3=length counter, 2-0=high 3 bits of frequency
    if (!chan.enabled) return;

    const bool notHeld = !Bit(chan.regs[0], 7);

    if (!chan.counter_started && notHeld) {
        if (chan.write_latency) chan.write_latency--;
        if (!chan.write_latency) chan.counter_started = true;
    }

    if (chan.counter_started) {
        if (chan.linear_reload)
            chan.linear_length = static_cast<std::int32_t>(sync_times2_[chan.regs[0] & 0x7f]);
        else if (chan.linear_length > 0)
            chan.linear_length--;

        if (notHeld) chan.linear_reload = false;

        if (chan.vbl_length && notHeld) chan.vbl_length--;
    }

    if (!(chan.linear_length && chan.vbl_length)) return;

    const int freq = ((chan.regs[3] & 7) << 8) + chan.regs[2] + 1;

    // The original halts ultrasonic frequencies here (it notes that the hardware pops instead).
    if (freq < 2) return;

    chan.phaseacc -= 4;
    while (chan.phaseacc < 0) {
        chan.phaseacc += static_cast<float>(freq);
        chan.adder++;

        chan.output = chan.adder & 0xf;
        if (!Bit(chan.adder, 4)) chan.output ^= 0xf;
    }
}

// OUTPUT NOISE WAVE SAMPLE (VALUES FROM 0 to +15)
void NesApuDeviceAdapter::ApuNoise(Noise& chan) {
    // reg0: 0-3=volume, 4=envelope, 5=hold
    // reg2: 7=small(93 byte) sample,3-0=freq lookup
    // reg3: 7-4=vbl length counter
    if (!chan.enabled) {
        chan.output = 0;
        return;
    }

    // enveloping: decay is at a rate of (env_regs + 1) / 240 secs
    const int envDelay = static_cast<int>(sync_times1_[chan.regs[0] & 0x0f]);
    chan.env_phase -= 4;
    while (chan.env_phase < 0) {
        chan.env_phase += static_cast<float>(envDelay);
        if (chan.regs[0] & 0x20)
            chan.env_vol = (chan.env_vol + 1) & 15;
        else if (chan.env_vol < 15)
            chan.env_vol++;
    }

    // length counter
    if (!(chan.regs[0] & 0x20)) {
        if (chan.vbl_length > 0) chan.vbl_length--;
    }

    if (!chan.vbl_length) {
        chan.output = 0;
        return;
    }

    const int freq = kNoiseFreq[is_pal_ ? 1 : 0][chan.regs[2] & 0x0f];
    chan.phaseacc -= 4;
    while (chan.phaseacc < 0) {
        chan.phaseacc += static_cast<float>(freq);
        UpdateLfsr(chan);
    }

    if (Bit(chan.lfsr, 0)) {  // silence channel
        chan.output = 0;
        return;
    }

    if (chan.regs[0] & 0x10)  // fixed volume
        chan.output = chan.regs[0] & 0x0f;
    else
        chan.output = static_cast<std::uint8_t>(0x0f - chan.env_vol);
}

void NesApuDeviceAdapter::UpdateLfsr(Noise& chan) const {
    const unsigned tap = (variant_ == Variant::kRp2a03g && (chan.regs[2] & 0x80)) ? 6u : 1u;
    chan.lfsr = static_cast<std::uint16_t>(chan.lfsr | ((Bit(chan.lfsr, 0) ^ Bit(chan.lfsr, tap)) << 15));
    chan.lfsr = static_cast<std::uint16_t>(chan.lfsr >> 1);
}

// RESET DPCM PARAMETERS
void NesApuDeviceAdapter::DpcmReset(Dpcm& chan) {
    chan.address = 0xc000u + static_cast<std::uint16_t>(chan.regs[2] << 6);
    chan.length = static_cast<std::uint16_t>(chan.regs[3] << 4) + 1u;
    chan.bits_left = static_cast<std::int32_t>(chan.length << 3);
    chan.enabled = true;
}

// OUTPUT DPCM WAVE SAMPLE (VALUES FROM 0 to +127)
void NesApuDeviceAdapter::ApuDpcm(Dpcm& chan) {
    // reg0: 7=irq gen, 6=looping, 3-0=pointer to clock table
    // reg1: output dc level, 7 bits unsigned
    // reg2: 8 bits of 64-byte aligned address offset : $C000 + (value * 64)
    // reg3: length, (value * 16) + 1
    if (chan.enabled) {
        const int freq = kDpcmClocks[is_pal_ ? 1 : 0][chan.regs[0] & 0x0f];
        chan.phaseacc -= 4;

        while (chan.phaseacc < 0) {
            chan.phaseacc += static_cast<float>(freq);

            if (chan.bits_left <= 0) {
                chan.enabled = false;
                if (chan.regs[0] & 0x40) {
                    DpcmReset(chan);
                } else {
                    if (chan.regs[0] & 0x80) {  // IRQ generator
                        chan.irq_occurred = true;
                        SetIrq(true);
                    }
                    break;
                }
            }

            // The bits of a byte are shifted out starting with the least significant one, and a byte is read when
            // its first bit is needed. The 7-bit counter stays where it is when it is asked to move past either end.
            const int bitPos = (8 - (chan.bits_left & 7)) & 7;
            if (0 == bitPos) {
                chan.cur_byte = memoryRead_ ? memoryRead_(static_cast<std::uint16_t>(chan.address)) : std::uint8_t{0};
                chan.address++;
                chan.length--;
            }
            chan.bits_left--;

            if (chan.cur_byte & (1 << bitPos)) {
                if (chan.vol <= 125) chan.vol = static_cast<std::int16_t>(chan.vol + 2);
            } else if (chan.vol >= 2) {
                chan.vol = static_cast<std::int16_t>(chan.vol - 2);
            }
        }
    }

    chan.output = static_cast<std::uint8_t>(chan.vol);
}

// ---------------------------------------------------------------------------
// Registers
// ---------------------------------------------------------------------------

void NesApuDeviceAdapter::Write(std::uint8_t offset, std::uint8_t value) {
    const int chan = (offset >> 2) & 1;

    switch (offset) {
    // squares
    case 0x00:
    case 0x04:
        apu_.squ[chan].regs[0] = value;
        break;

    case 0x01:
    case 0x05:
        apu_.squ[chan].regs[1] = value;
        break;

    case 0x02:
    case 0x06:
        apu_.squ[chan].regs[2] = value;
        if (apu_.squ[chan].enabled) apu_.squ[chan].freq = ((((apu_.squ[chan].regs[3] & 7) << 8) + value) + 1) << 16;
        break;

    case 0x03:
    case 0x07:
        apu_.squ[chan].regs[3] = value;

        if (apu_.squ[chan].enabled) {
            apu_.squ[chan].vbl_length = static_cast<std::int32_t>(vbl_times_[value >> 3]);
            apu_.squ[chan].env_vol = 0;
            apu_.squ[chan].freq = ((((value & 7) << 8) + apu_.squ[chan].regs[2]) + 1) << 16;
        }
        break;

    // triangle
    case 0x08:
        apu_.tri.regs[0] = value;

        if (apu_.tri.enabled) {
            if (!apu_.tri.counter_started) apu_.tri.linear_length = static_cast<std::int32_t>(sync_times2_[value & 0x7f]);
        }
        break;

    case 0x0A:
        apu_.tri.regs[2] = value;
        break;

    case 0x0B:
        apu_.tri.regs[3] = value;

        // There is some latency on the real chip between a write to the triangle's register 0 and the
        // linear length counter starting its countdown; the original delays the start by a few samples.
        apu_.tri.write_latency = static_cast<std::int32_t>((samps_per_sync_ + 239) / 240);

        if (apu_.tri.enabled) {
            apu_.tri.counter_started = false;
            apu_.tri.vbl_length = static_cast<std::int32_t>(vbl_times_[value >> 3]);
            apu_.tri.linear_length = static_cast<std::int32_t>(sync_times2_[apu_.tri.regs[0] & 0x7f]);
            apu_.tri.linear_reload = true;
        }
        break;

    // noise
    case 0x0C:
        apu_.noi.regs[0] = value;
        break;

    case 0x0E:
        apu_.noi.regs[2] = value;
        break;

    case 0x0F:
        apu_.noi.regs[3] = value;

        if (apu_.noi.enabled) {
            apu_.noi.vbl_length = static_cast<std::int32_t>(vbl_times_[value >> 3]);
            apu_.noi.env_vol = 0;  // reset envelope
        }
        break;

    // DMC
    case 0x10:
        apu_.dpcm.regs[0] = value;
        if (!(value & 0x80)) {
            apu_.dpcm.irq_occurred = false;
            if (!apu_.frame_irq_occurred) SetIrq(false);
        }
        break;

    case 0x11:  // 7-bit DAC
        apu_.dpcm.regs[1] = value & 0x7f;
        apu_.dpcm.vol = apu_.dpcm.regs[1];
        break;

    case 0x12:
        apu_.dpcm.regs[2] = value;
        break;

    case 0x13:
        apu_.dpcm.regs[3] = value;
        break;

    case 0x17:
        if (value & 0x80)
            apu_.step_mode = 5;
        else
            apu_.step_mode = 4;

        apu_.frame_irq_enabled = !Bit(value, 6);
        if (apu_.frame_irq_enabled) {
            frame_timer_active_ = true;
            frame_countdown_ = frame_clocks_;
        } else {
            apu_.frame_irq_occurred = false;
            if (!apu_.dpcm.irq_occurred) SetIrq(false);
            frame_timer_active_ = false;
        }
        break;

    case 0x15:
        if (value & 0x01) {
            apu_.squ[0].enabled = true;
        } else {
            apu_.squ[0].enabled = false;
            apu_.squ[0].vbl_length = 0;
        }

        if (value & 0x02) {
            apu_.squ[1].enabled = true;
        } else {
            apu_.squ[1].enabled = false;
            apu_.squ[1].vbl_length = 0;
        }

        if (value & 0x04) {
            apu_.tri.enabled = true;
        } else {
            apu_.tri.enabled = false;
            apu_.tri.vbl_length = 0;
            apu_.tri.linear_length = 0;
            apu_.tri.counter_started = false;
            apu_.tri.write_latency = 0;
        }

        if (value & 0x08) {
            apu_.noi.enabled = true;
        } else {
            apu_.noi.enabled = false;
            apu_.noi.vbl_length = 0;
        }

        if (value & 0x10) {
            // only reset dpcm values if DMA is finished
            if (!apu_.dpcm.enabled) {
                apu_.dpcm.enabled = true;
                DpcmReset(apu_.dpcm);
            }
        } else {
            apu_.dpcm.enabled = false;
        }

        apu_.dpcm.irq_occurred = false;
        if (!apu_.frame_irq_occurred) SetIrq(false);
        break;

    default:
        // The original also lists $09 and $0D (the unused triangle/noise registers), but its address
        // map passes offsets from $4000, so those cases are never reached; unknown writes do nothing.
        break;
    }
}

// Read status register at $4015
std::uint8_t NesApuDeviceAdapter::ReadStatus() {
    std::uint8_t readval = 0;
    if (apu_.squ[0].vbl_length > 0) readval |= 0x01;
    if (apu_.squ[1].vbl_length > 0) readval |= 0x02;
    if (apu_.tri.vbl_length > 0) readval |= 0x04;
    if (apu_.noi.vbl_length > 0) readval |= 0x08;
    if (apu_.dpcm.enabled) readval |= 0x10;
    if (apu_.frame_irq_occurred) readval |= 0x40;
    if (apu_.dpcm.irq_occurred) readval |= 0x80;

    apu_.frame_irq_occurred = false;
    if (!apu_.dpcm.irq_occurred) SetIrq(false);

    return readval;
}

// ---------------------------------------------------------------------------
// Time advance and mixing
// ---------------------------------------------------------------------------

void NesApuDeviceAdapter::ProduceSample(std::array<std::vector<float>, kChannelCount>& channelOut) {
    ApuSquare(apu_.squ[0]);
    ApuSquare(apu_.squ[1]);
    ApuTriangle(apu_.tri);
    ApuNoise(apu_.noi);
    ApuDpcm(apu_.dpcm);

    const unsigned levels[kChannelCount] = {apu_.squ[0].output, apu_.squ[1].output, apu_.tri.output, apu_.noi.output,
                                            apu_.dpcm.output};
    float shares[kChannelCount];
    MixShares(levels, shares);
    if (muted_mask_ != 0) {
        // Calculate the mixer again without the muted channels; the muted channels keep their share of the
        // full mix (see SetMutedChannels).
        unsigned kept[kChannelCount];
        for (std::size_t c = 0; c < kChannelCount; ++c) kept[c] = (muted_mask_ & (1u << c)) ? 0u : levels[c];
        float remixed[kChannelCount];
        MixShares(kept, remixed);
        for (std::size_t c = 0; c < kChannelCount; ++c)
            if (!(muted_mask_ & (1u << c))) shares[c] = remixed[c];
    }
    for (std::size_t c = 0; c < kChannelCount; ++c) channelOut[c].push_back(shares[c]);
}

// Mixes the five channel levels with the original's two look-up tables and splits each group's value between
// its channels in proportion to their own linear weights; the last channel of a group takes the remainder, so
// the parts add up to the original mix exactly.
void NesApuDeviceAdapter::MixShares(const unsigned (&levels)[kChannelCount], float (&shares)[kChannelCount]) const {
    const unsigned s0 = levels[kPulse1], s1 = levels[kPulse2], t = levels[kTriangle], n = levels[kNoise], d = levels[kDmc];
    const float squareMix = square_lut_[s0 + s1];
    const float tndMix = tnd_lut_[t][n][d];

    float p0 = 0.0f, p1 = squareMix;
    if (s0 + s1 > 0) {
        p0 = squareMix * static_cast<float>(s0) / static_cast<float>(s0 + s1);
        p1 = squareMix - p0;
    }
    float tr = 0.0f, no = 0.0f, dm = tndMix;
    const double wt = t / 8227.0, wn = n / 12241.0, wd = d / 22638.0;
    const double total = wt + wn + wd;
    if (total > 0.0) {
        tr = static_cast<float>(tndMix * (wt / total));
        no = static_cast<float>(tndMix * (wn / total));
        dm = tndMix - tr - no;
    }
    shares[kPulse1] = p0;
    shares[kPulse2] = p1;
    shares[kTriangle] = tr;
    shares[kNoise] = no;
    shares[kDmc] = dm;
}

std::size_t NesApuDeviceAdapter::Render(std::uint64_t cpuCycles, std::array<std::vector<float>, kChannelCount>& channelOut) {
    std::size_t produced = 0;
    std::uint64_t remaining = cpuCycles;
    while (remaining > 0) {
        std::uint64_t step = kCpuCyclesPerSample - cycles_into_sample_;
        if (step > remaining) step = remaining;
        if (frame_timer_active_ && step > frame_countdown_) step = frame_countdown_;
        remaining -= step;
        cycles_into_sample_ += static_cast<std::uint32_t>(step);
        if (frame_timer_active_) frame_countdown_ -= step;

        if (cycles_into_sample_ == kCpuCyclesPerSample) {
            cycles_into_sample_ = 0;
            ProduceSample(channelOut);
            ++produced;
        }
        if (frame_timer_active_ && frame_countdown_ == 0) {
            frame_countdown_ = frame_clocks_;  // the timer repeats
            FrameTimerFired();
        }
    }
    return produced;
}
