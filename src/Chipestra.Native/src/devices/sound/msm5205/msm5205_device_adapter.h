// license:BSD-3-Clause
// copyright-holders:Aaron Giles
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/sound/msm5205.cpp
//   src/devices/sound/msm5205.h
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#pragma once

#include "clock.h"
#include "device_manager.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

// Port of MAME's msm5205_device (OKI MSM5205, a streaming ADPCM decoder); see
// THIRD-PARTY-NOTICES.md. No MAME type appears in this header or the .cpp.
// The MAME source is tightly coupled to device_t/emu_timer, so the small,
// self-contained ADPCM decode algorithm is reimplemented directly.
//
// Simplification vs. MAME's two-timer model (a VCK timer toggling every
// half-period plus a capture timer firing a few clocks after VCK's falling
// edge): the exact VCK pin level only matters if something polls it
// mid-period, which no supported board does. This adapter collapses both
// timers into one "one output sample = kInputClocksPerSample input clocks"
// cadence (the chip's real sample rate, clock/prescaler), firing the VCK
// handler once per sample immediately before that sample's nibble is decoded
// (MAME's update_adpcm() order: callback first, decode second).
//
// Two fixed configurations, chosen at construction:
//  - Mode::kMasterS48 (S48_4B): prescaler /48, 4-bit ADPCM nibbles. The chip
//    free-runs at 8 kHz (see above).
//  - Mode::kSlave (SEX_4B): VCK is an input driven by the board. Each VCK
//    falling edge, delivered through SlaveWrite() together with the data
//    nibble and RESET pin level, decodes one sample 6 input clocks later
//    (MAME's capture timer). The output is a zero-order hold sampled at the
//    input clock (one sample per clock), so the value changes exactly at the
//    time of the write, as in MAME's 384 kHz stream.
// The real chip can change its prescaler at runtime (playmode_w()/s1_w()/
// s2_w()); that is not exposed until a board needs it.
//
// Deterministic time model: never reads a wall clock, only advances a
// chip-input-clock counter inside Render().
class Msm5205DeviceAdapter final : public IDevice {
public:
    // Input clock of the supported board: XTAL(384'000).
    static constexpr ClockRate kInputClockRate{384'000};

    // S48_4B: prescaler /48 (s1=0, s2=1). 384000/48 = 8000 Hz.
    static constexpr std::uint64_t kInputClocksPerSample = 48;
    static constexpr std::uint32_t kOutputSampleRateHz = 384'000 / 48;  // 8000

    // Fires once per output sample, BEFORE that sample's ADPCM nibble is
    // decoded. The board wires this to the ADPCM Z80's NMI line (gated by that
    // Z80's nmi_enable flag); a handler that responds synchronously by calling
    // DataW() feeds the freshest nibble into THIS sample's decode, as the
    // real chip's legacy VCK callback does.
    using VckHandler = std::function<void()>;

    enum class Mode { kMasterS48, kSlave };

    // Output rate of Mode::kSlave: one sample per input clock.
    static constexpr std::uint32_t kSlaveOutputSampleRateHz = 384'000;
    // Input clocks between a VCK falling edge and the ADPCM data capture.
    static constexpr std::uint64_t kSlaveCaptureDelayClocks = 6;

    explicit Msm5205DeviceAdapter(VckHandler vckHandler = nullptr, Mode mode = Mode::kMasterS48);

    // Power-on-equivalent reset (MAME's device_reset()): signal and step are
    // zeroed; the data latch and the reset pin state are NOT touched, as on
    // real hardware (only ResetPin() changes the pin).
    void Reset();

    // RESET pin (reset_w()). While asserted, every decoded sample is silence
    // (signal=0, step=0) regardless of DataW().
    void ResetPin(bool asserted);

    // Data bus latch (data_w()). Only the low 4 bits are used (fixed 4-bit mode).
    void DataW(std::uint8_t value);

    // Mode::kSlave only: a board write of `value` (bit 7 = RESET pin, low 4 bits
    // = data nibble) followed by a VCK pulse, at chip input-clock time
    // `atClock` (counted from construction/Reset(), same time base as the sum
    // of Render() arguments). Write times must not decrease and must not lie
    // before the clocks already rendered. Returns false (and ignores the write)
    // in master mode or on a violated ordering.
    bool SlaveWrite(std::uint64_t atClock, std::uint8_t value);

    // Total input clocks rendered so far (the time base of SlaveWrite()).
    std::uint64_t RenderedClocks() const { return rendered_clocks_; }

    // Advances the chip by `inputClocks` input clocks and appends one mono
    // float sample (signal range -2048..2047 scaled by 1/4096) per
    // kInputClocksPerSample clocks to `samples`. Partial samples carry to the
    // next call. Returns the number of samples appended. In Mode::kSlave one
    // sample is appended per input clock instead (see above).
    std::size_t Render(std::uint64_t inputClocks, std::vector<float>& samples);

    // Test-only observation points - not part of production playback.
    std::int32_t SignalForTest() const { return signal_; }
    std::int32_t StepForTest() const { return step_; }

    std::uint64_t GeneratedSampleCount() const { return generated_samples_; }

private:
    void DecodeOneSample();
    void DecodeOneSampleNoCallback();

    struct SlaveEvent {
        std::uint64_t effective_clock;  // write time + capture delay
        std::uint8_t data;
        bool reset;
    };

    VckHandler vck_handler_;
    Mode mode_;
    std::deque<SlaveEvent> slave_events_;
    std::uint64_t rendered_clocks_ = 0;
    std::uint64_t last_slave_write_clock_ = 0;

    std::uint8_t data_ = 0;
    bool reset_pin_ = false;
    std::int32_t signal_ = 0;  // matches m_signal, range [-2048, 2047]
    std::int32_t step_ = 0;    // matches m_step, range [0, 48]

    std::int32_t diff_lookup_[49 * 16] = {};  // matches m_diff_lookup, computed once at construction

    std::uint64_t partial_clocks_ = 0;
    std::uint64_t generated_samples_ = 0;
};
