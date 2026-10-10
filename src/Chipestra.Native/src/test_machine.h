// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "chipestra_abi.h"

#include <cstdint>
#include <vector>

// TestMachine - a synthetic IMachine used to validate the Native Runtime's
// instance/Machine lifecycle and TestMachine->Mixer->Output data flow without any ROM
// or real audio device. It performs no emulation of its own: Render increments a counter
// and writes a fixed, deterministic waveform into its one LogicalChannel.
class TestMachine final : public IMachine {
public:
    // The ChannelId TestMachine registers with its sink in Initialize() and
    // writes to in Render(). Fixed since TestMachine only ever has one
    // channel (LogicalChannels().size() == 1).
    static constexpr ChannelId kChannelId = 0;

    void Initialize() override;
    void Reset() override;
    void Shutdown() override;
    std::vector<LogicalChannel>& LogicalChannels() override;
    void Render(EmuDuration duration, LogicalChannelSink* sink) override;
    void SendCommand(const MusicCommand& command) override;

    // Test-only observation point: total Render calls since the last Reset
    // (or since Initialize, if Reset hasn't been called yet).
    std::uint64_t RenderCallCount() const;

private:
    std::uint64_t render_call_count_ = 0;
    std::vector<LogicalChannel> channels_{LogicalChannel{kChannelId, "TEST_MACHINE", "Channel 0"}};
};
