// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Ym2610DeviceAdapter native tests. All register writes and ADPCM
// sample bytes below are hand-written synthetic stimulus - no real ROM data.
//
// Register semantics below are derived from ymfm's OPN source
// (ymfm_opn.{h,cpp}/ymfm_adpcm.{h,cpp}):
//   - FM registers use the same address+data two-port scheme as plain OPN
//     (Write(0,addr)/Write(1,data)) for register-offset ("choffs") indices
//     0,1,2, but Write(2,addr)/Write(3,data) (the "high" port) for choffs
//     3,4,5 - opn_registers_base<true>::channel_offset(chnum) = (chnum%3) +
//     0x100*(chnum/3), and ym2610::write_data_hi() strips that 0x100 bit
//     back off before dispatching to the FM engine, so the actual address
//     byte sent is identical to the low-port case for the same "chnum%3"
//     local channel - only the port (0/1 vs 2/3) differs. This adapter's
//     kFmRegisterIndex maps its logical channels 0-3 onto real choffs
//     {1,2,4,5}: choffs 1,2 use the low port with local channel 1,2; choffs
//     4,5 use the high port with local channel (4%3)=1, (5%3)=2.
//   - Key-on register 0x28 is always on the low port regardless of which
//     bank the channel is in (channel = bits0-1 of the data byte, +3 if bit2
//     is set - so choffs 1 -> data bit-field 0b001=1, choffs 2 -> 0b010=2,
//     choffs 4 (local 1, high bank) -> 0b101=5, choffs 5 (local 2, high
//     bank) -> 0b110=6). Bits 4-7 select which operators to key on.
//   - Channel pan register 0xB4+localChannel (bit7=L, bit6=R) is not covered
//     by the 0xA0-0xAF latched-pair special case so it writes directly like
//     any other register - but it defaults to 0 (all pan bits clear) at
//     reset, so a test must set it or the note stays silent regardless of
//     the FM synthesis itself (add_to_output() gates entirely on these two
//     bits).
//   - ADPCM-A: per-channel pan/level register is at index choffs+0x08
//     (bit7=panL, bit6=panR, bits0-4=instrument_level), start/end address at
//     choffs+0x10/+0x18 (start, low/high byte) and choffs+0x20/+0x28 (end).
//     A channel's playback is silent unless both ch_instrument_level (0x1F =
//     loudest) and the global total_level register (index 0x01, 0x3F =
//     loudest) are near their max values (adpcm_a_channel::output()'s
//     `vol=(level^0x1f)+(total^0x3f); if (vol>=63) return;` gate). Key-on is
//     register index 0x00: bit(choffs) set + bit7 clear keys ON that channel.
//   - ADPCM-B: registers reached via the low port at chip addresses
//     0x10-0x1B (ym2610::write_data() dispatches these to
//     adpcm_b_engine::write(address&0x0f, data) - so callers use
//     Write(0, 0x10..0x1B)/Write(1, data)). Chip address 0x10 = engine
//     register 0x00 (execute=bit7, record=bit6 - ym2610 itself forces
//     external=1/record=0 on writes to this chip address), 0x11 = pan/mode
//     (panL=bit7, panR=bit6), 0x12/0x13 = start lo/hi, 0x14/0x15 = end
//     lo/hi, 0x19/0x1A = delta_n lo/hi (playback rate - 0xFFFF is used
//     below to guarantee fast advancement within a small test render
//     budget), 0x1B = level.
//   - SSG registers are always on the low port (addresses 0x00-0x0D,
//     matching plain AY-3-8910 semantics - see
//     ProgramLoudSsgNote in ym2203_adapter_tests.cpp).

#include "ym2610_adapter_tests.h"

#include "devices/sound/ym2610/ym2610_device_adapter.h"
#include "test_support.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

std::vector<std::vector<float>> MakeChannelBuffers() {
    return std::vector<std::vector<float>>(Ym2610DeviceAdapter::kChannelCount);
}

float PeakOf(const std::vector<std::vector<float>>& buffers, std::uint32_t channel) {
    float peak = 0.0f;
    for (float sample : buffers[channel]) {
        peak = std::max(peak, std::fabs(sample));
    }
    return peak;
}

// Programs a loud, sustained note on FM register-offset (choffs) 1 - this
// adapter's logical FM channel 0 (kFmRegisterIndex[0] == 1, ym2610_device_
// adapter.cpp) - via the low address/data port pair, algorithm 7 (all four
// operators sum straight to the output, same convention as
// Ym2203DeviceAdapter's ProgramLoudFmNote helper).
void ProgramLoudFmNoteOnLogicalChannel0(Ym2610DeviceAdapter& ym) {
    auto w = [&](std::uint8_t reg, std::uint8_t value) {
        ym.Write(0, reg);
        ym.Write(1, value);
    };
    constexpr std::uint8_t kLocal = 1;  // choffs 1 % 3
    w(0xB0 + kLocal, 0x07);  // feedback 0, algorithm 7
    w(0xB4 + kLocal, 0xC0);  // pan: both L and R enabled (see the header comment)
    w(0xA4 + kLocal, 0x22);  // block/fnum high - any mid-range audible pitch
    w(0xA0 + kLocal, 0x00);  // fnum low
    for (std::uint8_t o : {std::uint8_t(0), std::uint8_t(4), std::uint8_t(8), std::uint8_t(12)}) {
        w(0x30 + o + kLocal, 0x01);  // detune 0, multiple 1
        w(0x40 + o + kLocal, 0x00);  // total level 0 (max volume)
        w(0x50 + o + kLocal, 0x1F);  // KSR 0, attack rate 31 (instant attack)
        w(0x60 + o + kLocal, 0x00);  // decay rate 0 (stay at attack peak)
        w(0x70 + o + kLocal, 0x00);  // sustain rate 0
        w(0x80 + o + kLocal, 0x00);  // sustain level 0, release rate 0
    }
    // Key-on register 0x28: bits0-1=local channel(1), bit2=0(low bank),
    // bits4-7=operator mask (all 4) - see the header comment.
    w(0x28, 0xF0 | 0x01);
}

// Programs a loud, sustained tone on SSG channel A (register-compatible
// AY-3-8910 semantics, always the low port - see the header comment).
void ProgramLoudSsgNote(Ym2610DeviceAdapter& ym) {
    auto w = [&](std::uint8_t reg, std::uint8_t value) {
        ym.Write(0, reg);
        ym.Write(1, value);
    };
    w(0x00, 0x20);  // tone period fine (channel A) - any small nonzero value
    w(0x01, 0x00);  // tone period coarse (channel A)
    w(0x07, 0x3E);  // mixer: bit0 clear = tone A enabled, all else disabled
    w(0x08, 0x0F);  // channel A volume: max, fixed level (not envelope)
}

// A small synthetic ADPCM sample: every nibble a large positive delta code
// (0x7 = max magnitude, sign bit clear), guaranteeing a strongly non-silent,
// changing accumulator (ymfm_adpcm.cpp's decode formula) - NOT real ROM audio
// data.
std::vector<std::uint8_t> MakeSyntheticAdpcmSamples(std::size_t byteCount) { return std::vector<std::uint8_t>(byteCount, 0x77); }

}  // namespace

void Ym2610RegisterAccessAndStatusBusy() {
    std::printf("Ym2610RegisterAccessAndStatusBusy\n");

    Ym2610DeviceAdapter ym({}, {});
    auto buffers = MakeChannelBuffers();

    Check((ym.Read(0) & 0x80) == 0, "register access: status reads busy-clear after construction");

    // Any FM register write marks the chip busy for 32*clock_prescale(6) =
    // 192 input clocks (opn_registers_base<true>::DEFAULT_PRESCALE=6, this
    // adapter never overrides it - same DEFAULT_PRESCALE Ym2203DeviceAdapter
    // relies on). At kFmAdpcmInputClocksPerSample(144) clocks/sample, that's
    // still busy after 1 sample (144<192) and clear after 2 (288>=192).
    ym.Write(0, 0x30);
    ym.Write(1, 0x00);
    Check((ym.Read(0) & 0x80) != 0, "register access: STATUS_BUSY set right after a data write");

    ym.Render(1 * Ym2610DeviceAdapter::kFmAdpcmInputClocksPerSample, buffers);
    Check((ym.Read(0) & 0x80) != 0, "register access: STATUS_BUSY still set one sample before the 192-clock window elapses");

    ym.Render(1 * Ym2610DeviceAdapter::kFmAdpcmInputClocksPerSample, buffers);
    Check((ym.Read(0) & 0x80) == 0, "register access: STATUS_BUSY clears once the busy window elapses");
}

void Ym2610FmChannelOutputReachesPerChannelBuffers() {
    std::printf("Ym2610FmChannelOutputReachesPerChannelBuffers\n");

    Ym2610DeviceAdapter ym({}, {});
    auto buffers = MakeChannelBuffers();

    ProgramLoudFmNoteOnLogicalChannel0(ym);

    const std::uint64_t clocks = 2000 * Ym2610DeviceAdapter::kFmAdpcmInputClocksPerSample;
    const std::size_t produced = ym.Render(clocks, buffers);
    Check(produced == 2000, "FM channel output: Render() produced one FM/ADPCM sample per 144 input clocks");

    Check(PeakOf(buffers, Ym2610DeviceAdapter::kFmBase + 0) > 0.01f,
          "FM channel output: keyed logical FM channel 0 (real choffs 1) produced a non-silent signal");
    for (std::uint32_t ch = 1; ch < Ym2610DeviceAdapter::kFmChannelCount; ++ch) {
        Check(PeakOf(buffers, Ym2610DeviceAdapter::kFmBase + ch) == 0.0f,
              "FM channel output: untouched FM channel stayed exactly silent");
    }
}

// ymfm's ch_output_0/1() take the register OFFSET (0x101/0x102 for the high-bank channels), not the chip's
// channel NUMBER (4/5). A note panned LEFT-only (0xB4 bit7) on FM 3/4 must therefore still be audible.
void Ym2610FmHighBankChannelsHonourPanRegister() {
    std::printf("Ym2610FmHighBankChannelsHonourPanRegister\n");

    struct Case {
        std::uint32_t logicalChannel;  // FM 3 = 2, FM 4 = 3
        std::uint8_t local;            // channel within the high bank (chnum % 3)
        std::uint8_t pan;              // 0xB4 register value: bit7 = left, bit6 = right
        bool expectLeft;
        bool expectRight;
    };
    const Case cases[] = {
        {2, 1, 0x80, true, false}, {2, 1, 0x40, false, true}, {2, 1, 0xC0, true, true},
        {3, 2, 0x80, true, false}, {3, 2, 0x40, false, true}, {3, 2, 0xC0, true, true},
    };
    for (const Case& c : cases) {
        Ym2610DeviceAdapter ym({}, {});
        auto buffers = MakeChannelBuffers();
        auto w = [&](std::uint8_t reg, std::uint8_t value) {
            ym.Write(2, reg);  // high address/data port pair (registers 0x100+)
            ym.Write(3, value);
        };
        w(0xB0 + c.local, 0x07);  // feedback 0, algorithm 7
        w(0xB4 + c.local, c.pan);
        w(0xA4 + c.local, 0x22);
        w(0xA0 + c.local, 0x00);
        for (std::uint8_t o : {std::uint8_t(0), std::uint8_t(4), std::uint8_t(8), std::uint8_t(12)}) {
            w(0x30 + o + c.local, 0x01);
            w(0x40 + o + c.local, 0x00);
            w(0x50 + o + c.local, 0x1F);
            w(0x60 + o + c.local, 0x00);
            w(0x70 + o + c.local, 0x00);
            w(0x80 + o + c.local, 0x00);
        }
        ym.Write(0, 0x28);
        ym.Write(1, 0xF0 | 0x04 | c.local);  // key-on: all operators, high bank, this channel

        ym.Render(2000 * Ym2610DeviceAdapter::kFmAdpcmInputClocksPerSample, buffers);
        Check(ym.FmChannelOutputsLeft(c.logicalChannel) == c.expectLeft,
              "FM high bank: FmChannelOutputsLeft() follows the channel's own 0xB4 pan bit 7");
        Check(ym.FmChannelOutputsRight(c.logicalChannel) == c.expectRight,
              "FM high bank: FmChannelOutputsRight() follows the channel's own 0xB4 pan bit 6");
        Check(PeakOf(buffers, Ym2610DeviceAdapter::kFmBase + c.logicalChannel) > 0.01f,
              "FM high bank: a keyed note is audible whichever side the pan register routes it to");
    }
}

void Ym2610AdpcmAChannelPlaysSampleFromExternalRom() {
    std::printf("Ym2610AdpcmAChannelPlaysSampleFromExternalRom\n");

    Ym2610DeviceAdapter ym(MakeSyntheticAdpcmSamples(64), {});
    auto buffers = MakeChannelBuffers();

    auto wHi = [&](std::uint8_t reg, std::uint8_t value) {
        ym.Write(2, reg);
        ym.Write(3, value);
    };
    // Channel 0 (choffs=0): loudest possible instrument level + pan L+R,
    // global total_level maxed too (both are needed for audible output, see
    // the header comment).
    wHi(0x01, 0x3F);         // total_level (global)
    wHi(0x00 + 0x08, 0xDF);  // choffs 0: pan L+R (0xC0) | instrument_level 0x1F
    wHi(0x00 + 0x10, 0x00);  // start low
    wHi(0x00 + 0x18, 0x00);  // start high
    wHi(0x00 + 0x20, 0x20);  // end low (32 bytes in)
    wHi(0x00 + 0x28, 0x00);  // end high
    wHi(0x00, 0x01);         // key on channel 0 (bit0 set, bit7 clear)

    const std::uint64_t clocks = 4000 * Ym2610DeviceAdapter::kFmAdpcmInputClocksPerSample;
    ym.Render(clocks, buffers);

    Check(PeakOf(buffers, Ym2610DeviceAdapter::kAdpcmABase + 0) > 0.01f,
          "ADPCM-A: keyed channel 0 played a non-silent signal from the synthetic external sample ROM");
    Check(PeakOf(buffers, Ym2610DeviceAdapter::kAdpcmABase + 1) == 0.0f,
          "ADPCM-A: un-keyed channel 1 stayed exactly silent");
}

void Ym2610AdpcmBChannelPlaysSampleFromExternalRom() {
    std::printf("Ym2610AdpcmBChannelPlaysSampleFromExternalRom\n");

    Ym2610DeviceAdapter ym({}, MakeSyntheticAdpcmSamples(64));
    auto buffers = MakeChannelBuffers();

    auto w = [&](std::uint8_t reg, std::uint8_t value) {
        ym.Write(0, reg);
        ym.Write(1, value);
    };
    w(0x12, 0x00);  // start low
    w(0x13, 0x00);  // start high
    w(0x14, 0x20);  // end low (32 bytes in)
    w(0x15, 0x00);  // end high
    w(0x19, 0xFF);  // delta_n low - max rate, guarantees fast advancement
    w(0x1A, 0xFF);  // delta_n high
    w(0x1B, 0xFF);  // level - max
    w(0x11, 0xC0);  // pan L+R (rom_ram bit0 already 0 = ROM at reset)
    w(0x10, 0x80);  // execute=1 (external forced on by ym2610 itself for this address)

    const std::uint64_t clocks = 4000 * Ym2610DeviceAdapter::kFmAdpcmInputClocksPerSample;
    ym.Render(clocks, buffers);

    Check(PeakOf(buffers, Ym2610DeviceAdapter::kAdpcmBBase) > 0.01f,
          "ADPCM-B: the single channel played a non-silent signal from the synthetic external sample ROM");
}

void Ym2610SsgChannelOutputReachesPerChannelBuffers() {
    std::printf("Ym2610SsgChannelOutputReachesPerChannelBuffers\n");

    Ym2610DeviceAdapter ym({}, {});
    auto buffers = MakeChannelBuffers();

    ProgramLoudSsgNote(ym);

    const std::uint64_t clocks = 4000 * Ym2610DeviceAdapter::kSsgInputClocksPerSample;
    ym.Render(clocks, buffers);
    Check(ym.GeneratedSsgSampleCount() == 4000, "SSG channel output: Render() produced one SSG sample per 32 input clocks");

    Check(PeakOf(buffers, Ym2610DeviceAdapter::kSsgBase + 0) > 0.01f,
          "SSG channel output: keyed SSG channel A produced a non-silent signal");
    for (std::uint32_t ch = 1; ch < Ym2610DeviceAdapter::kSsgChannelCount; ++ch) {
        Check(PeakOf(buffers, Ym2610DeviceAdapter::kSsgBase + ch) == 0.0f,
              "SSG channel output: untouched SSG channel stayed exactly silent");
    }
}

void Ym2610RenderIsDeterministicAndLosesNoChipTime() {
    std::printf("Ym2610RenderIsDeterministicAndLosesNoChipTime\n");

    auto run = [](std::vector<float>& out) {
        Ym2610DeviceAdapter ym({}, {});
        auto buffers = MakeChannelBuffers();
        ProgramLoudFmNoteOnLogicalChannel0(ym);
        // Same total chip time, fed in uneven chunks that don't land on 144:
        // the partial-clock carry must reproduce the single-call result.
        for (std::uint64_t chunk : {std::uint64_t{100}, std::uint64_t{33}, std::uint64_t{4000}, std::uint64_t{7},
                                    std::uint64_t{1860}}) {
            ym.Render(chunk, buffers);
        }
        out = buffers[Ym2610DeviceAdapter::kFmBase + 0];
    };

    std::vector<float> a;
    std::vector<float> b;
    run(a);
    run(b);
    Check(!a.empty() && a == b, "determinism: identical register + timing sequence yields identical FM samples");
}
