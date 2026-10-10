// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "devices/sound/ym2203/ym2203_device_adapter.h"

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

// ymfm's ym2203::generate() only exposes one pre-summed mono FM value. To publish one LogicalChannel per
// FM channel, this subclass reaches the protected fm_engine/ssg_engine members and reimplements generate()'s
// cadence logic with a per-channel FM clock step. The SSG half is untouched (the resampler is called exactly
// as the base class does), so the 3 SSG channels stay identical to MAME's ym2203_device.
class OpnPerChannel final : public ymfm::ym2203 {
public:
    // Public alias so callers outside this translation unit can name the per-channel FM output type.
    using FmChannelOutput = fm_engine::output_data;

    explicit OpnPerChannel(ymfm::ymfm_interface& intf) : ymfm::ym2203(intf) {}

    // Mirrors ym2203::generate()'s two branches for one output sample. `fmOut[ch]` receives FM channel
    // ch's isolated output; `ssgOut` receives the SSG triplet from the same output_data slots the stock
    // resample() call fills (indices 1-3).
    void ClockOneSample(std::array<fm_engine::output_data, 3>& fmOut, ymfm::ym2203::output_data& combined) {
        if (m_fm_samples_per_output != 0) {
            if (m_ssg_resampler.sampindex() % m_fm_samples_per_output == 0) {
                ClockFmPerChannel(fmOut);
            }
            // combined.data[0] is unused here (per-channel data comes from fmOut) but is still written so
            // the struct matches what resample() expects, as generate() does.
            combined.data[0] = m_last_fm_per_channel_sum;
        } else {
            // MIN fidelity with prescale == 3 only; this adapter always runs at MED fidelity, so this branch
            // is unreachable in practice. Kept to mirror the base class.
            const std::uint32_t step = m_ssg_resampler.sampindex() % 3;
            if (step == 0) {
                ClockFmPerChannel(fmOut);
                m_last_fm_per_channel_sum = SumChannels(fmOut);
            }
            combined.data[0] = m_last_fm_per_channel_sum;
            if (step == 1) {
                std::array<fm_engine::output_data, 3> second{};
                ClockFmPerChannel(second);
                const std::int32_t secondSum = SumChannels(second);
                combined.data[0] = (combined.data[0] + secondSum) / 2;
                for (std::uint32_t ch = 0; ch < 3; ++ch) {
                    fmOut[ch].data[0] = (fmOut[ch].data[0] + second[ch].data[0]) / 2;
                }
            }
        }
        m_ssg_resampler.resample(&combined, 1);
    }

private:
    void ClockFmPerChannel(std::array<fm_engine::output_data, 3>& fmOut) {
        m_fm.clock(fm_engine::ALL_CHANNELS);
        for (std::uint32_t ch = 0; ch < 3; ++ch) {
            m_fm.output(fmOut[ch].clear(), 0, 32767, 1u << ch);
            // OPN is full 14-bit with no intermediate clipping; round-trip through the 10.3 float DAC truncation.
            fmOut[ch].roundtrip_fp();
        }
        m_last_fm_per_channel_sum = SumChannels(fmOut);
    }

    static std::int32_t SumChannels(const std::array<fm_engine::output_data, 3>& fmOut) {
        return fmOut[0].data[0] + fmOut[1].data[0] + fmOut[2].data[0];
    }

    std::int32_t m_last_fm_per_channel_sum = 0;
};

}  // namespace

// The ymfm_interface implementation + the chip instance. All ymfm types are
// confined here; the public header exposes only unique_ptr<Impl>.
struct Ym2203DeviceAdapter::Impl final : public ymfm::ymfm_interface {
    Impl(IrqHandler irqHandlerIn, PortWriteFn portAWriteIn, PortWriteFn portBWriteIn, PortReadFn portAReadIn,
         PortReadFn portBReadIn)
        : irq_handler(std::move(irqHandlerIn)),
          port_a_write(std::move(portAWriteIn)),
          port_b_write(std::move(portBWriteIn)),
          port_a_read(std::move(portAReadIn)),
          port_b_read(std::move(portBReadIn)),
          chip(*this) {}

    // --- ymfm_interface hooks -------------------------------------------
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

    void ymfm_set_busy_end(std::uint32_t clocks) override { busy_end_clocks = now_clocks + clocks; }

    bool ymfm_is_busy() override { return now_clocks < busy_end_clocks; }

    // SSG port A/B access (registers 14/15). ymfm routes these here only when the port's output-enable
    // bit (register 7) selects output.
    std::uint8_t ymfm_external_read(ymfm::access_class type, std::uint32_t address) override {
        if (type != ymfm::ACCESS_IO) {
            return 0;
        }
        if (address == 0 && port_a_read) {
            return port_a_read();
        }
        if (address == 1 && port_b_read) {
            return port_b_read();
        }
        return 0;
    }

    void ymfm_external_write(ymfm::access_class type, std::uint32_t address, std::uint8_t data) override {
        if (type != ymfm::ACCESS_IO) {
            return;
        }
        if (address == 0 && port_a_write) {
            port_a_write(data);
        } else if (address == 1 && port_b_write) {
            port_b_write(data);
        }
    }

    // --- helpers ----------------------------------------------------------
    void AdvanceOneSample() {
        now_clocks += Ym2203DeviceAdapter::kInputClocksPerSample;
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
        fm_per_channel = {};
        // chip.reset() drives the status/IRQ path, which calls back through
        // ymfm_update_irq() to deassert if it was high.
        chip.reset();
    }

    IrqHandler irq_handler;
    PortWriteFn port_a_write;
    PortWriteFn port_b_write;
    PortReadFn port_a_read;
    PortReadFn port_b_read;
    OpnPerChannel chip;

    // Last per-channel FM output ClockFmPerChannel() computed. ClockOneSample() re-clocks it only once every
    // m_fm_samples_per_output samples and otherwise shows the last value. It must persist across Render()
    // calls: the Machine calls Render() once per bus access, often with zero samples of budget, and a
    // call-local array would reset to zero each time (a single-sample spike).
    std::array<OpnPerChannel::FmChannelOutput, 3> fm_per_channel{};

    std::uint64_t now_clocks = 0;
    std::uint64_t busy_end_clocks = 0;
    std::uint64_t generated_samples = 0;
    std::uint64_t partial_clocks = 0;

    bool timer_active[2] = {false, false};
    std::uint64_t timer_deadline[2] = {0, 0};
    bool irq_asserted = false;
};

Ym2203DeviceAdapter::Ym2203DeviceAdapter(IrqHandler irq_handler, PortWriteFn portAWrite, PortWriteFn portBWrite,
                                         PortReadFn portARead, PortReadFn portBRead)
    : impl_(std::make_unique<Impl>(std::move(irq_handler), std::move(portAWrite), std::move(portBWrite),
                                    std::move(portARead), std::move(portBRead))) {
    // Like MAME's ym2203_device::device_start(): the fidelity must be set (MED, see the header) before first use.
    impl_->chip.set_fidelity(ymfm::OPN_FIDELITY_MED);
    impl_->ResetState();
}

Ym2203DeviceAdapter::~Ym2203DeviceAdapter() = default;

void Ym2203DeviceAdapter::Reset() { impl_->ResetState(); }

void Ym2203DeviceAdapter::WriteAddress(std::uint8_t value) { impl_->chip.write_address(value); }

void Ym2203DeviceAdapter::WriteData(std::uint8_t value) { impl_->chip.write_data(value); }

std::uint8_t Ym2203DeviceAdapter::ReadStatus() { return impl_->chip.read_status(); }

std::uint8_t Ym2203DeviceAdapter::ReadData() { return impl_->chip.read_data(); }

std::size_t Ym2203DeviceAdapter::Render(std::uint64_t inputClocks,
                                        std::vector<std::vector<float>>& channelSamples) {
    const std::uint64_t available = impl_->partial_clocks + inputClocks;
    const std::size_t sampleCount = static_cast<std::size_t>(available / kInputClocksPerSample);
    impl_->partial_clocks = available % kInputClocksPerSample;

    // impl_->fm_per_channel persists across Render() calls (the hold-between-re-clocks cadence needs it).
    ymfm::ym2203::output_data combined{};

    for (std::size_t i = 0; i < sampleCount; ++i) {
        impl_->chip.ClockOneSample(impl_->fm_per_channel, combined);
        impl_->AdvanceOneSample();

        for (std::uint32_t ch = 0; ch < kFmChannelCount; ++ch) {
            // ymfm's ~16-bit range -> [-1, 1].
            channelSamples[ch].push_back(static_cast<float>(impl_->fm_per_channel[ch].data[0]) / 32768.0f);
        }
        for (std::uint32_t ch = 0; ch < kSsgChannelCount; ++ch) {
            // SSG channels land at output_data indices 1..3, filled by the resampler in ClockOneSample().
            channelSamples[kFmChannelCount + ch].push_back(static_cast<float>(combined.data[1 + ch]) / 32768.0f);
        }
        ++impl_->generated_samples;
    }
    return sampleCount;
}

std::uint64_t Ym2203DeviceAdapter::GeneratedSampleCount() const { return impl_->generated_samples; }
