// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "devices/sound/ym2612/ym2612_device_adapter.h"

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
#include <new>

namespace {

constexpr std::uint32_t kChannels = Ym2612DeviceAdapter::kChannelCount;

// Register offset of an FM channel in the pan/feedback register block (channels 4-6 live in the upper bank).
constexpr std::uint32_t ChannelOffset(std::uint32_t ch) {
    return ch == Ym2612DeviceAdapter::kDacChannel ? 0x102u : (ch < 3 ? ch : 0x100u + (ch - 3));
}

// ymfm's ym2612::generate() only exposes the summed mix. This subclass repeats its body (ymfm_opn.cpp) but keeps
// each channel's share, so the Machine can publish one logical channel per FM channel. The sums are the chip's
// own: the last active channel takes the integer rounding remainder of the final scaling.
class Opn2PerChannel final : public ymfm::ym2612 {
public:
    struct Sample {
        std::array<std::int32_t, kChannels> left{};
        std::array<std::int32_t, kChannels> right{};
        std::int32_t total_left = 0;
        std::int32_t total_right = 0;
    };

    explicit Opn2PerChannel(ymfm::ymfm_interface& intf) : ymfm::ym2612(intf) {}

    void ClockOneSample(Sample& out) {
        m_fm.clock(fm_engine::ALL_CHANNELS);
        std::array<std::int32_t, kChannels> raw_left{};
        std::array<std::int32_t, kChannels> raw_right{};
        std::int32_t sum_left = 0;
        std::int32_t sum_right = 0;
        const int last_fm_channel = m_dac_enable ? 5 : 6;
        for (int chan = 0; chan < last_fm_channel; ++chan) {
            output_data temp;
            m_fm.output(temp.clear(), 5, 256, 1 << chan);
            raw_left[chan] = dac_discontinuity(temp.data[0]);
            raw_right[chan] = dac_discontinuity(temp.data[1]);
            sum_left += raw_left[chan];
            sum_right += raw_right[chan];
        }
        if (m_dac_enable) {
            const std::int32_t dacval = dac_discontinuity(std::int16_t(m_dac_data << 7) >> 7);
            raw_left[Ym2612DeviceAdapter::kDacChannel] =
                m_fm.regs().ch_output_0(0x102) ? dacval : dac_discontinuity(0);
            raw_right[Ym2612DeviceAdapter::kDacChannel] =
                m_fm.regs().ch_output_1(0x102) ? dacval : dac_discontinuity(0);
            sum_left += raw_left[Ym2612DeviceAdapter::kDacChannel];
            sum_right += raw_right[Ym2612DeviceAdapter::kDacChannel];
        }
        out.total_left = (sum_left * 128) * 64 / (6 * 65);
        out.total_right = (sum_right * 128) * 64 / (6 * 65);

        std::int32_t last_active = -1;
        for (std::uint32_t c = 0; c < kChannels; ++c) {
            if (raw_left[c] != 0 || raw_right[c] != 0) {
                last_active = static_cast<std::int32_t>(c);
            }
        }
        std::int32_t acc_left = 0;
        std::int32_t acc_right = 0;
        for (std::uint32_t c = 0; c < kChannels; ++c) {
            if (static_cast<std::int32_t>(c) == last_active) {
                out.left[c] = out.total_left - acc_left;
                out.right[c] = out.total_right - acc_right;
            } else {
                out.left[c] = (raw_left[c] * 128) * 64 / (6 * 65);
                out.right[c] = (raw_right[c] * 128) * 64 / (6 * 65);
            }
            acc_left += out.left[c];
            acc_right += out.right[c];
        }
    }

    bool OutputsLeft(std::uint32_t ch) { return m_fm.regs().ch_output_0(ChannelOffset(ch)) != 0; }
    bool OutputsRight(std::uint32_t ch) { return m_fm.regs().ch_output_1(ChannelOffset(ch)) != 0; }
};

}  // namespace

struct Ym2612DeviceAdapter::Impl final : public ymfm::ymfm_interface {
    explicit Impl(std::uint64_t z80ClockHz) : chip(*this), kTicksPerClock(z80ClockHz) {}

    void ymfm_set_timer(std::uint32_t tnum, std::int32_t duration_in_clocks) override {
        if (tnum >= 2) {
            return;
        }
        if (duration_in_clocks >= 0) {
            timer_deadline[tnum] = now_ticks + static_cast<std::uint64_t>(duration_in_clocks) * kTicksPerClock;
            timer_active[tnum] = true;
        } else {
            timer_active[tnum] = false;
        }
    }

    void ymfm_set_busy_end(std::uint32_t clocks) override { busy_end_ticks = now_ticks + clocks * kTicksPerClock; }
    bool ymfm_is_busy() override { return now_ticks < busy_end_ticks; }

    // Lets the timers that are due by `limitTicks` expire, each at its own time, so that one that reloads itself counts
    // from the time it expired.
    void ExpireTimersUpTo(std::uint64_t limitTicks) {
        for (;;) {
            std::uint32_t due = 2;
            for (std::uint32_t t = 0; t < 2; ++t) {
                if (timer_active[t] && timer_deadline[t] <= limitTicks && (due == 2 || timer_deadline[t] < timer_deadline[due])) due = t;
            }
            if (due == 2) break;
            timer_active[due] = false;
            now_ticks = timer_deadline[due];
            m_engine->engine_timer_expired(due);
        }
        now_ticks = limitTicks;
    }

    void AdvanceOneSample() {
        // The end of the sample that has just been made.
        ExpireTimersUpTo((generated_samples + 1) * Ym2612DeviceAdapter::kInputClocksPerSample * kTicksPerClock);
    }

    void ResetState() {
        now_ticks = 0;
        busy_end_ticks = 0;
        generated_samples = 0;
        partial_clocks = 0;
        timer_active[0] = timer_active[1] = false;
        timer_deadline[0] = timer_deadline[1] = 0;
        // ymfm's reset() leaves some engine state alone, so rebuild the chip object for a power-on state.
        chip.~Opn2PerChannel();
        new (&chip) Opn2PerChannel(*this);
        chip.reset();
    }

    Opn2PerChannel chip;
    // Time is kept in ticks of 1 / (Z80 clock * chip clock) of a second: a chip clock (the master clock divided by 7 and
    // truncated to a whole number) is `kTicksPerClock` (the Z80 clock in Hz) ticks and a Z80 cycle is as many ticks as the
    // chip clock has Hz (7670453 in the NTSC machine).
    const std::uint64_t kTicksPerClock;
    std::uint64_t now_ticks = 0;
    std::uint64_t busy_end_ticks = 0;
    std::uint64_t generated_samples = 0;
    std::uint64_t partial_clocks = 0;
    bool timer_active[2] = {false, false};
    std::uint64_t timer_deadline[2] = {0, 0};
};

Ym2612DeviceAdapter::Ym2612DeviceAdapter(std::uint64_t z80ClockHz) : impl_(std::make_unique<Impl>(z80ClockHz)) { impl_->ResetState(); }

Ym2612DeviceAdapter::~Ym2612DeviceAdapter() = default;

void Ym2612DeviceAdapter::Reset() { impl_->ResetState(); }

void Ym2612DeviceAdapter::SoftReset() {
    impl_->timer_active[0] = impl_->timer_active[1] = false;
    impl_->chip.reset();
}

void Ym2612DeviceAdapter::SetAccessTime(std::uint64_t masterTicks) {
    // Timers that are due by then expire first (so the status shows them).
    impl_->ExpireTimersUpTo(masterTicks);
}

void Ym2612DeviceAdapter::Write(std::uint32_t port, std::uint8_t value) { impl_->chip.write(port & 3u, value); }

std::uint8_t Ym2612DeviceAdapter::ReadStatus() { return impl_->chip.read_status(); }

std::size_t Ym2612DeviceAdapter::Render(std::uint64_t inputClocks, std::vector<std::vector<float>>& channelSamples) {
    const std::uint64_t available = impl_->partial_clocks + inputClocks;
    const std::size_t sampleCount = static_cast<std::size_t>(available / kInputClocksPerSample);
    impl_->partial_clocks = available % kInputClocksPerSample;
    Opn2PerChannel::Sample s;
    for (std::size_t i = 0; i < sampleCount; ++i) {
        impl_->chip.ClockOneSample(s);
        impl_->AdvanceOneSample();
        for (std::uint32_t c = 0; c < kChannelCount; ++c) {
            // The chip routes a channel's waveform to the left output only when its left bit is set (and likewise
            // right), so data[0] + data[1] is the waveform (twice when both are on).
            const bool left = impl_->chip.OutputsLeft(c);
            const bool right = impl_->chip.OutputsRight(c);
            const float sum = static_cast<float>(s.left[c]) + static_cast<float>(s.right[c]);
            channelSamples[c].push_back(sum * ((left && right ? 0.5f : 1.0f) / 32768.0f));
        }
        ++impl_->generated_samples;
    }
    return sampleCount;
}

std::size_t Ym2612DeviceAdapter::RenderStereo(std::uint64_t inputClocks, std::vector<float>& left,
                                              std::vector<float>& right) {
    const std::uint64_t available = impl_->partial_clocks + inputClocks;
    const std::size_t sampleCount = static_cast<std::size_t>(available / kInputClocksPerSample);
    impl_->partial_clocks = available % kInputClocksPerSample;
    Opn2PerChannel::Sample s;
    for (std::size_t i = 0; i < sampleCount; ++i) {
        impl_->chip.ClockOneSample(s);
        impl_->AdvanceOneSample();
        left.push_back(static_cast<float>(s.total_left) / 32768.0f);
        right.push_back(static_cast<float>(s.total_right) / 32768.0f);
        ++impl_->generated_samples;
    }
    return sampleCount;
}

bool Ym2612DeviceAdapter::ChannelOutputsLeft(std::uint32_t channel) const {
    return channel < kChannelCount && impl_->chip.OutputsLeft(channel);
}

bool Ym2612DeviceAdapter::ChannelOutputsRight(std::uint32_t channel) const {
    return channel < kChannelCount && impl_->chip.OutputsRight(channel);
}

std::uint64_t Ym2612DeviceAdapter::GeneratedSampleCount() const { return impl_->generated_samples; }

Ym2612DeviceAdapter::SavedState Ym2612DeviceAdapter::SaveState() {
    SavedState s;
    ymfm::ymfm_saved_state saver(s.chip, true);
    impl_->chip.save_restore(saver);
    s.nowTicks = impl_->now_ticks;
    s.busyEndTicks = impl_->busy_end_ticks;
    s.generatedSamples = impl_->generated_samples;
    s.partialClocks = impl_->partial_clocks;
    for (int t = 0; t < 2; ++t) {
        s.timerActive[t] = impl_->timer_active[t];
        s.timerDeadline[t] = impl_->timer_deadline[t];
    }
    return s;
}

void Ym2612DeviceAdapter::RestoreState(SavedState& s) {
    ymfm::ymfm_saved_state loader(s.chip, false);
    impl_->chip.save_restore(loader);
    impl_->chip.invalidate_caches();
    impl_->now_ticks = s.nowTicks;
    impl_->busy_end_ticks = s.busyEndTicks;
    impl_->generated_samples = s.generatedSamples;
    impl_->partial_clocks = s.partialClocks;
    for (int t = 0; t < 2; ++t) {
        impl_->timer_active[t] = s.timerActive[t];
        impl_->timer_deadline[t] = s.timerDeadline[t];
    }
}
