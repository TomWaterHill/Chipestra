// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "clock.h"
#include "device_manager.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

// Chipestra-owned Sound Device wrapper around Aaron Giles' ymfm OPN core (thirdparty/ymfm, vendored from
// MAME, BSD-3-Clause; see THIRD-PARTY-NOTICES.md), using ymfm::ym2610. Follows the pimpl discipline of the
// other ymfm adapters: no ymfm or MAME type appears in this header.
//
// YM2610 = 4 FM channels + 3 SSG channels (AY-3-8910-compatible, built in) + 6 ADPCM-A channels (fixed-rate
// 4-bit ADPCM, external sample ROM) + 1 ADPCM-B channel (variable-rate Delta-T ADPCM, external sample ROM),
// 14 channels total.
//
// Real YM2610 hardware has only 4 usable FM channels, not 6: ymfm's opna_registers (shared with the YM2608)
// reports 6, but ym2610's constructor defaults `channel_mask` to 0x36 (register-offset indices 1, 2, 4, 5),
// leaving indices 0 and 3 silent. The adapter's FM numbering 0-3 maps onto those indices via
// kFmRegisterIndex[] in the .cpp, not straight through.
//
// Per-channel isolation: ymfm's ym2610::generate() only exposes a pre-mixed stereo pair (FM + ADPCM-A +
// ADPCM-B) and a mono-summed SSG. To publish one LogicalChannel per real channel, a private subclass
// (Ym2610PerChannel in the .cpp) reaches the protected engines and:
//   - calls the inherited clock_fm_and_adpcm() once per FM/ADPCM output sample, which advances the engines
//     as real hardware would (including the EOS status bookkeeping read_status_hi() depends on);
//   - then calls each engine's public output(..., chanmask) once per real channel; output() only reads
//     state, so the extra calls have no side effects;
//   - drives the SSG independently at its own rate: ssg_engine::clock()/output() are public and already
//     return 3 separate channel values, so no isolation trick is needed.
//
// Panning: FM, ADPCM-A and ADPCM-B channels each have a hardware L/R output-enable bit pair (on/off, not
// proportional, like the YM2151 RL bits). ChannelOutputsLeft/Right() read those bits from ymfm's register
// state; the owning Machine folds them into LogicalChannel Pan/RoutingGain. The SSG has no pan mechanism
// and is left centered by the Machine.
//
// Time model: never reads a wall clock; chip-input-clock counters advance only inside Render().
class Ym2610DeviceAdapter final : public IDevice {
public:
    static constexpr std::uint32_t kFmChannelCount = 4;
    static constexpr std::uint32_t kSsgChannelCount = 3;
    static constexpr std::uint32_t kAdpcmAChannelCount = 6;
    static constexpr std::uint32_t kAdpcmBChannelCount = 1;
    static constexpr std::uint32_t kChannelCount =
        kFmChannelCount + kSsgChannelCount + kAdpcmAChannelCount + kAdpcmBChannelCount;

    // LogicalChannel ordering published into Render()'s `channelSamples` (Chipestra's own convention):
    // 0-3 = FM, 4-6 = SSG, 7-12 = ADPCM-A, 13 = ADPCM-B.
    static constexpr std::uint32_t kFmBase = 0;
    static constexpr std::uint32_t kSsgBase = kFmBase + kFmChannelCount;
    static constexpr std::uint32_t kAdpcmABase = kSsgBase + kSsgChannelCount;
    static constexpr std::uint32_t kAdpcmBBase = kAdpcmABase + kAdpcmAChannelCount;

    // Ninja Warriors wiring: input clock = 16MHz / 2 = 8,000,000Hz.
    static constexpr ClockRate kInputClockRate{8'000'000};

    // FM, ADPCM-A and ADPCM-B share one cadence (clock_fm_and_adpcm() clocks all three once per output
    // sample). MAME's MED-fidelity rate is input_clock/144 (integer-truncating: 8,000,000/144 = 55555).
    // set_fidelity() is not needed for correctness here but mirrors MAME's device_start().
    static constexpr std::uint64_t kFmAdpcmInputClocksPerSample = 144;
    static constexpr std::uint32_t kFmAdpcmOutputSampleRateHz = 8'000'000 / 144;  // 55555

    // The SSG is driven independently of the FM/ADPCM cadence: ssg_engine expects one clock()/output() call
    // per (ssg_effective_clock / 8), and ssg_effective_clock is input_clock/4, so input_clock/32 =
    // 250,000 Hz exactly.
    static constexpr std::uint64_t kSsgInputClocksPerSample = 32;
    static constexpr std::uint32_t kSsgOutputSampleRateHz = 8'000'000 / 32;  // 250000, exact

    using IrqHandler = std::function<void(bool asserted)>;

    // `adpcmARom`/`adpcmBRom` are the ADPCM-A/B sample ROM images, copied in. Empty vectors are valid (a
    // registered-but-not-yet-playable adapter); out-of-range reads return 0.
    explicit Ym2610DeviceAdapter(std::vector<std::uint8_t> adpcmARom, std::vector<std::uint8_t> adpcmBRom,
                                  IrqHandler irqHandler = nullptr);
    ~Ym2610DeviceAdapter() override;

    Ym2610DeviceAdapter(const Ym2610DeviceAdapter&) = delete;
    Ym2610DeviceAdapter& operator=(const Ym2610DeviceAdapter&) = delete;

    // Power-on-equivalent reset (all engines' register files cleared, IRQ deasserted if it was high).
    void Reset();

    // Register access, wired by the Machine to the sound CPU's 0xE000-0xE003: offset 0-3 matches
    // ym2610_device::read/write (0 = address/status, 1 = data, 2 = address_hi/status_hi, 3 = data_hi).
    std::uint8_t Read(std::uint8_t offset);
    void Write(std::uint8_t offset, std::uint8_t value);

    // Advances the chip by `inputClocks` 8MHz cycles and appends each channel's mono float sample,
    // normalized to [-1, 1], to `channelSamples` (a kChannelCount-long vector of buffers; see kFmBase etc.
    // for the layout). Partial cycles carry to the next call (separate carries for the FM/ADPCM and SSG
    // cadences). Returns the number of FM/ADPCM samples appended; the SSG buffers may receive a different
    // count since it runs at a different rate.
    std::size_t Render(std::uint64_t inputClocks, std::vector<std::vector<float>>& channelSamples);

    // Per-channel hardware L/R output-enable bits (on/off, not proportional). `fmChannel`/`adpcmAChannel`
    // use the adapter's own 0-3 / 0-5 numbering, mapped internally onto the real register-offset indices.
    bool FmChannelOutputsLeft(std::uint32_t fmChannel) const;
    bool FmChannelOutputsRight(std::uint32_t fmChannel) const;
    bool AdpcmAChannelOutputsLeft(std::uint32_t adpcmAChannel) const;
    bool AdpcmAChannelOutputsRight(std::uint32_t adpcmAChannel) const;
    bool AdpcmBOutputsLeft() const;
    bool AdpcmBOutputsRight() const;

    // Test-only observation.
    std::uint64_t GeneratedFmAdpcmSampleCount() const;
    std::uint64_t GeneratedSsgSampleCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
