// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "audio_output.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>

// WasapiSharedOutput - IAudioOutput over the Windows default audio render
// endpoint, opened in WASAPI Shared mode.
//
// No separate worker thread: PlaybackPump is the only thread that touches an
// IAudioOutput after construction. SubmitStereo() both enqueues into the
// bounded queue and immediately drains as much as currently fits into the
// endpoint's buffer (checking GetCurrentPadding()); that attempt is what
// WorkerWakeCount counts.
//
// Initialize() opens the stream using the endpoint's own GetMixFormat() (the
// one format Shared mode always accepts). SubmitStereo() takes the canonical
// 192kHz mix and resamples it to the endpoint's rate before queueing (skipped
// if the endpoint already runs at Mixer::kCanonicalSampleRateHz). The queue,
// drain and diagnostics all operate at the endpoint rate.
// Thrown by WasapiSharedOutput::Initialize() when the endpoint's mix format is
// not one it can write (not float32 / 16-bit PCM). Carries the format so the
// ABI layer can report exactly what the device offered.
class UnsupportedEndpointFormatError : public std::runtime_error {
public:
    explicit UnsupportedEndpointFormatError(const ChipestraAudioOutputFormat& format)
        : std::runtime_error("WasapiSharedOutput: unsupported endpoint mix format"), format_(format) {}
    const ChipestraAudioOutputFormat& Format() const { return format_; }

private:
    ChipestraAudioOutputFormat format_;
};

class WasapiSharedOutput final : public IAudioOutput {
public:
    WasapiSharedOutput();
    ~WasapiSharedOutput() override;

    WasapiSharedOutput(const WasapiSharedOutput&) = delete;
    WasapiSharedOutput& operator=(const WasapiSharedOutput&) = delete;

    // Advisory only (shared mode follows the endpoint's rate - see
    // chipestra_select_wasapi_shared_output): recorded for GetFormat(). Call before
    // Initialize(). 0 = auto.
    void SetRequestedRateHz(std::uint32_t requestedRateHz);
    bool GetFormat(ChipestraAudioOutputFormat* out) const override;

    void Initialize() override;
    void Start() override;
    void Stop() override;
    void SubmitStereo(const StereoFrame* frames, std::size_t frameCount) override;
    std::uint64_t SubmittedFrameCount() const override;

    // The endpoint's actual mix-format sample rate (valid after Initialize()).
    // The endpoint's actual mix-format sample rate (valid after Initialize());
    // 0 before Initialize(). SubmitStereo() resamples to this rate internally;
    std::uint32_t EndpointSampleRateHz() const;
    ChipestraAudioOutputDiagnostics GetDiagnostics() const override;
    void Shutdown() override;

private:
    void DrainIntoEndpoint();

    // Opaque pimpl: keeps <mmdeviceapi.h>/<audioclient.h>/<wrl/client.h> out of
    // this header, so other code does not see raw COM types.
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
