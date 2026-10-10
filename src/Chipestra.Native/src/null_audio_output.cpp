// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "null_audio_output.h"

void NullAudioOutput::Initialize() {
    // No-op: opens no real device.
}

void NullAudioOutput::Start() {
    // No-op.
}

void NullAudioOutput::Stop() {
    // No-op.
}

void NullAudioOutput::SubmitStereo(const StereoFrame* frames, std::size_t frameCount) {
    (void)frames;  // counting only; never dereferenced.
    submitted_frame_count_ += frameCount;
}

std::uint64_t NullAudioOutput::SubmittedFrameCount() const {
    return submitted_frame_count_;
}

ChipestraAudioOutputDiagnostics NullAudioOutput::GetDiagnostics() const {
    // No real queue/worker: only submitted_frame_count is meaningful here.
    ChipestraAudioOutputDiagnostics diagnostics{};
    diagnostics.submitted_frame_count = submitted_frame_count_;
    return diagnostics;
}

void NullAudioOutput::Shutdown() {
    // No-op.
}
