// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "audio_output.h"

// NullAudioOutput - the reference IAudioOutput every Machine must be renderable
// through: opens no real device and only counts submitted frames.
class NullAudioOutput final : public IAudioOutput {
public:
    void Initialize() override;
    void Start() override;
    void Stop() override;
    void SubmitStereo(const StereoFrame* frames, std::size_t frameCount) override;
    std::uint64_t SubmittedFrameCount() const override;
    ChipestraAudioOutputDiagnostics GetDiagnostics() const override;
    void Shutdown() override;

private:
    std::uint64_t submitted_frame_count_ = 0;
};
