// license:BSD-3-Clause
// copyright-holders:Tatsuyuki Satoh
// copyright-holders:Frank Palazzolo, Couriersud, Jonathan Gevaryahu
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/sound/vlm5030.cpp  [copyright-holders:Tatsuyuki Satoh]
//   src/devices/sound/vlm5030.h  [copyright-holders:Tatsuyuki Satoh]
//   src/devices/sound/tms5110r.hxx  [copyright-holders:Frank Palazzolo, Couriersud, Jonathan Gevaryahu]
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#pragma once

#include "device_manager.h"

#include <cstdint>
#include <functional>
#include <vector>

// Chipestra-owned port of MAME's vlm5030_device (Sanyo VLM5030, a TMS5100-family LPC vocoder), including
// its coefficient tables (vlm5030_coeff in tms5110r.hxx); see THIRD-PARTY-NOTICES.md. No MAME type appears
// in this header or the .cpp; the chip is driven by a plain Render() call like every other adapter here.
//
// Pin-level interface: DataW()/RstPin()/VcuPin()/StPin() are inputs and BusyPin() is the output the driver
// polls. RST and ST act on specific pin transitions, not levels; VCU is a plain level.
//
// Voice data source: Gradius's sound Z80 writes speech data into its own shared RAM ("voiceram",
// 0x8000-0x87FF), which the chip's address space aliases 1:1. This adapter therefore takes a read callback
// into that RAM instead of owning a fixed ROM image.
//
// Deliberate adaptation: MAME generates the unvoiced excitation with machine().rand() (a placeholder, per
// its own TODO). A wall-clock-seeded PRNG would break deterministic emulation, so this adapter uses a small
// xorshift32 PRNG reset to a fixed seed together with the rest of the chip state.
//
// Time model: never reads a wall clock. One output sample is kInputClocksPerSample (440) input clocks,
// as in MAME's stream_alloc(0, 1, clock() / 440).
//
// Implements IDevice so a Machine's DeviceManager owns it.
class Vlm5030DeviceAdapter final : public IDevice {
public:
    // device_start(): `stream_alloc(0, 1, clock() / 440)`.
    static constexpr std::uint32_t kInputClocksPerSample = 440;

    // Reads one byte of the shared voiceram. The offset is already masked to the chip's 0x7FF-byte address
    // space; the callback translates it to wherever the voiceram lives.
    using ReadVoiceRamFn = std::function<std::uint8_t(std::uint16_t offset)>;

    explicit Vlm5030DeviceAdapter(ReadVoiceRamFn readVoiceRam);

    // Matches device_reset() plus the one-time step to PH_IDLE that device_start() performs afterwards.
    void Reset();

    // Data bus latch (D0-D7), matches data_w(). RstPin()/StPin() consume the latched value on their edges.
    void DataW(std::uint8_t data);

    // RST pin, matches rst_w(). A falling edge latches the data bus as the speed/pitch-offset parameter. A
    // rising edge resets the chip only while BSY is asserted (a rising edge while idle is a no-op).
    void RstPin(bool state);

    // VCU pin, matches vcu_w(). A plain level: selects direct (high byte via VCU) or indirect (lookup table)
    // start addressing on ST's falling edge.
    void VcuPin(bool state);

    // ST pin, matches st_w(). A falling edge either latches the high address byte (VCU asserted) or starts
    // speech at the resolved address (VCU clear). A rising edge asserts BSY and enters the PH_SETUP phase.
    void StPin(bool state);

    // BSY pin, matches bsy_r(). True from ST's rising edge through the end of playback.
    bool BusyPin() const { return busy_pin_ != 0; }

    // Advances the chip by `inputClocks` input clocks and appends one mono float sample per output sample,
    // clamped to [-1, 1) as in MAME's put_int_clamp(..., 512). Partial samples carry to the next call.
    // Returns the number of samples appended.
    std::size_t Render(std::uint64_t inputClocks, std::vector<float>& samples);

    // Test-only observation points.
    std::uint16_t AddressForTest() const { return address_; }
    std::uint8_t PhaseForTest() const { return static_cast<std::uint8_t>(phase_); }
    std::uint32_t CurrentEnergyForTest() const { return current_energy_; }
    std::uint32_t CurrentPitchForTest() const { return current_pitch_; }
    std::uint8_t ParameterForTest() const { return parameter_; }
    int FrameSizeForTest() const { return frame_size_; }
    int PitchOffsetForTest() const { return pitch_offset_; }
    std::uint8_t InterpStepForTest() const { return interp_step_; }
    std::uint16_t VcuAddrHForTest() const { return vcu_addr_h_; }
    // Raw, un-interpolated frame decode results, set when a frame is parsed and unchanged until the next one.
    std::uint32_t NewEnergyForTest() const { return new_energy_; }
    std::uint8_t NewPitchForTest() const { return new_pitch_; }

private:
    enum Phase : std::uint8_t {
        kPhaseReset,
        kPhaseIdle,
        kPhaseSetup,
        kPhaseWait,
        kPhaseRun,
        kPhaseStop,
        kPhaseEnd,
    };

    static constexpr int kFrameSize = 4;  // "FR_SIZE" - interpolator steps per frame

    std::uint8_t ReadByte(std::uint16_t offset) const;
    int GetBits(int startBit, int bits) const;
    int ParseFrame();
    void SetupParameter(std::uint8_t param);
    void TickNoiseRng();

    ReadVoiceRamFn read_voice_ram_;

    std::uint16_t address_ = 0;
    std::uint8_t busy_pin_ = 0;
    std::uint8_t st_pin_ = 0;
    std::uint8_t vcu_pin_ = 0;
    std::uint8_t rst_pin_ = 0;
    std::uint8_t latch_data_ = 0;
    std::uint16_t vcu_addr_h_ = 0;
    std::uint8_t parameter_ = 0;
    Phase phase_ = kPhaseReset;

    int frame_size_ = 0;
    int pitch_offset_ = 0;
    std::uint8_t interp_step_ = 0;

    std::uint8_t interp_count_ = 0;
    std::uint8_t sample_count_ = 0;
    std::uint8_t pitch_count_ = 0;

    std::uint32_t old_energy_ = 0;
    std::uint8_t old_pitch_ = 0;
    std::int32_t old_k_[10] = {};
    std::uint32_t target_energy_ = 0;
    std::uint8_t target_pitch_ = 0;
    std::int32_t target_k_[10] = {};

    std::uint32_t new_energy_ = 0;
    std::uint8_t new_pitch_ = 0;
    std::int32_t new_k_[10] = {};

    std::uint32_t current_energy_ = 0;
    std::uint32_t current_pitch_ = 0;
    std::int32_t current_k_[10] = {};

    std::int32_t x_[10] = {};  // lattice filter state

    std::uint32_t noise_rng_ = 1;  // xorshift32 state (replaces machine().rand())

    std::uint64_t partial_input_clocks_ = 0;
};
