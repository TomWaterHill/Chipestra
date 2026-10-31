// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "devices/sound/vrc7/vrc7_sound_device_adapter.h"

// ymfm is vendored unmodified (thirdparty/ymfm, BSD-3-Clause); wrap the includes so its headers don't trip /W4
// here, and pull in <intrin.h> first since ymfm.h uses _BitScanReverse under _MSC_VER without it.
#ifdef _MSC_VER
#include <intrin.h>
#pragma warning(push, 0)
#endif
#include "ymfm/ymfm.h"
#include "ymfm/ymfm_opl.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <stdexcept>
#include <utility>

namespace {

// ymfm's opll_base::generate() mixes all channels in one call. To publish one value per channel, this
// subclass reaches the protected fm_engine and repeats generate()'s steps with a one-channel mask.
class Ds1001PerChannel final : public ymfm::ds1001 {
public:
    Ds1001PerChannel(ymfm::ymfm_interface& intf, const std::uint8_t* table) : ymfm::ds1001(intf, table) {}

    // One output sample. `raw[ch]` is each channel's value as the engine makes it (before the final scaling).
    void ClockOneSample(std::int32_t (&raw)[Vrc7SoundDeviceAdapter::kChannelCount]) {
        m_fm.clock(fm_engine::ALL_CHANNELS);
        for (std::size_t ch = 0; ch < Vrc7SoundDeviceAdapter::kChannelCount; ++ch) {
            output_data out;
            m_fm.output(out.clear(), 5, 256, 1u << ch);
            raw[ch] = out.data[0];
        }
    }
};

// opll_base::generate()'s final scaling of the summed value.
std::int32_t Scale(std::int32_t sum) { return (sum * 128) / 9; }

}  // namespace

struct Vrc7SoundDeviceAdapter::Impl {
    explicit Impl(const std::vector<std::uint8_t>& table) : table_(table), chip_(interface_, table_.data()) {}

    ymfm::ymfm_interface interface_;
    std::vector<std::uint8_t> table_;  // ymfm keeps the pointer
    Ds1001PerChannel chip_;
    std::uint8_t address_ = 0;
    std::uint8_t muted_ = 0;
    std::uint64_t clock_remainder_ = 0;
    std::uint64_t samples_ = 0;
};

Vrc7SoundDeviceAdapter::Vrc7SoundDeviceAdapter(const std::vector<std::uint8_t>& instrumentTable) {
    if (instrumentTable.size() != kInstrumentTableSize)
        throw std::invalid_argument("VRC7 instrument table: the table must be exactly 144 bytes");
    impl_ = std::make_unique<Impl>(instrumentTable);
    Reset();
}

Vrc7SoundDeviceAdapter::~Vrc7SoundDeviceAdapter() = default;

void Vrc7SoundDeviceAdapter::Reset() {
    impl_->chip_.reset();
    impl_->address_ = 0;
    impl_->clock_remainder_ = 0;
    impl_->samples_ = 0;
}

void Vrc7SoundDeviceAdapter::WriteAddress(std::uint8_t value) {
    impl_->address_ = value;
    impl_->chip_.write_address(value);
}

void Vrc7SoundDeviceAdapter::WriteData(std::uint8_t value) {
    const std::uint8_t reg = impl_->address_;
    // The rhythm control and the channels 7-9 (registers x6-x8) are not on the VRC7.
    const bool perChannel = reg >= 0x10 && reg <= 0x3F;
    if (reg == 0x0E || (perChannel && (reg & 0x0F) >= kChannelCount)) return;
    impl_->chip_.write_data(value);
}

void Vrc7SoundDeviceAdapter::SetMutedChannels(std::uint8_t mask) { impl_->muted_ = mask; }

std::size_t Vrc7SoundDeviceAdapter::Render(std::uint64_t inputClocks,
                                           std::array<std::vector<float>, kChannelCount>& channelOut) {
    impl_->clock_remainder_ += inputClocks;
    const std::uint64_t count = impl_->clock_remainder_ / kInputClocksPerSample;
    impl_->clock_remainder_ %= kInputClocksPerSample;
    for (auto& v : channelOut) v.reserve(v.size() + static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i) {
        std::int32_t raw[kChannelCount];
        impl_->chip_.ClockOneSample(raw);
        // Each channel gets the difference of the scaled running sums, so the six add up to Scale(total).
        std::int32_t prefix = 0;
        std::int32_t scaledBefore = 0;
        for (std::size_t ch = 0; ch < kChannelCount; ++ch) {
            prefix += raw[ch];
            const std::int32_t scaled = Scale(prefix);
            const std::int32_t share = scaled - scaledBefore;
            scaledBefore = scaled;
            const bool muted = ((impl_->muted_ >> ch) & 1) != 0;
            channelOut[ch].push_back(muted ? 0.0f : static_cast<float>(share) / 32768.0f);
        }
    }
    impl_->samples_ += count;
    return static_cast<std::size_t>(count);
}

std::uint64_t Vrc7SoundDeviceAdapter::GeneratedSampleCountForTest() const { return impl_->samples_; }
