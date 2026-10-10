// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "devices/sound/ym2151/ym2151_device_adapter.h"

// ymfm is vendored pristine (thirdparty/ymfm, BSD-3-Clause). It is header-heavy
// template code held to a different warning bar than Chipestra's own sources
// (matches the miniz/pugixml precedent in CMakeLists.txt); wrap the includes so
// its headers don't trip /W4 here. <intrin.h> up front: ymfm.h uses
// _BitScanReverse under _MSC_VER without including it itself.
#ifdef _MSC_VER
#include <intrin.h>
#pragma warning(push, 0)
#endif
#include "ymfm/ymfm.h"
#include "ymfm/ymfm_opm.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <array>
#include <new>
#include <utility>

namespace {

// ymfm's ym2151::generate() only exposes the summed 8-channel mix. To publish
// one Chipestra LogicalChannel per FM channel we need each channel's
// output in isolation, so subclass ym2151 to reach the protected fm_engine and
// call its public output() once per channel with a single-channel mask. This
// touches only ym2151's already-protected surface - the vendored ymfm sources
// stay byte-for-byte upstream (THIRD-PARTY-NOTICES.md).
class OpmPerChannel final : public ymfm::ym2151 {
public:
    explicit OpmPerChannel(ymfm::ymfm_interface& intf) : ymfm::ym2151(intf) {}

    // Clock the engine forward one output sample, then capture each FM
    // channel's stereo output separately. Body mirrors ym2151::generate().
    void ClockOneSample(std::array<ymfm::ym2151::output_data, 8>& out) {
        m_fm.clock(fm_engine::ALL_CHANNELS);
        for (std::uint32_t ch = 0; ch < 8; ++ch) {
            m_fm.output(out[ch].clear(), 0, 32767, 1u << ch);
            // YM2151 feeds an external YM3012 float DAC per channel; round-trip
            // each channel through the same 10.3 float truncation generate()
            // applies to the mix.
            out[ch].roundtrip_fp();
        }
    }
};

}  // namespace

// The ymfm_interface implementation + the chip instance. All ymfm types are
// confined here; the public header exposes only unique_ptr<Impl>.
struct Ym2151DeviceAdapter::Impl final : public ymfm::ymfm_interface {
    explicit Impl(IrqHandler handler) : irq_handler(std::move(handler)), chip(*this) {}

    // --- ymfm_interface hooks --------------------------------------------
    // ymfm hands us timer durations in chip input clocks; record an absolute
    // deadline on the same counter Render() advances. A negative duration
    // cancels the timer (ymfm's documented convention).
    void ymfm_set_timer(std::uint32_t tnum, std::int32_t duration_in_clocks) override {
        if (tnum >= 2) {
            return;
        }
        if (duration_in_clocks >= 0) {
            timer_deadline[tnum] = now_clocks + static_cast<std::uint64_t>(duration_in_clocks);
            timer_active[tnum] = true;
        } else {
            timer_active[tnum] = false;
        }
    }

    void ymfm_update_irq(bool asserted) override {
        irq_asserted = asserted;
        if (irq_handler) {
            irq_handler(asserted);
        }
    }

    void ymfm_set_busy_end(std::uint32_t clocks) override {
        busy_end_clocks = now_clocks + clocks;
    }

    bool ymfm_is_busy() override { return now_clocks < busy_end_clocks; }

    // ymfm_sync_mode_write / ymfm_sync_check_interrupts keep their default
    // implementations (call straight into the engine): Chipestra drives the chip
    // from a single synchronous Render() with no other CPU core to order
    // against, so there is nothing to defer (unlike MAME's scheduler path).

    // --- helpers --------------------------------------------------------
    // Advance the deterministic clock by one output sample and fire any timer
    // whose deadline has passed. engine_timer_expired() re-arms the timer
    // synchronously through ymfm_set_timer(), so loop until it settles.
    void AdvanceOneSample() {
        now_clocks += Ym2151DeviceAdapter::kInputClocksPerSample;
        for (std::uint32_t t = 0; t < 2; ++t) {
            while (timer_active[t] && now_clocks >= timer_deadline[t]) {
                timer_active[t] = false;
                m_engine->engine_timer_expired(t);
            }
        }
    }

    void ResetState() {
        now_clocks = 0;
        busy_end_clocks = 0;
        generated_samples = 0;
        partial_clocks = 0;
        timer_active[0] = timer_active[1] = false;
        timer_deadline[0] = timer_deadline[1] = 0;
        // ymfm's reset() leaves some engine state alone (e.g. the envelope counter), so a mid-session
        // Reset() would not match a fresh chip bit for bit. Rebuild the chip object instead (the vendored
        // ymfm sources stay untouched), deasserting a high IRQ line first since the new chip starts with
        // it low and would not call back.
        if (irq_asserted) {
            ymfm_update_irq(false);
        }
        chip.~OpmPerChannel();
        new (&chip) OpmPerChannel(*this);
        // chip.reset() drives the status/IRQ/mode path, which calls back
        // through ymfm_update_irq() to deassert if it was high.
        chip.reset();
    }

    IrqHandler irq_handler;
    OpmPerChannel chip;

    std::uint64_t now_clocks = 0;       // chip input clocks since construction/Reset()
    std::uint64_t busy_end_clocks = 0;  // ymfm_is_busy() horizon
    std::uint64_t generated_samples = 0;
    std::uint64_t stereo_samples = 0;   // diagnostic: samples with L != R on some channel
    std::uint64_t partial_clocks = 0;   // < kInputClocksPerSample, carried between Render() calls

    bool timer_active[2] = {false, false};
    std::uint64_t timer_deadline[2] = {0, 0};
    bool irq_asserted = false;
};

Ym2151DeviceAdapter::Ym2151DeviceAdapter(IrqHandler irq_handler)
    : impl_(std::make_unique<Impl>(std::move(irq_handler))) {
    // ymfm requires an explicit reset() after construction to reach a defined
    // state (MAME does this in device_reset()).
    impl_->ResetState();
}

Ym2151DeviceAdapter::~Ym2151DeviceAdapter() = default;

void Ym2151DeviceAdapter::Reset() {
    impl_->ResetState();
    latched_address_ = 0;
    for (std::uint32_t ch = 0; ch < kChannelCount; ++ch) {
        ch_out_left_[ch] = false;
        ch_out_right_[ch] = false;
    }
}

void Ym2151DeviceAdapter::WriteAddress(std::uint8_t value) {
    latched_address_ = value;
    impl_->chip.write_address(value);
}

void Ym2151DeviceAdapter::WriteData(std::uint8_t value) {
    impl_->chip.write_data(value);
    // Registers 0x20-0x27 are RL / feedback / connect per channel; bit 6 is the
    // left-bus enable, bit 7 the right-bus enable (ymfm ch_output_0/1).
    if ((latched_address_ & 0xF8u) == 0x20u) {
        const std::uint32_t ch = latched_address_ & 0x07u;
        ch_out_left_[ch] = (value & 0x40u) != 0;
        ch_out_right_[ch] = (value & 0x80u) != 0;
    }
}

bool Ym2151DeviceAdapter::ChannelOutputsLeft(std::uint32_t channel) const {
    return channel < kChannelCount && ch_out_left_[channel];
}

bool Ym2151DeviceAdapter::ChannelOutputsRight(std::uint32_t channel) const {
    return channel < kChannelCount && ch_out_right_[channel];
}

std::uint8_t Ym2151DeviceAdapter::ReadStatus() { return impl_->chip.read_status(); }

std::size_t Ym2151DeviceAdapter::Render(std::uint64_t inputClocks,
                                        std::vector<std::vector<float>>& channelSamples) {
    const std::uint64_t available = impl_->partial_clocks + inputClocks;
    const std::size_t sampleCount = static_cast<std::size_t>(available / kInputClocksPerSample);
    impl_->partial_clocks = available % kInputClocksPerSample;

    std::array<ymfm::ym2151::output_data, 8> perChannel;
    for (std::size_t i = 0; i < sampleCount; ++i) {
        impl_->chip.ClockOneSample(perChannel);
        impl_->AdvanceOneSample();
        bool anyStereo = false;
        for (std::uint32_t ch = 0; ch < kChannelCount; ++ch) {
            // ymfm routes the channel waveform to data[0] only when the left
            // bus is enabled and to data[1] only when the right bus is enabled,
            // so data[0]==data[1]==waveform with both on, and one side is 0
            // when only the other is on. Recover the un-routed waveform (so a
            // hard-panned channel keeps full level); the L/R split is applied
            // downstream from ChannelOutputsLeft/Right(). Normalized from
            // ymfm's ~16-bit range to [-1, 1].
            const float sum =
                static_cast<float>(perChannel[ch].data[0]) + static_cast<float>(perChannel[ch].data[1]);
            const bool both = ch_out_left_[ch] && ch_out_right_[ch];
            const float mono = sum * ((both ? 0.5f : 1.0f) / 32768.0f);
            channelSamples[ch].push_back(mono);
            anyStereo = anyStereo || (perChannel[ch].data[0] != perChannel[ch].data[1]);
        }
        if (anyStereo) {
            ++impl_->stereo_samples;
        }
        ++impl_->generated_samples;
    }
    return sampleCount;
}

std::uint64_t Ym2151DeviceAdapter::GeneratedSampleCount() const { return impl_->generated_samples; }

std::uint64_t Ym2151DeviceAdapter::StereoSampleCount() const { return impl_->stereo_samples; }
