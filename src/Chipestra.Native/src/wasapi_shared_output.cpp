// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "wasapi_shared_output.h"

#include "bounded_frame_queue.h"
#include "endpoint_resampler.h"
#include "mixer.h"

// NOMINMAX: <windows.h> (pulled in below) otherwise defines min/max macros
// that break std::min/std::max.
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

void ThrowIfFailed(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        throw std::runtime_error(what);
    }
}

// {00000003-0000-0010-8000-00AA00389B71}, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT.
// Defined locally to avoid depending on which system header/library provides
// that symbol's storage.
constexpr GUID kSubTypeIeeeFloat = {
    0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};

// KSDATAFORMAT_SUBTYPE_PCM, defined locally for the same reason.
constexpr GUID kSubTypePcm = {
    0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};

bool IsIeeeFloatSubFormat(const GUID& subFormat) {
    return IsEqualGUID(subFormat, kSubTypeIeeeFloat) != 0;
}

}  // namespace

struct WasapiSharedOutput::Impl {
    bool com_initialized = false;
    ComPtr<IMMDeviceEnumerator> device_enumerator;
    ComPtr<IMMDevice> device;
    ComPtr<IAudioClient> audio_client;
    ComPtr<IAudioRenderClient> render_client;
    WAVEFORMATEX* mix_format = nullptr;  // owned; CoTaskMemFree'd in ~Impl
    UINT32 buffer_frame_capacity = 0;
    bool started = false;

    std::unique_ptr<BoundedFrameQueue> queue;

    std::uint32_t requested_rate_hz = 0;
    ChipestraAudioOutputFormat format_info{};
    bool format_known = false;

    // Canonical (192kHz) -> endpoint-rate converter. Created in Initialize()
    // once the mix format is known. Only the PlaybackPump thread touches it.
    std::unique_ptr<EndpointResampler> resampler;

    // Guards every counter below; SubmitStereo() (PlaybackPump thread) and
    // GetDiagnostics() (ABI/UI thread) can run concurrently.
    mutable std::mutex diagnostics_mutex;
    std::uint64_t submitted_frame_count = 0;
    std::uint64_t worker_wake_count = 0;
    std::uint64_t queue_drain_count = 0;
    std::uint64_t queue_overflow_count = 0;
    std::uint64_t last_padding_frames = 0;

    ~Impl() {
        if (mix_format != nullptr) {
            CoTaskMemFree(mix_format);
        }
        if (com_initialized) {
            CoUninitialize();
        }
    }

    // Writes `frames` into `data` (a buffer from GetBuffer(), sized for
    // `frames.size()` frames of mix_format) in the endpoint's sample format.
    // The int16 path clamps as a final safety net; the float32 path does not,
    // relying on Mixer::MixBlock's own clamp.
    void WriteFrames(BYTE* data, const std::vector<StereoFrame>& frames) const {
        bool is_float = mix_format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
        if (mix_format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
            const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(mix_format);
            is_float = IsIeeeFloatSubFormat(ext->SubFormat);
        }
        UINT16 channels = mix_format->nChannels;

        if (is_float && mix_format->wBitsPerSample == 32) {
            auto* out = reinterpret_cast<float*>(data);
            for (const StereoFrame& frame : frames) {
                for (UINT16 ch = 0; ch < channels; ++ch) {
                    *out++ = (ch == 0) ? frame.left : (ch == 1 ? frame.right : 0.0f);
                }
            }
            return;
        }

        if (!is_float && mix_format->wBitsPerSample == 16) {
            auto clamp16 = [](float v) -> std::int16_t {
                float scaled = v * 32767.0f;
                scaled = std::min(scaled, 32767.0f);
                scaled = std::max(scaled, -32768.0f);
                return static_cast<std::int16_t>(scaled);
            };
            auto* out = reinterpret_cast<std::int16_t*>(data);
            for (const StereoFrame& frame : frames) {
                for (UINT16 ch = 0; ch < channels; ++ch) {
                    *out++ = (ch == 0) ? clamp16(frame.left) : (ch == 1 ? clamp16(frame.right) : std::int16_t{0});
                }
            }
            return;
        }

        // Unsupported endpoint sample format (e.g. 24-bit packed): write silence
        // rather than garbage. Initialize() only accepts float32/int16, so this
        // should not be reachable.
        std::memset(data, 0, static_cast<std::size_t>(frames.size()) * channels * (mix_format->wBitsPerSample / 8));
    }
};

WasapiSharedOutput::WasapiSharedOutput() : impl_(std::make_unique<Impl>()) {}

WasapiSharedOutput::~WasapiSharedOutput() = default;

void WasapiSharedOutput::SetRequestedRateHz(std::uint32_t requestedRateHz) {
    impl_->requested_rate_hz = requestedRateHz;
}

bool WasapiSharedOutput::GetFormat(ChipestraAudioOutputFormat* out) const {
    if (out == nullptr || !impl_->format_known) {
        return false;
    }
    *out = impl_->format_info;
    return true;
}

void WasapiSharedOutput::Initialize() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr)) {
        impl_->com_initialized = true;
    } else if (hr != RPC_E_CHANGED_MODE) {
        // RPC_E_CHANGED_MODE: this thread already has COM initialized with a
        // different concurrency model - still usable, not a failure of ours.
        ThrowIfFailed(hr, "CoInitializeEx failed");
    }

    ThrowIfFailed(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                    __uuidof(IMMDeviceEnumerator), &impl_->device_enumerator),
                  "CoCreateInstance(MMDeviceEnumerator) failed");

    ThrowIfFailed(impl_->device_enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &impl_->device),
                  "GetDefaultAudioEndpoint failed (no default render device?)");

    ThrowIfFailed(impl_->device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &impl_->audio_client),
                  "IMMDevice::Activate(IAudioClient) failed");

    ThrowIfFailed(impl_->audio_client->GetMixFormat(&impl_->mix_format), "GetMixFormat failed");

    bool is_float_format =
        impl_->mix_format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
        (impl_->mix_format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
         IsIeeeFloatSubFormat(reinterpret_cast<WAVEFORMATEXTENSIBLE*>(impl_->mix_format)->SubFormat));
    // Record the mix format first so a rejection below can report exactly what
    // the device offered.
    {
        const WAVEFORMATEX* f = impl_->mix_format;
        const bool extensible = f->wFormatTag == WAVE_FORMAT_EXTENSIBLE;
        bool is_pcm = f->wFormatTag == WAVE_FORMAT_PCM;
        if (extensible) {
            const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(f);
            is_pcm = IsEqualGUID(ext->SubFormat, kSubTypePcm) != 0;
        }
        ChipestraAudioOutputFormat info{};
        info.requested_rate_hz = impl_->requested_rate_hz;
        info.endpoint_rate_hz = f->nSamplesPerSec;
        info.bits_per_sample = f->wBitsPerSample;
        info.valid_bits = extensible ? reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(f)->Samples.wValidBitsPerSample
                                     : f->wBitsPerSample;
        info.channels = f->nChannels;
        info.sample_format = is_float_format ? CHIPESTRA_SAMPLE_FORMAT_FLOAT
                                             : (is_pcm ? CHIPESTRA_SAMPLE_FORMAT_PCM : CHIPESTRA_SAMPLE_FORMAT_OTHER);
        info.is_extensible = extensible ? 1u : 0u;
        info.selection_honored =
            (impl_->requested_rate_hz == 0 || impl_->requested_rate_hz == f->nSamplesPerSec) ? 1u : 0u;
        impl_->format_info = info;
        impl_->format_known = true;
    }

    bool format_ok = (impl_->mix_format->wBitsPerSample == 32 && is_float_format) ||
                      (impl_->mix_format->wBitsPerSample == 16 && impl_->mix_format->wFormatTag == WAVE_FORMAT_PCM);
    if (!format_ok) {
        throw UnsupportedEndpointFormatError(impl_->format_info);
    }

    // 0 duration lets WASAPI pick a default buffer size for Shared mode.
    ThrowIfFailed(
        impl_->audio_client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 0, 0, impl_->mix_format, nullptr),
        "IAudioClient::Initialize failed");

    ThrowIfFailed(impl_->audio_client->GetBufferSize(&impl_->buffer_frame_capacity), "GetBufferSize failed");

    ThrowIfFailed(impl_->audio_client->GetService(__uuidof(IAudioRenderClient), &impl_->render_client),
                  "GetService(IAudioRenderClient) failed");

    // Several buffers' worth of headroom so a couple of slow drain attempts
    // don't immediately overflow the queue.
    std::size_t queue_capacity = static_cast<std::size_t>(impl_->buffer_frame_capacity) * 4;
    impl_->queue = std::make_unique<BoundedFrameQueue>(queue_capacity);

    impl_->resampler = std::make_unique<EndpointResampler>(impl_->mix_format->nSamplesPerSec);
}

void WasapiSharedOutput::Start() {
    ThrowIfFailed(impl_->audio_client->Start(), "IAudioClient::Start failed");
    impl_->started = true;
}

void WasapiSharedOutput::Stop() {
    if (impl_->started) {
        impl_->audio_client->Stop();
        impl_->started = false;
    }
    impl_->queue->Clear();
    if (impl_->resampler != nullptr) {
        impl_->resampler->Reset();
    }
}

void WasapiSharedOutput::SubmitStereo(const StereoFrame* frames, std::size_t frameCount) {
    {
        // Reported count is canonical input frames, so callers see the amount
        // they submitted regardless of the internal resampling.
        std::lock_guard<std::mutex> lock(impl_->diagnostics_mutex);
        impl_->submitted_frame_count += frameCount;
    }

    // Resample the canonical 192kHz block to the endpoint rate (identity copy
    // when the endpoint runs at 192kHz). The queue/drain path then works
    // entirely in endpoint-rate frames.
    const std::vector<StereoFrame>& endpointFrames = impl_->resampler->ProcessBlock(frames, frameCount);
    const std::size_t pushCount = endpointFrames.size();

    std::size_t pushed = impl_->queue->Push(endpointFrames.data(), pushCount);
    if (pushed < pushCount) {
        std::lock_guard<std::mutex> lock(impl_->diagnostics_mutex);
        impl_->queue_overflow_count += (pushCount - pushed);
    }

    DrainIntoEndpoint();
}

void WasapiSharedOutput::DrainIntoEndpoint() {
    std::lock_guard<std::mutex> lock(impl_->diagnostics_mutex);
    ++impl_->worker_wake_count;

    UINT32 padding = 0;
    if (FAILED(impl_->audio_client->GetCurrentPadding(&padding))) {
        return;  // best-effort; the next SubmitStereo() will try again
    }
    impl_->last_padding_frames = padding;

    UINT32 available = impl_->buffer_frame_capacity - padding;
    std::size_t queued = impl_->queue->Size();
    std::size_t to_write = std::min<std::size_t>(available, queued);
    if (to_write == 0) {
        return;
    }

    BYTE* data = nullptr;
    if (FAILED(impl_->render_client->GetBuffer(static_cast<UINT32>(to_write), &data))) {
        return;
    }

    std::vector<StereoFrame> popped(to_write);
    impl_->queue->Pop(popped.data(), to_write);
    impl_->WriteFrames(data, popped);

    impl_->render_client->ReleaseBuffer(static_cast<UINT32>(to_write), 0);
    impl_->queue_drain_count += to_write;
}

std::uint64_t WasapiSharedOutput::SubmittedFrameCount() const {
    std::lock_guard<std::mutex> lock(impl_->diagnostics_mutex);
    return impl_->submitted_frame_count;
}

std::uint32_t WasapiSharedOutput::EndpointSampleRateHz() const {
    return impl_->mix_format != nullptr ? impl_->mix_format->nSamplesPerSec : 0;
}

ChipestraAudioOutputDiagnostics WasapiSharedOutput::GetDiagnostics() const {
    std::lock_guard<std::mutex> lock(impl_->diagnostics_mutex);
    ChipestraAudioOutputDiagnostics diagnostics{};
    diagnostics.submitted_frame_count = impl_->submitted_frame_count;
    diagnostics.worker_wake_count = impl_->worker_wake_count;
    diagnostics.queue_drain_count = impl_->queue_drain_count;
    diagnostics.queue_overflow_count = impl_->queue_overflow_count;
    diagnostics.queued_frame_count = impl_->queue->Size();
    diagnostics.buffer_frame_capacity = impl_->buffer_frame_capacity;
    diagnostics.last_padding_frames = impl_->last_padding_frames;
    return diagnostics;
}

void WasapiSharedOutput::Shutdown() {
    impl_->render_client.Reset();
    impl_->audio_client.Reset();
    impl_->device.Reset();
    impl_->device_enumerator.Reset();
}
