// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "devices/sound/ym2610/ym2610_device_adapter.h"

// ymfm is vendored pristine (thirdparty/ymfm, BSD-3-Clause); wrap the includes so its headers don't trip
// /W4 here, and pull in <intrin.h> first since ymfm.h uses _BitScanReverse under _MSC_VER without it.
#ifdef _MSC_VER
#include <intrin.h>
#pragma warning(push, 0)
#endif
#include "ymfm/ymfm.h"
#include "ymfm/ymfm_opn.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <array>
#include <utility>

namespace {

// ymfm::ym2610's default channel_mask (0x36) leaves register-offset FM indices 0 and 3 silent; only
// 1, 2, 4, 5 are real. The adapter's FM channels 0-3 map onto these in order.
constexpr std::uint32_t kFmRegisterIndex[Ym2610DeviceAdapter::kFmChannelCount] = {1, 2, 4, 5};

// The same 4 channels as ymfm's per-channel register offset (chnum % 3 + 0x100 * (chnum / 3)), which is what
// regs().ch_output_0/1() take. kFmRegisterIndex is the channel number (right for fm_engine::output()'s
// chanmask); using it for the pan lookup would read FM 3/4 from the wrong register.
constexpr std::uint32_t kFmChannelOffset[Ym2610DeviceAdapter::kFmChannelCount] = {1, 2, 0x101, 0x102};

// Per-channel isolation subclass (same technique as Ym2203DeviceAdapter's OpnPerChannel). It calls the
// inherited protected clock_fm_and_adpcm() directly, which gives the real side effects (EOS status, busy
// timing), and only exposes references to the protected engines so Impl can read per-channel output()
// values afterwards (a pure read, safe to repeat).
class Ym2610PerChannel final : public ymfm::ym2610 {
public:
    explicit Ym2610PerChannel(ymfm::ymfm_interface& intf) : ymfm::ym2610(intf) {}

    fm_engine& FmEngine() { return m_fm; }
    ymfm::ssg_engine& SsgEngine() { return m_ssg; }
    ymfm::adpcm_a_engine& AdpcmAEngine() { return m_adpcm_a; }
    ymfm::adpcm_b_engine& AdpcmBEngine() { return m_adpcm_b; }

    // Inherited and protected; exposed so Impl::AdvanceFmAdpcmSample() can call it.
    using ymfm::ym2610::clock_fm_and_adpcm;
};

}  // namespace

// The ymfm_interface implementation, the chip instance and the two ADPCM sample ROMs. All ymfm types are
// confined here; the public header exposes only unique_ptr<Impl>.
struct Ym2610DeviceAdapter::Impl final : public ymfm::ymfm_interface {
    Impl(std::vector<std::uint8_t> adpcmARomIn, std::vector<std::uint8_t> adpcmBRomIn, IrqHandler irqHandlerIn)
        : adpcm_a_rom(std::move(adpcmARomIn)), adpcm_b_rom(std::move(adpcmBRomIn)),
          irq_handler(std::move(irqHandlerIn)), chip(*this) {}

    // --- ymfm_interface hooks -------------------------------------------
    void ymfm_set_timer(std::uint32_t tnum, std::int32_t duration_in_clocks) override {
        if (tnum >= 2) {
            return;
        }
        if (duration_in_clocks >= 0) {
            timer_deadline[tnum] = now_fm_adpcm_clocks + static_cast<std::uint64_t>(duration_in_clocks);
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

    void ymfm_set_busy_end(std::uint32_t clocks) override { busy_end_clocks = now_fm_adpcm_clocks + clocks; }

    bool ymfm_is_busy() override { return now_fm_adpcm_clocks < busy_end_clocks; }

    // ADPCM-A/B external sample ROM access. Out-of-range reads return 0, so a short or empty ROM is a
    // valid but silent state.
    std::uint8_t ymfm_external_read(ymfm::access_class type, std::uint32_t address) override {
        if (type == ymfm::ACCESS_ADPCM_A) {
            return address < adpcm_a_rom.size() ? adpcm_a_rom[address] : 0;
        }
        if (type == ymfm::ACCESS_ADPCM_B) {
            return address < adpcm_b_rom.size() ? adpcm_b_rom[address] : 0;
        }
        return 0;
    }

    // --- helpers ----------------------------------------------------------
    // One FM/ADPCM output sample: the real clock_fm_and_adpcm() (advances FM, ADPCM-A and ADPCM-B and
    // updates EOS status), then the per-channel isolation reads.
    void AdvanceFmAdpcmSample() {
        chip.clock_fm_and_adpcm();

        for (std::uint32_t ch = 0; ch < kFmChannelCount; ++ch) {
            fm_out[ch].clear();
            chip.FmEngine().output(fm_out[ch], /*rshift=*/1, /*clipmax=*/32767, 1u << kFmRegisterIndex[ch]);
        }
        for (std::uint32_t ch = 0; ch < kAdpcmAChannelCount; ++ch) {
            adpcm_a_out[ch].clear();
            chip.AdpcmAEngine().output(adpcm_a_out[ch], 1u << ch);
        }
        adpcm_b_out.clear();
        chip.AdpcmBEngine().output(adpcm_b_out, /*rshift=*/1);

        now_fm_adpcm_clocks += Ym2610DeviceAdapter::kFmAdpcmInputClocksPerSample;
        for (std::uint32_t t = 0; t < 2; ++t) {
            while (timer_active[t] && now_fm_adpcm_clocks >= timer_deadline[t]) {
                timer_active[t] = false;
                m_engine->engine_timer_expired(t);
            }
        }
        ++fm_adpcm_samples_generated;
    }

    // One SSG output sample: clock()/output() are public and already return 3 separate channel values.
    void AdvanceSsgSample() {
        chip.SsgEngine().clock();
        chip.SsgEngine().output(ssg_out);
        ++ssg_samples_generated;
    }

    void ResetState() {
        now_fm_adpcm_clocks = 0;
        busy_end_clocks = 0;
        fm_adpcm_samples_generated = 0;
        ssg_samples_generated = 0;
        partial_fm_adpcm_clocks = 0;
        partial_ssg_clocks = 0;
        adpcm_b_last_mono = 0;
        adpcm_b_frozen_streak = 0;
        timer_active[0] = timer_active[1] = false;
        timer_deadline[0] = timer_deadline[1] = 0;
        // chip.reset() calls back through ymfm_update_irq() to deassert the IRQ if it was high.
        chip.reset();
    }

    std::vector<std::uint8_t> adpcm_a_rom;
    std::vector<std::uint8_t> adpcm_b_rom;
    IrqHandler irq_handler;
    Ym2610PerChannel chip;

    std::array<ymfm::ymfm_output<2>, Ym2610DeviceAdapter::kFmChannelCount> fm_out{};
    std::array<ymfm::ymfm_output<2>, Ym2610DeviceAdapter::kAdpcmAChannelCount> adpcm_a_out{};
    ymfm::ymfm_output<2> adpcm_b_out{};
    ymfm::ymfm_output<3> ssg_out{};

    // ADPCM-B residual-DC suppression. At the natural end of a sample, ymfm's adpcm_b_channel::clock()
    // resets the ADPCM state but leaves the output alone (ADPCM-A, by contrast, outputs 0 when not
    // playing), so the last decoded value is held until the next key-on. On real hardware the cabinet's
    // AC-coupled output stage filters that constant level out; Chipestra has no such stage, so it would
    // keep the channel meter up and be audible as DC. The playing status bits are private or do not clear
    // at end of sample, so the stop is detected from the one observable signal: the identical raw decoded
    // value repeating for kAdpcmBFrozenStreakThreshold consecutive samples (64, ~1.15ms at ~55555Hz). This
    // does not happen during genuine playback, and the threshold is far below anything audible.
    static constexpr std::uint32_t kAdpcmBFrozenStreakThreshold = 64;
    std::int32_t adpcm_b_last_mono = 0;
    std::uint32_t adpcm_b_frozen_streak = 0;

    std::uint64_t now_fm_adpcm_clocks = 0;
    std::uint64_t busy_end_clocks = 0;
    std::uint64_t fm_adpcm_samples_generated = 0;
    std::uint64_t ssg_samples_generated = 0;
    std::uint64_t partial_fm_adpcm_clocks = 0;
    std::uint64_t partial_ssg_clocks = 0;

    bool timer_active[2] = {false, false};
    std::uint64_t timer_deadline[2] = {0, 0};
    bool irq_asserted = false;
};

Ym2610DeviceAdapter::Ym2610DeviceAdapter(std::vector<std::uint8_t> adpcmARom, std::vector<std::uint8_t> adpcmBRom,
                                         IrqHandler irqHandler)
    : impl_(std::make_unique<Impl>(std::move(adpcmARom), std::move(adpcmBRom), std::move(irqHandler))) {
    // Like MAME's device_start(); has no effect on clock_fm_and_adpcm() but mirrors it.
    impl_->chip.set_fidelity(ymfm::OPN_FIDELITY_MED);
    impl_->ResetState();
}

Ym2610DeviceAdapter::~Ym2610DeviceAdapter() = default;

void Ym2610DeviceAdapter::Reset() { impl_->ResetState(); }

std::uint8_t Ym2610DeviceAdapter::Read(std::uint8_t offset) { return impl_->chip.read(offset); }

void Ym2610DeviceAdapter::Write(std::uint8_t offset, std::uint8_t value) { impl_->chip.write(offset, value); }

std::size_t Ym2610DeviceAdapter::Render(std::uint64_t inputClocks,
                                        std::vector<std::vector<float>>& channelSamples) {
    // FM/ADPCM-A/ADPCM-B cadence.
    const std::uint64_t fmAvailable = impl_->partial_fm_adpcm_clocks + inputClocks;
    const std::size_t fmSampleCount = static_cast<std::size_t>(fmAvailable / kFmAdpcmInputClocksPerSample);
    impl_->partial_fm_adpcm_clocks = fmAvailable % kFmAdpcmInputClocksPerSample;

    for (std::size_t i = 0; i < fmSampleCount; ++i) {
        impl_->AdvanceFmAdpcmSample();
        for (std::uint32_t ch = 0; ch < kFmChannelCount; ++ch) {
            const ymfm::ymfm_output<2>& out = impl_->fm_out[ch];
            const bool left = FmChannelOutputsLeft(ch);
            const std::int32_t mono = left ? out.data[0] : out.data[1];
            channelSamples[kFmBase + ch].push_back(static_cast<float>(mono) / 32768.0f);
        }
        for (std::uint32_t ch = 0; ch < kAdpcmAChannelCount; ++ch) {
            const ymfm::ymfm_output<2>& out = impl_->adpcm_a_out[ch];
            const bool left = AdpcmAChannelOutputsLeft(ch);
            const std::int32_t mono = left ? out.data[0] : out.data[1];
            channelSamples[kAdpcmABase + ch].push_back(static_cast<float>(mono) / 32768.0f);
        }
        {
            const ymfm::ymfm_output<2>& out = impl_->adpcm_b_out;
            const bool left = AdpcmBOutputsLeft();
            const std::int32_t mono = left ? out.data[0] : out.data[1];

            // Frozen-streak detection (see Impl::kAdpcmBFrozenStreakThreshold): suppresses the held DC level.
            if (mono == impl_->adpcm_b_last_mono) {
                if (impl_->adpcm_b_frozen_streak < Impl::kAdpcmBFrozenStreakThreshold) {
                    ++impl_->adpcm_b_frozen_streak;
                }
            } else {
                impl_->adpcm_b_last_mono = mono;
                impl_->adpcm_b_frozen_streak = 0;
            }
            const bool frozen = impl_->adpcm_b_frozen_streak >= Impl::kAdpcmBFrozenStreakThreshold;
            channelSamples[kAdpcmBBase].push_back(frozen ? 0.0f : static_cast<float>(mono) / 32768.0f);
        }
    }

    // SSG cadence (independent rate).
    const std::uint64_t ssgAvailable = impl_->partial_ssg_clocks + inputClocks;
    const std::size_t ssgSampleCount = static_cast<std::size_t>(ssgAvailable / kSsgInputClocksPerSample);
    impl_->partial_ssg_clocks = ssgAvailable % kSsgInputClocksPerSample;

    for (std::size_t i = 0; i < ssgSampleCount; ++i) {
        impl_->AdvanceSsgSample();
        for (std::uint32_t ch = 0; ch < kSsgChannelCount; ++ch) {
            channelSamples[kSsgBase + ch].push_back(static_cast<float>(impl_->ssg_out.data[ch]) / 32768.0f);
        }
    }

    return fmSampleCount;
}

bool Ym2610DeviceAdapter::FmChannelOutputsLeft(std::uint32_t fmChannel) const {
    return impl_->chip.FmEngine().regs().ch_output_0(kFmChannelOffset[fmChannel]) != 0;
}

bool Ym2610DeviceAdapter::FmChannelOutputsRight(std::uint32_t fmChannel) const {
    return impl_->chip.FmEngine().regs().ch_output_1(kFmChannelOffset[fmChannel]) != 0;
}

bool Ym2610DeviceAdapter::AdpcmAChannelOutputsLeft(std::uint32_t adpcmAChannel) const {
    return impl_->chip.AdpcmAEngine().regs().ch_pan_left(adpcmAChannel) != 0;
}

bool Ym2610DeviceAdapter::AdpcmAChannelOutputsRight(std::uint32_t adpcmAChannel) const {
    return impl_->chip.AdpcmAEngine().regs().ch_pan_right(adpcmAChannel) != 0;
}

bool Ym2610DeviceAdapter::AdpcmBOutputsLeft() const { return impl_->chip.AdpcmBEngine().regs().pan_left() != 0; }

bool Ym2610DeviceAdapter::AdpcmBOutputsRight() const { return impl_->chip.AdpcmBEngine().regs().pan_right() != 0; }

std::uint64_t Ym2610DeviceAdapter::GeneratedFmAdpcmSampleCount() const { return impl_->fm_adpcm_samples_generated; }

std::uint64_t Ym2610DeviceAdapter::GeneratedSsgSampleCount() const { return impl_->ssg_samples_generated; }
