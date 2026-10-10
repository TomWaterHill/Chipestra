// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "dac8bitr2r_device_adapter.h"

#include <algorithm>

void Dac8BitR2RDeviceAdapter::Reconstruct(std::vector<float>& out, std::size_t tickCount, double ticksPerCycle) {
    out.assign(tickCount, 0.0f);

    std::size_t filledTo = 0;
    float carry = holdBeforeBlock_;
    for (const WriteEvent& ev : events_) {
        auto tickOffset = static_cast<std::size_t>(static_cast<double>(ev.cyclesInBlock) * ticksPerCycle);
        tickOffset = std::min(tickOffset, out.size());
        for (std::size_t i = filledTo; i < tickOffset; ++i) out[i] = carry;
        filledTo = tickOffset;
        carry = (static_cast<float>(ev.rawByte) - 128.0f) / 128.0f * gain_;
    }
    for (std::size_t i = filledTo; i < out.size(); ++i) out[i] = carry;
    holdBeforeBlock_ = carry;
}
