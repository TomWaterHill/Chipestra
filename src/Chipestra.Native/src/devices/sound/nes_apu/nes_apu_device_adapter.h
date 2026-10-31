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

#pragma once

#include "device_manager.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

// Port of MAME's nesapu_device / apu2a03_device: the sound generator inside the Ricoh RP2A03/RP2A03G
// (two pulse channels, triangle, noise, DMC), driven by the CPU's clock. See THIRD-PARTY-NOTICES.md.
// No MAME type appears here.
//
// Time model: the chip is advanced by CPU cycles, never by a wall clock. One output sample is produced
// every kCpuCyclesPerSample (4) CPU cycles, i.e. at CPU clock / 4 (about 447 kHz on NTSC), like the
// original's sound stream. A Machine calls Render() up to the CPU's current cycle before every register
// access (the original updates its stream at the start of every read and write); Render() also runs the
// frame counter, which asserts the IRQ line every frame period (29830 cycles NTSC, 33254 PAL) while the
// frame IRQ is enabled. Only the 4-step sequence exists, as in the original.
//
// Output: the original mixes the five channels with two non-linear look-up tables (the two pulse
// channels together; triangle, noise and DMC together). Render() appends, for each channel, that
// channel's share of the mixed value, so the five buffers add up to exactly what the original produces
// and a Machine can show each channel in its own meter. A group's value is split in proportion to its
// channels' own linear weights (pulse 1 : pulse 2 by level; triangle : noise : DMC by level / 8227,
// 12241, 22638, the same weights the look-up table is built from). All outputs lie in [0, 1) with the
// original's DC offset (no high-pass filter).
//
// The DMC reads sample bytes through a callback and, as in the original, costs the CPU no cycles.
class NesApuDeviceAdapter final : public IDevice {
public:
    static constexpr std::uint32_t kCpuCyclesPerSample = 4;
    static constexpr std::size_t kChannelCount = 5;

    enum Channel : std::size_t { kPulse1 = 0, kPulse2, kTriangle, kNoise, kDmc };

    // Which noise generator the original's two device types use: the RP2A03G (the NES_APU device, the one
    // MAME's Famicom/NES drivers use) selects a short 93-step sequence with bit 7 of the noise period
    // register; the RP2A03 (APU_2A03) always runs the long sequence.
    enum class Variant { kRp2a03g, kRp2a03 };

    using MemoryReadFn = std::function<std::uint8_t(std::uint16_t address)>;
    using IrqFn = std::function<void(bool asserted)>;

    explicit NesApuDeviceAdapter(bool pal = false, Variant variant = Variant::kRp2a03g);

    void SetMemoryReadCallback(MemoryReadFn read) { memoryRead_ = std::move(read); }
    void SetIrqCallback(IrqFn irq) { irq_ = std::move(irq); }

    // Power-on state, then the original's device_reset(): all channels off ($4015 = 0) and the frame timer
    // started with the frame IRQ enabled.
    void Reset();

    // Register write; `offset` is the register number from $4000 (0x00-0x13, 0x15, 0x17). Other offsets are
    // ignored.
    void Write(std::uint8_t offset, std::uint8_t value);

    // $4015 read: channel length/DMC status and the frame and DMC IRQ flags; clears the frame IRQ flag
    // (and the IRQ line when the DMC flag is not set).
    std::uint8_t ReadStatus();

    // Channels whose bit is set (1 << Channel) are left out of the mix: the mixer is calculated again from the
    // other channels' levels (not by subtracting a share), so the result is what the original would give for
    // those channels alone. The muted channels' own buffers still carry their share of the *unmuted* mix, for
    // the level meters; whoever sums the buffers must not add them (the Mixer's mute does that).
    void SetMutedChannels(std::uint8_t mask) { muted_mask_ = mask; }

    // Advances the chip by `cpuCycles` CPU cycles and appends one value per completed output sample to each
    // of the kChannelCount vectors in `channelOut` (see the class comment). Partial samples carry over to
    // the next call. Returns the number of samples appended to each vector.
    std::size_t Render(std::uint64_t cpuCycles, std::array<std::vector<float>, kChannelCount>& channelOut);

    // Test-only observation points - not part of production playback.
    std::uint8_t ChannelLevelForTest(Channel channel) const;
    bool FrameIrqPendingForTest() const { return apu_.frame_irq_occurred; }
    bool FrameIrqEnabledForTest() const { return apu_.frame_irq_enabled; }
    bool DmcIrqPendingForTest() const { return apu_.dpcm.irq_occurred; }
    bool DmcEnabledForTest() const { return apu_.dpcm.enabled; }
    std::int32_t LengthForTest(Channel channel) const;
    std::uint64_t FrameCountdownForTest() const { return frame_countdown_; }

private:
    struct Square {
        std::uint8_t regs[4] = {};
        std::int32_t vbl_length = 0;
        std::int32_t freq = 0;
        float phaseacc = 0.0f;
        float env_phase = 0.0f;
        float sweep_phase = 0.0f;
        std::uint8_t adder = 0;
        std::uint8_t env_vol = 0;
        bool enabled = false;
        std::uint8_t output = 0;
    };
    struct Triangle {
        std::uint8_t regs[4] = {};  // regs[1] unused
        std::int32_t linear_length = 0;
        bool linear_reload = false;
        std::int32_t vbl_length = 0;
        std::int32_t write_latency = 0;
        float phaseacc = 0.0f;
        std::uint8_t adder = 0;
        bool counter_started = false;
        bool enabled = false;
        std::uint8_t output = 0;
    };
    struct Noise {
        std::uint8_t regs[4] = {};  // regs[1] unused
        std::uint16_t lfsr = 1;
        std::int32_t vbl_length = 0;
        float phaseacc = 0.0f;
        float env_phase = 0.0f;
        std::uint8_t env_vol = 0;
        bool enabled = false;
        std::uint8_t output = 0;
    };
    struct Dpcm {
        std::uint8_t regs[4] = {};
        std::uint32_t address = 0;
        std::uint32_t length = 0;
        std::int32_t bits_left = 0;
        float phaseacc = 0.0f;
        std::uint8_t cur_byte = 0;
        bool enabled = false;
        bool irq_occurred = false;
        std::int16_t vol = 0;
        std::uint8_t output = 0;
    };
    struct Apu {
        Square squ[2];
        Triangle tri;
        Noise noi;
        Dpcm dpcm;
        std::uint8_t step_mode = 0;
        bool frame_irq_enabled = false;
        bool frame_irq_occurred = false;
    };

    static constexpr unsigned kSyncsMax1 = 0x20;
    static constexpr unsigned kSyncsMax2 = 0x80;

    void CalculateRates();
    void FrameTimerFired();
    void SetIrq(bool asserted) {
        if (irq_) irq_(asserted);
    }
    void ProduceSample(std::array<std::vector<float>, kChannelCount>& channelOut);
    void MixShares(const unsigned (&levels)[kChannelCount], float (&shares)[kChannelCount]) const;

    void ApuSquare(Square& chan);
    void ApuTriangle(Triangle& chan);
    void ApuNoise(Noise& chan);
    void ApuDpcm(Dpcm& chan);
    void UpdateLfsr(Noise& chan) const;
    static void DpcmReset(Dpcm& chan);

    Apu apu_;
    bool is_pal_;
    Variant variant_;
    std::uint32_t samps_per_sync_ = 0;
    std::uint32_t vbl_times_[kSyncsMax1] = {};
    std::uint32_t sync_times1_[kSyncsMax1] = {};
    std::uint32_t sync_times2_[kSyncsMax2] = {};
    float square_lut_[31] = {};
    float tnd_lut_[16][16][128];

    std::uint16_t frame_clocks_ = 0;
    bool frame_timer_active_ = false;
    std::uint64_t frame_countdown_ = 0;  // CPU cycles until the next frame-timer expiry
    std::uint32_t cycles_into_sample_ = 0;

    std::uint8_t muted_mask_ = 0;

    MemoryReadFn memoryRead_;
    IrqFn irq_;
};
