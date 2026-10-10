// license:BSD-3-Clause
// copyright-holders:Bryan McPhail
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/sound/k005289.cpp
//   src/devices/sound/k005289.h
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#pragma once

#include "device_manager.h"

#include <cstdint>
#include <vector>

// Port of MAME's k005289_device (Konami 005289 address generator, 2 voices);
// see THIRD-PARTY-NOTICES.md. No MAME type appears in this header or the
// .cpp; the chip is driven by a plain Render() call.
//
// The 005289 is "nothing but an address generator": each voice walks a
// 32-byte window of a 4-bit-wide waveform PROM (one voice's window per
// 256-byte half of the combined 512-byte PROM). Volume and waveform-select
// come from the byte delivered by an AY8910 port A/B write callback (the
// AY8910's I/O port doubles as this chip's control bus: PA0-3 = volume,
// PA5-7 = waveform select, PB for the second voice). Digital-to-analog
// conversion and mixing are done externally with resistor networks, so there
// is no calibrated table to port; see Render() for the sample scaling.
//
// Clock domain: the chip runs at a fixed 3,579,545Hz, exactly double the
// Z80/AY8910 clock (both derive from the same 14.31818MHz master oscillator),
// not from whatever clock the owning Machine runs its CPU at.
//
// Deterministic time model: never reads a wall clock. One output sample is
// kInputClocksPerSample (1) input clock, with no internal divider.
//
// Implements IDevice so a Machine's DeviceManager owns it. Register access
// (ControlA/ControlB/Ld1/Ld2/Tg1/Tg2) is driven directly by the owning
// Machine, since the writes are spread over several addresses and mechanisms
// rather than one mapped register window.
class K005289DeviceAdapter final : public IDevice {
public:
    static constexpr std::uint32_t kVoiceCount = 2;

    // One 256-byte waveform-PROM half per voice (voice 0: bytes
    // [0, kWaveformPromBytesPerVoice); voice 1: the next half), matching
    // MAME's `((i & 1) << 8) | v.waveform` addressing.
    static constexpr std::size_t kWaveformPromBytesPerVoice = 256;
    static constexpr std::size_t kWaveformPromTotalBytes = kWaveformPromBytesPerVoice * kVoiceCount;

    // One output sample per input clock (MAME's `stream_alloc(0, 1, clock())`).
    static constexpr std::uint32_t kInputClocksPerSample = 1;

    // `waveformProm` must be exactly kWaveformPromTotalBytes long (wave0 then
    // wave1 concatenated); a shorter vector is zero-padded, a longer one
    // truncated, so a missing ROM never crashes the Machine.
    explicit K005289DeviceAdapter(std::vector<std::uint8_t> waveformProm);

    // Power-on-equivalent reset: both voices' counter/frequency/pitch/
    // waveform/volume cleared (the chip has no reset line on the board).
    void Reset();

    // AY8910 port A/B write callback target (`control_A_w`/`control_B_w`).
    // Bits 0-3 = volume (0-15), bits 5-7 = waveform-select (which 32-byte
    // window of this voice's 256-byte PROM half); bit 4 is unused on real
    // hardware (masked off by the 0xe0 write-mask below).
    void ControlA(std::uint8_t data);
    void ControlB(std::uint8_t data);

    // `ld1_w`/`ld2_w`. The chip has no data bus for these; `offset` (the
    // address within the mapped 4KB window) IS the value and `data` is
    // ignored. Only latches `pitch`; the audible `frequency` changes on
    // Tg1/Tg2.
    void Ld1(std::uint16_t offset, std::uint8_t data);
    void Ld2(std::uint16_t offset, std::uint8_t data);

    // `tg1_w`/`tg2_w`. Commits the already-latched pitch (see Ld1/Ld2) to
    // the audible frequency; `data` is ignored (single-byte trigger).
    void Tg1(std::uint8_t data);
    void Tg2(std::uint8_t data);

    // Advances the chip by `inputClocks` input clocks and appends each of
    // the kVoiceCount voices' mono float sample to `voiceSamples` (a
    // kVoiceCount-long vector of buffers). Returns the number of output
    // samples appended to each buffer.
    //
    // Sample value: MAME's per-sample formula is
    // `((waveform_prom_nibble & 0xf) - 8) * volume` - a signed 4-bit waveform
    // sample (range -8..7, an inherent hardware asymmetry) times the 4-bit
    // volume register (0..15) - scaled by MAME's stream divisor of 512
    // (maximum magnitude about 0.234).
    std::size_t Render(std::uint64_t inputClocks, std::vector<std::vector<float>>& voiceSamples);

    // Test-only observation points - not part of production playback.
    std::int16_t CounterForTest(std::uint32_t voice) const { return voice_[voice].counter; }
    std::uint16_t FrequencyForTest(std::uint32_t voice) const { return voice_[voice].frequency; }
    std::uint16_t PitchForTest(std::uint32_t voice) const { return voice_[voice].pitch; }
    std::uint16_t WaveformForTest(std::uint32_t voice) const { return voice_[voice].waveform; }
    std::uint8_t VolumeForTest(std::uint32_t voice) const { return voice_[voice].volume; }

private:
    // One voice's state, matching MAME's voice_t exactly (k005289.h).
    struct Voice {
        std::int16_t counter = 0;
        std::uint16_t frequency = 0;
        std::uint16_t pitch = 0;
        std::uint16_t waveform = 0;
        std::uint8_t volume = 0;
    };

    void TickOneSample();

    std::vector<std::uint8_t> waveform_prom_;
    Voice voice_[kVoiceCount]{};
    std::uint64_t partial_input_clocks_ = 0;
};
