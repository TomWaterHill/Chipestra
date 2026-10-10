// license:BSD-3-Clause
// copyright-holders:Couriersud
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/sound/ay8910.cpp
//   src/devices/sound/ay8910.h
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#pragma once

#include "clock.h"
#include "device_manager.h"

#include <cstdint>
#include <functional>
#include <vector>

// Port of MAME's ay8910_device (AY-3-8910 PSG), scoped to the plain AY-3-8910
// configuration - not the generic AY8910/YM2149/AY8930 family MAME's class
// supports. See THIRD-PARTY-NOTICES.md.
// No MAME type appears in this header or the .cpp: the register file, the 3
// tone generators, the noise generator and the envelope generator are owned
// here, and the chip is driven by a plain Render() call.
//
// Simplifications versus the full MAME class:
// - Only the PSG_TYPE_AY / non-"expanded mode" code paths are ported. AY8930
//   expanded mode, YM2149 (different DAC constants and envelope step rate),
//   and AY8914's tone_envelope field are not carried over.
// - A plain AY-3-8910 has exactly one envelope generator; MAME's extra two
//   serve only AY8930 expanded mode (registers 0x10-0x15, unreachable here).
//   Only one is modeled.
// - Some boards sum a chip's 3 tone generators into one mono output through
//   an analog resistor network (MAME's AY8910_SINGLE_OUTPUT). The adapter
//   always exposes 3 independent channels (stereo/mixing is expressed through
//   LogicalChannel routing, not baked into the waveform); the owning
//   Machine's RoutingGain accounts for the analog summing.
//
// Deterministic time model: never reads a wall clock. Playback advances a
// per-instance input-clock counter only inside Render(); one output sample
// is kInputClocksPerSample input clocks (MAME's `master_clock / 8` stream).
//
// Implements IDevice so a Machine's DeviceManager owns it. Register access
// (`WriteAddress`/`WriteData`/`ReadData`) is driven directly by the owning
// Machine's bus handlers, since address-select and data access sit at
// separate I/O addresses per chip instance.
class Ay8910DeviceAdapter final : public IDevice {
public:
    static constexpr std::uint32_t kChannelCount = 3;

    // device_start(): `stream_alloc(0, streams, master_clock / 8)`.
    static constexpr std::uint32_t kInputClocksPerSample = 8;

    // Called when the AY_ENABLE register's port-A/B direction bit toggles to
    // output and (on that same edge, or on a later PORTA/PORTB register
    // write while already in output mode) a value is latched out. Unset callbacks are a no-op.
    using PortWriteFn = std::function<void(std::uint8_t value)>;

    // Called whenever the currently-selected register is PORTA/PORTB and
    // ReadData() is invoked - unconditionally, regardless of the AY_ENABLE
    // direction bit (as in MAME: even a port configured as output returns
    // live external data).
    using PortReadFn = std::function<std::uint8_t()>;

    // loadResistanceOhms: the board's external per-channel load resistor
    // (MAME's `set_resistors_load()`), part of the resistor-ladder DAC model.
    // Defaults to MAME's built-in 1000 ohms. A board that sets a different
    // value in MAME must pass it here; keeping 1k for a 3.3k board
    // understates the output level by roughly 2.7-2.9x.
    explicit Ay8910DeviceAdapter(PortWriteFn portAWrite = nullptr, PortWriteFn portBWrite = nullptr,
                                  PortReadFn portARead = nullptr, PortReadFn portBRead = nullptr,
                                  float loadResistanceOhms = 1000.0f);

    // Opt-in emulation of MAME's `AY8910_LEGACY_OUTPUT | AY8910_SINGLE_OUTPUT`
    // output mode: the 3 tone generators are summed
    // through one shared resistor network (mix_3D()/build_3D_table()) and the
    // result is normalised so silence is 0 (no DC offset) and all three
    // channels at full volume reach 3.0. The exact combined value is then
    // split across the 3 output channels in proportion to each channel's
    // conductance, so the sum of the 3 channels equals MAME's single stream.
    // Default off: the per-channel tables stay un-normalised.
    void SetLegacyNormalizedMixOutput(bool enabled) { legacy_normalized_mix_ = enabled; }

    // MAME's ay8910_device defaults to AY8910_LEGACY_OUTPUT, so a chip that never calls
    // set_flags() also uses normalised tables, but with 3 separate streams:
    // build_single_table(normalize=1) = ((t-min)/(max-min) - 0.25) * 0.5, i.e. a full-volume
    // swing of 0.5 (vs 0.24 for the raw resistor-ladder table). The DC part (-0.125 at
    // level 0) is dropped. Default off (AY8910_DISCRETE_OUTPUT boards keep the raw tables).
    void SetLegacyNormalizedSeparateOutput(bool enabled) { legacy_normalized_separate_ = enabled; }

    // Power-on-equivalent reset: register file, tone/noise/envelope
    // generators, and the register-select latch all cleared.
    void Reset();

    // Register-select (BC1=0) and data (BC1=1) bus writes, matching MAME's
    // address_w()/data_w() (ay8910_write_ym(0, ...)/(1, ...)). A register
    // number above 0x0F (the 4-bit GI-bus address mask) deactivates the
    // latch - matching real hardware, subsequent WriteData() calls are then
    // ignored until a valid WriteAddress() re-activates it.
    void WriteAddress(std::uint8_t value);
    void WriteData(std::uint8_t value);

    // Reads back the currently-latched register (data_r()/ay8910_read_ym()).
    // Returns 0xFF (high impedance) while the latch is inactive.
    std::uint8_t ReadData();

    // Advances the chip by `inputClocks` input clocks and appends each of
    // the 3 channels' mono float sample (in [-1, 1], the un-normalized
    // resistor-ladder DAC model) to `channelSamples` (a kChannelCount-long vector of
    // buffers). Partial samples (< kInputClocksPerSample clocks) carry to
    // the next call. Returns the number of output samples appended to each
    // buffer.
    std::size_t Render(std::uint64_t inputClocks, std::vector<std::vector<float>>& channelSamples);

    // Test-only observation points - not part of production playback.
    std::uint8_t RegisterForTest(std::uint8_t reg) const { return regs_[reg & 0x0F]; }
    bool ToneOutputForTest(std::uint32_t channel) const { return tone_[channel].output != 0; }
    std::uint32_t ToneCountForTest(std::uint32_t channel) const { return static_cast<std::uint32_t>(tone_[channel].count); }
    std::uint32_t TonePeriodForTest(std::uint32_t channel) const { return tone_[channel].period; }
    std::uint8_t NoisePeriodForTest() const { return NoisePeriod(); }
    std::uint32_t NoiseRngForTest() const { return rng_; }
    bool NoiseOutputForTest() const { return noise_out_ != 0; }
    std::uint32_t EnvelopePeriodForTest() const { return envelope_.period; }
    std::uint32_t EnvelopeVolumeForTest() const { return envelope_.volume; }
    std::int32_t EnvelopeStepForTest() const { return envelope_.step; }

private:
    // Register indices, matching MAME's AY_* enum (ay8910.h).
    enum RegisterIndex : std::uint8_t {
        kRegAFine = 0x00,
        kRegACoarse = 0x01,
        kRegBFine = 0x02,
        kRegBCoarse = 0x03,
        kRegCFine = 0x04,
        kRegCCoarse = 0x05,
        kRegNoisePeriod = 0x06,
        kRegEnable = 0x07,
        kRegAVol = 0x08,
        kRegBVol = 0x09,
        kRegCVol = 0x0A,
        kRegEnvFine = 0x0B,
        kRegEnvCoarse = 0x0C,
        kRegEnvShape = 0x0D,
        kRegPortA = 0x0E,
        kRegPortB = 0x0F,
    };

    // One tone generator (A/B/C). `output` toggles each time `count` reaches
    // `period` - equivalent to MAME's 5-bit `duty_cycle` down-counter whose
    // LSB (`BIT(duty_cycle, 0)`) is used as the output in non-expanded mode:
    // decrementing a counter by 1 flips its LSB every step, so a plain
    // toggle produces the identical bit sequence without carrying the
    // now-dead expanded-mode duty-cycle table lookup.
    struct Tone {
        std::uint32_t period = 0;
        std::uint8_t volume = 0;
        std::int32_t count = 0;
        std::uint8_t output = 0;
    };

    // The chip's single envelope generator.
    struct Envelope {
        std::uint32_t period = 0;
        std::int32_t count = 0;
        std::int8_t step = 0;
        std::uint32_t volume = 0;
        std::uint8_t hold = 0;
        std::uint8_t alternate = 0;
        std::uint8_t attack = 0;
        std::uint8_t holding = 0;

        void SetPeriod(std::uint8_t fine, std::uint8_t coarse) {
            period = static_cast<std::uint32_t>(fine) | (static_cast<std::uint32_t>(coarse) << 8);
        }
        void SetShape(std::uint8_t shape) {
            constexpr std::uint8_t kStepMask = 0x0F;  // AY-3-8910: 16-step envelope
            attack = (shape & 0x04) ? kStepMask : 0x00;
            if ((shape & 0x08) == 0) {
                hold = 1;
                alternate = attack;
            } else {
                hold = shape & 0x01;
                alternate = shape & 0x02;
            }
            step = static_cast<std::int8_t>(kStepMask);
            holding = 0;
            volume = static_cast<std::uint32_t>(step) ^ attack;
        }
    };

    void WriteRegister(std::uint8_t reg, std::uint8_t value);
    void TickOneSample();
    void NoiseRngTick();
    bool ToneEnabled(std::uint32_t channel) const;   // AY_ENABLE bit (channel): 1 = tone disabled
    bool NoiseEnabled(std::uint32_t channel) const;  // AY_ENABLE bit (3+channel): 1 = noise disabled
    std::uint8_t NoisePeriod() const;
    void BuildVolumeTables();
    void RenderLegacyNormalizedSample(std::vector<std::vector<float>>& channelSamples);

    PortWriteFn port_a_write_;
    PortWriteFn port_b_write_;
    PortReadFn port_a_read_;
    PortReadFn port_b_read_;
    float load_resistance_ohms_ = 1000.0f;

    std::uint8_t regs_[16] = {};
    std::uint8_t register_latch_ = 0;
    bool active_ = false;
    std::uint8_t last_enable_ = 0;

    Tone tone_[kChannelCount]{};
    Envelope envelope_{};

    std::uint32_t rng_ = 1;
    std::uint8_t noise_out_ = 0;
    std::int32_t count_noise_ = 0;
    std::uint8_t prescale_noise_ = 0;
    bool vol_enabled_[kChannelCount] = {};

    // Per-channel DAC curve for the fixed-volume (0-15) and envelope-volume
    // (0-15) cases, built by BuildVolumeTables() (the model has no chip-state
    // dependency).
    float volume_table_[kChannelCount][16]{};
    float envelope_table_[16]{};
    bool legacy_normalized_mix_ = false;
    bool legacy_normalized_separate_ = false;

    std::uint64_t partial_input_clocks_ = 0;
};
