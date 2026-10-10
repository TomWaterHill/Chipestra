// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "clock.h"
#include "device_manager.h"
#include "devices/cpu/i8039/mame_derived_i8039_adapter.h"
#include "devices/cpu/z80/mame_derived_z80_adapter.h"
#include "devices/sound/ay8910/ay8910_device_adapter.h"
#include "gyruss_ay_rc_filter.h"
#include "gyruss_command_interface.h"
#include "gyruss_mixer_lowpass.h"
#include "gyruss_sound_memory_map.h"
#include "chipestra_abi.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

// The Konami Gyruss sound subsystem: sound Z80 + i8039 + AY-3-8910 x5 (five
// independent Ay8910DeviceAdapter instances) + a software-driven DAC (the
// i8039's own Port 1 output; there is no dedicated DAC chip on the board).
//
// The command latch is a plain soundlatch plus a one-instruction Z80 IM1 pulse
// (no handshake protocol). The pulse is deasserted from Render()'s own loop,
// never from inside the interrupt-servicing path (see GradiusCommandInterface).
// The i8039's external IRQ is level-triggered, asserted by the Z80 writing its
// `i8039_irq_w` port (0x14) and cleared by the i8039's own ISR writing Port 2,
// so it needs no Machine-level deassert logic.
//
// The two CPUs are advanced to the same target tick once per Render() block.
// That is enough because the soundlatch2/i8039_irq_w handoff is a one-shot
// trigger that neither CPU polls. The i8039's Port 1 (DAC) writes are captured
// at write granularity via an observer, independent of the coarser CPU sync.
//
// LogicalChannels: ay1..ay5's 3 tone generators each (ids 0-2, 3-5, 6-8, 9-11,
// 12-14), then the DAC (id 15).
class GyrussMachine final : public IMachine {
public:
    // Sound Z80 clock: SOUND_CLOCK/4 = 14'318'181/4 = 3,579,545.25Hz, rounded.
    static constexpr ClockRate kZ80ClockRate{3'579'545};

    // AY-3-8910 x5 clock: SOUND_CLOCK/8 = 1,789,772.625Hz, rounded. Exactly half
    // kZ80ClockRate (both are SOUND_CLOCK divided by powers of 2), which
    // SyncSoundDevices() uses directly instead of a separate ClockConverter.
    static constexpr ClockRate kAy8910ClockRate{1'789'773};

    // i8039 clock: a separate 8 MHz oscillator, not derived from SOUND_CLOCK, so
    // it needs its own ClockConverter.
    static constexpr ClockRate kI8039ClockRate{8'000'000};

    // A command can arrive before the first real Render() call, so the driver's
    // boot sequence is run during Initialize()/Reset(). Gyruss has no shared-RAM
    // boot handshake, so a flat budget is sufficient.
    static constexpr std::uint64_t kBootWarmupFrames = 256ull * 24;

    // Per-chip AY8910 routing gains. The values come from the DISCRETE mixer
    // described in MAME's src/mame/konami/gyruss.cpp (konami_right_mixer_desc /
    // konami_left_mixer_desc, sound_discrete, and the ay1..ay5 routes):
    //   - the chip outputs reach the mixer through resistors: ay1 and ay2 one
    //     2.2kOhm input per channel (after the switchable RC low-pass, which is
    //     implemented separately in GyrussAyRcFilter and applied before these gains);
    //     ay3, ay4 and ay5 three channels summed after a 0.33 scale into one
    //     3.3kOhm/3 = 1.1kOhm input per chip;
    //   - each bus is a resistor mixer with a 200Ohm feedback resistor, so an
    //     input's weight is (1/r) / (1/200 + sum of 1/r of that bus's inputs);
    //     the right bus has ay1 (3 inputs) + ay3 + ay4, the left bus has
    //     ay2 (3 inputs) + ay5 + the DAC (4.7kOhm);
    //   - the output stage multiplies by 11 (DISCRETE_OUTPUT), and the AY
    //     stream samples are scaled by 32768 on the way in, so a channel
    //     waveform in this project's [0..~0.585] AY units needs
    //     11 * weight * (0.33 for ay3/4/5) as its gain.
    // Right bus (ay1, ay3, ay4) and left bus (ay2, ay5, DAC) are separate
    // buses with no crossfeed; their weights differ slightly because the left
    // bus has one more, lightly loaded input.
    static constexpr float kAy1RoutingGain = 0.611111f;   // right: 11 * (1/2.2k) / 0.008182
    static constexpr float kAy2RoutingGain = 0.667959f;   // left:  11 * (1/2.2k) / 0.007486
    static constexpr float kAy34RoutingGain = 0.403333f;  // right: 11 * 0.33 * (1/1.1k) / 0.008182
    static constexpr float kAy5RoutingGain = 0.440853f;   // left:  11 * 0.33 * (1/1.1k) / 0.007486

    // The board's AY8910 load resistor: MAME's gyruss.cpp calls
    // `set_resistors_load(RES_K(3.3), RES_K(3.3), RES_K(3.3))` on all 5 chips.
    // This differs from Ay8910DeviceAdapter's default (MAME's built-in 1kOhm);
    // using the default would make the AYs roughly 2.7-2.9x too quiet.
    static constexpr float kAy8910LoadResistanceOhms = 3300.0f;

    // DAC routing gain (id 15), left bus, 4.7kOhm input. MAME feeds the 8-bit
    // Port 1 value into the mixer as value * 204.8 (INPUTX_DATA gain
    // 256 * 4 / 5), which is 0.00625 per code once the 32768 stream scale is
    // divided out - the same per-code step as OnDacWrite() below ((code - 128)
    // / 128 * kDacFullScale). With the same weight/output-stage calculation as
    // the AY gains above, the circuit description gives
    //   11 * (1/4.7k) / 0.007486 = 0.3127.
    // Measured against MAME's own rendering of the same songs (14s left/right
    // RMS of "Main BGM 1/2/3"), 0.3127 leaves the left channel about 3 dB quiet,
    // while about 0.438 matches it. The source of the extra factor (about 1.4) in
    // MAME's DAC path was not found, so the value is adjusted to match MAME's
    // measured output level rather than taken from the circuit calculation.
    static constexpr float kDacRoutingGain = 0.438f;
    // 0.0, 0.0)`, i.e. an 8-bit unsigned sample scaled to a ~4V swing against a
    // notional 5V full scale. Converted to [-1,1] as
    // `(sample - 128) / 128.0f * (4.0f/5.0f)`.
    static constexpr float kDacFullScale = 4.0f / 5.0f;

    // DC-blocking one-pole filter coefficient (y[n] = x[n] - x[n-1] + R*y[n-1])
    // applied to every LogicalChannel at that channel's own sample rate. The cutoff is
    // about rate*(1-R)/(2*pi): ~30Hz for the DAC (192kHz), ~36Hz for the AY8910s
    // (~223.7kHz). It passes audio but removes near-DC bias (the DAC's offset and the
    // AY8910 volume-0 DC bias).
    static constexpr float kDcBlockR = 0.999f;

    // `z80_rom` is loaded into the sound Z80's ROM region; `i8039_rom` into the
    // i8039's external program ROM (0x000-0xFFF).
    GyrussMachine(std::vector<std::uint8_t> z80_rom, std::vector<std::uint8_t> i8039_rom);
    ~GyrussMachine() override;

    void Initialize() override;
    void Reset() override;
    void Shutdown() override;
    std::vector<LogicalChannel>& LogicalChannels() override;
    void Render(EmuDuration duration, LogicalChannelSink* sink) override;
    void SendCommand(const MusicCommand& command) override;

    // Pan is set once per channel in Initialize() (hard L/R) and never revisited,
    // so this restores it when a UI override is cleared.
    void OnPanOverrideCleared(std::uint32_t channelId) override;

    // Test-only observation points (not part of IMachine).
    MameDerivedZ80Adapter::RegisterSnapshot Z80Snapshot() const;
    MameDerivedI8039Adapter::RegisterSnapshot I8039Snapshot() const;
    std::uint8_t ReadZ80Memory(std::uint16_t address) const;

    // Drains and resets the i8039 Port 1 (DAC) write count and min/max raw byte
    // seen since the last call.
    struct DacWriteStatsForTest {
        std::uint64_t count = 0;
        std::uint8_t minRaw = 255;
        std::uint8_t maxRaw = 0;
    };
    DacWriteStatsForTest DrainDacWriteStatsForTest();
    MameDerivedZ80Adapter& Z80ForTest() { return *z80_; }
    MameDerivedI8039Adapter& I8039ForTest() { return *i8039_; }
    Ay8910DeviceAdapter& Ay8910ForTest(int index) { return *ay_[static_cast<std::size_t>(index)]; }
    // ay1/ay2 channel `channel` (0-2): the RC filter's current switch setting
    // (bit 0 = 0.047 uF, bit 1 = 0.22 uF), and every distinct Port B value the
    // chip has been given since construction/Reset().
    std::uint8_t AyFilterSwitchesForTest(int chip, int channel) const {
        return ay_rc_filters_[static_cast<std::size_t>(chip)][static_cast<std::size_t>(channel)].Switches();
    }
    const std::vector<std::uint8_t>& AyPortBValuesForTest(int chip) const {
        return ay_port_b_values_[static_cast<std::size_t>(chip)];
    }

private:
    class IoBus;  // Z80-side: soundlatch read, ay1-5 register access, i8039 IRQ trigger, soundlatch2 write.

    void SyncSoundDevices(std::uint64_t z80CycleMark);
    std::uint64_t CurrentBlockCycles() const { return block_base_cycles_ + z80_->CyclesConsumedThisCall(); }

    void RunBootWarmup();
    static constexpr std::uint64_t kWarmupChunkFrames = 256;

    // DC-blocking one-pole filter state, one per source signal.
    struct DcBlockState {
        float prevInput = 0.0f;
        float prevOutput = 0.0f;
    };
    // Applies the kDcBlockR one-pole DC-blocking filter to `buffer` in place.
    static void ApplyDcBlock(std::vector<float>& buffer, DcBlockState& state);

    // Port 1 write observer on the i8039 (DAC capture).
    void OnDacWrite(std::uint8_t value);
    // Port 2 write observer on the i8039 (clears its own external IRQ line).
    void OnI8039Port2Write(std::uint8_t value);

    std::vector<std::uint8_t> z80_rom_;
    std::vector<std::uint8_t> i8039_rom_;
    GyrussSoundMemoryMap sound_memory_map_;
    DeviceManager device_manager_;
    MameDerivedZ80Adapter* z80_ = nullptr;
    MameDerivedI8039Adapter* i8039_ = nullptr;
    std::array<Ay8910DeviceAdapter*, 5> ay_{};
    std::unique_ptr<GyrussCommandInterface> command_interface_;
    std::unique_ptr<IoBus> io_bus_;

    ClockConverter clock_converter_;        // canonical ticks -> Z80 cycles
    ClockConverter i8039_clock_converter_;  // canonical ticks -> i8039 cycles

    std::uint64_t cumulative_ticks_processed_ = 0;
    std::uint64_t cumulative_cycles_executed_ = 0;         // Z80
    std::uint64_t i8039_cumulative_cycles_executed_ = 0;

    std::uint64_t sound_sync_cursor_ = 0;  // Z80-cycle mark ay_[] have been advanced to, within the current block
    std::uint64_t block_base_cycles_ = 0;  // Z80 cycles consumed by prior Execute() calls within the current block
    bool irq_awaiting_ack_ = false;        // command-latch IRQ asserted, deassert pending

    // Soundlatch2 (Z80 -> i8039) and the i8039's IRQ line state: plain wiring
    // state, not device state.
    std::uint8_t soundlatch2_ = 0;
    bool i8039_irq_asserted_ = false;

    // Current DAC output sample ([-1,1], already scaled), updated by
    // OnDacWrite(). Holds the last write of the block that just ran, so it is
    // not what Render() uses to fill the block (see dac_write_events_).
    float dac_current_value_ = 0.0f;

    // The DAC value at the end of the previous block: what ticks before this
    // block's first Port 1 write must hold (dac_current_value_ is overwritten
    // mid-block by OnDacWrite()).
    float dac_hold_before_block_ = 0.0f;

    // Every i8039 Port 1 (DAC) write seen during the current block's Execute()
    // call, in order, as (cycles into that call, sample). A block typically
    // contains 5-24 writes, so Render() replays each at its own offset instead
    // of holding one value across the block. Cleared at the top of every
    // Render() call, including sink-less warm-up calls.
    struct DacWriteEvent {
        std::uint64_t i8039CyclesInBlock = 0;
        float sample = 0.0f;
    };
    std::vector<DacWriteEvent> dac_write_events_;

    // DC-blocking filter state, one per LogicalChannel (ids 0-14 = the 5 AY8910s'
    // 3 channels each, id 15 = the DAC). The board applies its DC removal
    // capacitor at the combined mixer stage; because the filter and RoutingGain
    // are linear, filtering each source channel with the same coefficient before
    // the generic Mixer sums them is equivalent. The AY8910 volume-0 DC bias
    // makes this necessary on the AY channels as well as the DAC.
    std::array<DcBlockState, 16> dc_block_state_{};

    // Backing counters for DrainDacWriteStatsForTest().
    std::uint64_t dac_write_count_debug_ = 0;
    std::uint8_t dac_write_min_debug_ = 255;
    std::uint8_t dac_write_max_debug_ = 0;

    std::vector<LogicalChannel> channels_;
    bool channels_registered_ = false;
    std::array<std::vector<std::vector<float>>, 5> ay_channel_scratch_;

    // ay1/ay2 only: the per-channel RC low-pass between the chip and the mixer
    // (gyruss_ay_rc_filter.h), run at the AY output rate before RoutingGain.
    // Port B selects the capacitor per channel: bits 0-1 = A, 2-3 = B, 4-5 = C.
    static constexpr double kAyOutputRateHz =
        static_cast<double>(kAy8910ClockRate.hz) / static_cast<double>(Ay8910DeviceAdapter::kInputClocksPerSample);
    void OnAyPortBWrite(int chip, std::uint8_t value);

    // Mixer-output low-pass (gyruss_mixer_lowpass.h), one per LogicalChannel, run
    // at that channel's own sample rate (AY: kAyOutputRateHz, DAC: 192 kHz).
    // Right bus = ay1/ay3/ay4, left bus = ay2/ay5/DAC.
    std::array<GyrussMixerLowPass, 16> mixer_lpf_{};
    std::array<std::array<GyrussAyRcFilter, 3>, 2> ay_rc_filters_{{
        {GyrussAyRcFilter(kAyOutputRateHz), GyrussAyRcFilter(kAyOutputRateHz), GyrussAyRcFilter(kAyOutputRateHz)},
        {GyrussAyRcFilter(kAyOutputRateHz), GyrussAyRcFilter(kAyOutputRateHz), GyrussAyRcFilter(kAyOutputRateHz)}}};
    std::array<std::vector<std::uint8_t>, 2> ay_port_b_values_;
    std::vector<float> dac_channel_scratch_;
};
