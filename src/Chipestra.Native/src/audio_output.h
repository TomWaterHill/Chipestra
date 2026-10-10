// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "mixer.h"

#include <cstddef>
#include <cstdint>

// IAudioOutput - contract for anywhere the canonical stereo mix can be sent.
// NullAudioOutput is the always-available reference implementation;
// WasapiSharedOutput implements the same contract for a real device. Nothing
// here knows anything about Machines or board specifics. Internal to Native: no
// chipestra_* function takes or returns this type.
class IAudioOutput {
public:
    virtual ~IAudioOutput() = default;

    // Deterministic setup (opens/validates format). Called exactly once,
    // before Start().
    virtual void Initialize() = 0;

    // Begins accepting SubmitStereo() calls.
    virtual void Start() = 0;

    // Stops accepting SubmitStereo() calls. Symmetric with Start(); safe to
    // call once Initialize() has run.
    virtual void Stop() = 0;

    // Submits `frameCount` StereoFrames of the canonical mix, in order.
    virtual void SubmitStereo(const StereoFrame* frames, std::size_t frameCount) = 0;

    // Total frames ever accepted by SubmitStereo() since Initialize().
    virtual std::uint64_t SubmittedFrameCount() const = 0;

    // Diagnostic counters. Implementations with no real queue/worker
    // (NullAudioOutput) report the fields that don't apply as zero. May be
    // called concurrently with SubmitStereo() from another thread (PlaybackPump
    // submits; the ABI layer/UI reads diagnostics), so implementations must
    // synchronize internally.
    virtual ChipestraAudioOutputDiagnostics GetDiagnostics() const = 0;

    // The endpoint's mix format, for outputs that have one (WasapiSharedOutput;
    // valid after Initialize()). Returns false for outputs with no such
    // concept (NullAudioOutput, the default).
    virtual bool GetFormat(ChipestraAudioOutputFormat* /*out*/) const { return false; }

    // Releases any resources acquired since Initialize(). Called exactly
    // once, right before this output is destroyed.
    virtual void Shutdown() = 0;
};
