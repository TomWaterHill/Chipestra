// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "test_machine.h"

void TestMachine::Initialize() {
    // No-op: deterministic, no ROM/audio device I/O. Channel registration
    // happens lazily on Render()'s first call instead, since Initialize()
    // has no sink to register with.
}

void TestMachine::Reset() {
    render_call_count_ = 0;
}

void TestMachine::Shutdown() {
    // No-op: nothing was acquired in Initialize.
}

std::vector<LogicalChannel>& TestMachine::LogicalChannels() {
    return channels_;
}

void TestMachine::Render(EmuDuration duration, LogicalChannelSink* sink) {
    ++render_call_count_;

    if (sink == nullptr) {
        return;
    }
    if (!sink->HasChannel(kChannelId)) {
        sink->RegisterChannel(kChannelId);
    }

    // Fixed, deterministic pattern (amplitude/shape are arbitrary; only
    // determinism matters here) - repeats every 4 samples.
    static constexpr float kPattern[4] = {0.5f, -0.5f, 0.25f, -0.25f};

    for (EmuTick i = 0; i < duration.ticks; ++i) {
        float sample = kPattern[i % 4];
        sink->WriteSamples(kChannelId, &sample, 1);
    }
}

void TestMachine::SendCommand(const MusicCommand& command) {
    (void)command;
    // No-op: TestMachine is a synthetic infra-test fixture with no sound
    // driver to inject commands into.
}

std::uint64_t TestMachine::RenderCallCount() const {
    return render_call_count_;
}
