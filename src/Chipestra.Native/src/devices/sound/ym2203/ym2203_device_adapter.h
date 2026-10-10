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
// MAME, BSD-3-Clause; see THIRD-PARTY-NOTICES.md). Follows Ym2151DeviceAdapter's pimpl discipline: no ymfm
// or MAME type appears in this header.
//
// YM2203 = 3 FM channels (OPN) + 3 SSG channels (AY-3-8910-compatible, built into the same chip) + one I/O
// port pair (A/B). ymfm's ym2203 only exposes one pre-summed mono FM value, so to publish one LogicalChannel
// per FM channel this adapter uses the same technique as Ym2151DeviceAdapter: a private subclass reaches the
// protected fm_engine and calls its per-channel output() with a single-channel mask. The SSG engine already
// produces 3 separate channel outputs natively, so it needs no subclass trick.
//
// The SSG half reuses ymfm's own ssg_engine rather than Ay8910DeviceAdapter: although both are
// register-compatible, ymfm's engine is what MAME's ym2203_device uses, so output stays consistent with it.
//
// Time model: never reads a wall clock; a chip-input-clock counter advances only inside Render().
//
// Fidelity: ymfm's class default is OPN_FIDELITY_MAX, but MAME's ym2203_device overrides it to
// OPN_FIDELITY_MED (SSG_FIDELITY in MAME's ymfm glue, ymfm_mame.h) before first use; this adapter matches MAME.
class Ym2203DeviceAdapter final : public IDevice {
public:
    static constexpr std::uint32_t kFmChannelCount = 3;
    static constexpr std::uint32_t kSsgChannelCount = 3;
    // LogicalChannel ordering published: 0-2 = FM ch A/B/C, 3-5 = SSG ch A/B/C (Chipestra's own order;
    // MAME's stream numbering differs and is not reproduced).
    static constexpr std::uint32_t kChannelCount = kFmChannelCount + kSsgChannelCount;

    // Darius wiring: both YM2203s clock at XTAL(8'000'000)/2 = 4MHz.
    static constexpr ClockRate kInputClockRate{4'000'000};

    // MAME's ym2203_device::sample_rate() at OPN_FIDELITY_MED returns input_clock/12 (integer-truncating:
    // 4'000'000/12 = 333'333).
    static constexpr std::uint32_t kOutputSampleRateHz = 4'000'000 / 12;  // 333333

    // Input clocks consumed per generated output sample (the 12 above). A single fixed ratio suffices even
    // though the FM engine only re-clocks once every kFmSamplesPerOutput samples; the samples in between
    // repeat the last FM value, as in MAME's ym2203::generate().
    static constexpr std::uint64_t kInputClocksPerSample = 12;

    // Same IRQ-handler shape as Ym2151DeviceAdapter. Darius wires only ym1's IRQ to the sound Z80; pass
    // nullptr for ym2's handler.
    using IrqHandler = std::function<void(bool asserted)>;

    // SSG port A/B access (registers 14/15), named like Ay8910DeviceAdapter's PortWriteFn/PortReadFn.
    // Darius uses both ports as outputs (the driver's software volume control); the read callbacks exist
    // for completeness.
    using PortWriteFn = std::function<void(std::uint8_t value)>;
    using PortReadFn = std::function<std::uint8_t()>;

    explicit Ym2203DeviceAdapter(IrqHandler irq_handler = nullptr, PortWriteFn portAWrite = nullptr,
                                 PortWriteFn portBWrite = nullptr, PortReadFn portARead = nullptr,
                                 PortReadFn portBRead = nullptr);
    ~Ym2203DeviceAdapter() override;

    Ym2203DeviceAdapter(const Ym2203DeviceAdapter&) = delete;
    Ym2203DeviceAdapter& operator=(const Ym2203DeviceAdapter&) = delete;

    // Power-on-equivalent reset (FM and SSG register files cleared, IRQ driven low through the handler
    // exactly once if it was high).
    void Reset();

    // Register access, wired by the Machine to the sound Z80's addresses for this chip.
    void WriteAddress(std::uint8_t value);
    void WriteData(std::uint8_t value);
    std::uint8_t ReadStatus();
    // SSG data read (address < 0x10 only, as in ym2203::read_data()); returns 0 for FM register addresses
    // (no FM register is readable on a real YM2203).
    std::uint8_t ReadData();

    // Advances the chip by `inputClocks` input clocks and appends per-channel mono float samples,
    // normalized to [-1, 1], to `channelSamples` (a kChannelCount-long vector of buffers; entries 0-2 get
    // FM ch A/B/C, entries 3-5 get SSG ch A/B/C). Partial samples carry to the next call. Returns the
    // number of output samples appended to each buffer.
    std::size_t Render(std::uint64_t inputClocks, std::vector<std::vector<float>>& channelSamples);

    // Test-only: output samples generated since construction/Reset().
    std::uint64_t GeneratedSampleCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
