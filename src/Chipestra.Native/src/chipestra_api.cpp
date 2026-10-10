// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "chipestra_abi.h"

#include "default_machine_catalog.h"
#include "game_definition.h"
#include "machine_registry.h"
#include "mixer.h"
#include "null_audio_output.h"
#include "playback_pump.h"
#include "resampling_mixer_feed.h"
#include "resource_resolver.h"
#include "test_machine.h"
#include "wasapi_shared_output.h"
#include "xml_definition_parser.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string_view>
#include <unordered_set>

namespace {
constexpr uint32_t kApiVersion = 4;  // bumped on any breaking C ABI change
}

extern "C" uint32_t chipestra_get_api_version(void) noexcept {
    return kApiVersion;
}

// ChipestraInstanceOpaque is intentionally defined only here: the header forward-
// declares it so callers can never depend on its layout (see chipestra_abi.h).
struct ChipestraInstanceOpaque {
    enum class State { Created, MachineLoaded, Ready, Playing, Paused };

    State state = State::Created;
    std::unique_ptr<IMachine> machine;
    std::unique_ptr<IAudioOutput> output;
    // The mix format a failed chipestra_select_wasapi_shared_output rejected
    // (CHIPESTRA_E_UNSUPPORTED_FORMAT), for chipestra_get_audio_output_format.
    ChipestraAudioOutputFormat last_rejected_format{};
    bool has_last_rejected_format = false;
    Mixer mixer;
    LogicalChannelSink sink;          // Machine writes here at each channel's native rate
    ResamplingMixerFeed resampler;    // native-rate sink -> canonical-rate sink
    std::unique_ptr<PlaybackPump> pump;

    // Channel Meter group - guards every LogicalChannel element's Mute/Pan/Gain/Peak/RMS
    // against the PlaybackPump thread's MixBlock call (which reads Mute/Pan/Gain and writes
    // Peak/RMS once per render block) racing the ABI getter/setters below (called from the UI
    // thread). Not needed for LogicalChannels().size() itself - it is immutable after Initialize().
    std::mutex channels_mutex;

    // Master section state - independent atomics rather than channels_mutex, since these apply
    // to the final mixed StereoFrame block (after MixBlock), not to any LogicalChannel element.
    std::atomic<float> master_gain{1.0f};
    std::atomic<bool> master_muted{false};
    std::atomic<float> master_peak_left{0.0f};
    std::atomic<float> master_peak_right{0.0f};
    std::atomic<float> master_rms_left{0.0f};
    std::atomic<float> master_rms_right{0.0f};
};

namespace {

// Tracks every instance currently between chipestra_create_instance and a
// successful chipestra_destroy_instance. This is what lets chipestra_destroy_instance
// (and the Machine/Output lifecycle functions below) detect "already
// destroyed" without touching (UB) potentially-freed memory through a stale
// handle, giving double-destroy safety.
std::mutex g_liveInstancesMutex;
std::unordered_set<ChipestraInstanceOpaque*> g_liveInstances;

// True if `handle` currently owns a live (not-yet-destroyed) instance.
bool IsLiveInstance(ChipestraHandle handle) {
    std::lock_guard<std::mutex> lock(g_liveInstancesMutex);
    return g_liveInstances.find(handle) != g_liveInstances.end();
}

}  // namespace

extern "C" ChipestraResult chipestra_create_instance(ChipestraHandle* out_handle) noexcept {
    if (out_handle == nullptr) {
        return CHIPESTRA_E_INVALID_ARGUMENT;
    }
    try {
        auto* instance = new ChipestraInstanceOpaque();
        std::lock_guard<std::mutex> lock(g_liveInstancesMutex);
        g_liveInstances.insert(instance);
        *out_handle = instance;
        return CHIPESTRA_OK;
    } catch (...) {
        return CHIPESTRA_E_INTERNAL;
    }
}

extern "C" void chipestra_destroy_instance(ChipestraHandle handle) noexcept {
    if (handle == nullptr) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_liveInstancesMutex);
        auto it = g_liveInstances.find(handle);
        if (it == g_liveInstances.end()) {
            // Unknown handle, or already destroyed by an earlier call: ignore.
            return;
        }
        g_liveInstances.erase(it);
    }
    // PlaybackPump first, before Output or Machine: its worker thread calls
    // back into both, so it must be stopped and joined before either is
    // torn down, or the pump thread could touch freed memory mid-render.
    if (handle->pump) {
        handle->pump->Stop();
        handle->pump.reset();
    }
    // Output before Machine: stop the downstream consumer before tearing
    // down the upstream source.
    if (handle->output) {
        try {
            handle->output->Stop();
            handle->output->Shutdown();
        } catch (...) {
            // Best-effort teardown; the instance is being destroyed regardless.
        }
    }
    if (handle->machine) {
        try {
            handle->machine->Shutdown();
        } catch (...) {
            // Best-effort teardown; the instance is being destroyed regardless.
        }
    }
    delete handle;
}

extern "C" ChipestraResult chipestra_load_machine(ChipestraHandle handle, const char* machine_id, const char* variant_id,
                                       const ChipestraResolvedResource* resources, uint32_t resource_count) noexcept {
    if (handle == nullptr || machine_id == nullptr || variant_id == nullptr) {
        return CHIPESTRA_E_INVALID_ARGUMENT;
    }
    if (resource_count > 0 && resources == nullptr) {
        return CHIPESTRA_E_INVALID_ARGUMENT;
    }
    for (uint32_t i = 0; i < resource_count; ++i) {
        if (resources[i].size > 0 && resources[i].data == nullptr) {
            return CHIPESTRA_E_INVALID_ARGUMENT;
        }
    }
    if (!IsLiveInstance(handle)) {
        return CHIPESTRA_E_INVALID_STATE;
    }
    if (handle->state != ChipestraInstanceOpaque::State::Created) {
        return CHIPESTRA_E_INVALID_STATE;
    }

    try {
        // Build a MachineResources view over the caller's resolved bytes -
        // ResolvedResource's constructor makes its own owned copy of each resource's
        // bytes, so this MachineResources stays valid independent of `resources`'s own
        // lifetime (the caller's ChipestraResourceResolutionHandle) once this loop returns.
        MachineResources machineResources;
        for (uint32_t i = 0; i < resource_count; ++i) {
            const ChipestraResolvedResource& resource = resources[i];
            const std::uint8_t* data = resource.data;
            std::vector<std::uint8_t> bytes(data, data + resource.size);
            machineResources.Add(ResolvedResource(std::string(resource.logical_name), std::move(bytes)));
        }

        auto machine =
            DefaultMachineRegistry().Create(MachineId(machine_id), VariantId(variant_id), machineResources);
        if (!machine) {
            // Unknown machine_id, or known machine_id with a variant_id
            // that doesn't exactly match - both map to CHIPESTRA_E_NOT_FOUND
            // (no fuzzy matching, no distinguishing the two).
            return CHIPESTRA_E_NOT_FOUND;
        }
        machine->Initialize();
        handle->machine = std::move(machine);
        handle->state = ChipestraInstanceOpaque::State::MachineLoaded;
        return CHIPESTRA_OK;
    } catch (...) {
        return CHIPESTRA_E_INTERNAL;
    }
}

extern "C" ChipestraResult chipestra_unload_machine(ChipestraHandle handle) noexcept {
    if (handle == nullptr) {
        return CHIPESTRA_E_INVALID_ARGUMENT;
    }
    if (!IsLiveInstance(handle)) {
        return CHIPESTRA_E_INVALID_STATE;
    }
    if (handle->state != ChipestraInstanceOpaque::State::MachineLoaded) {
        return CHIPESTRA_E_INVALID_STATE;
    }

    ChipestraResult result = CHIPESTRA_OK;
    try {
        handle->machine->Shutdown();
    } catch (...) {
        result = CHIPESTRA_E_INTERNAL;
    }
    handle->machine.reset();
    handle->state = ChipestraInstanceOpaque::State::Created;
    return result;
}

extern "C" ChipestraResult chipestra_reset_machine(ChipestraHandle handle) noexcept {
    if (handle == nullptr) {
        return CHIPESTRA_E_INVALID_ARGUMENT;
    }
    if (!IsLiveInstance(handle)) {
        return CHIPESTRA_E_INVALID_STATE;
    }
    if (handle->state != ChipestraInstanceOpaque::State::MachineLoaded) {
        return CHIPESTRA_E_INVALID_STATE;
    }

    try {
        handle->machine->Reset();
        handle->resampler.Reset();
        return CHIPESTRA_OK;
    } catch (...) {
        return CHIPESTRA_E_INTERNAL;
    }
}

extern "C" ChipestraResult chipestra_get_machine_channel_count(ChipestraHandle handle, uint32_t* out_count) noexcept {
    if (handle == nullptr || out_count == nullptr) {
        return CHIPESTRA_E_INVALID_ARGUMENT;
    }
    if (!IsLiveInstance(handle)) {
        return CHIPESTRA_E_INVALID_STATE;
    }
    // Valid in any state that has a Machine loaded, Playing included - see
    // this function's own comment in chipestra_abi.h for why that's safe (the
    // LogicalChannels() vector's size is fixed after Initialize(), unlike
    // element contents like Peak/RMS which the Mixer does mutate concurrently
    // during Render()).
    if (handle->state == ChipestraInstanceOpaque::State::Created) {
        return CHIPESTRA_E_INVALID_STATE;
    }

    try {
        *out_count = static_cast<uint32_t>(handle->machine->LogicalChannels().size());
        return CHIPESTRA_OK;
    } catch (...) {
        return CHIPESTRA_E_INTERNAL;
    }
}

extern "C" ChipestraResult chipestra_send_command(ChipestraHandle handle, uint8_t kind, uint8_t value, uint8_t auxiliary) noexcept {
    if (handle == nullptr || kind > static_cast<uint8_t>(MusicCommandKind::Wsg)) {
        return CHIPESTRA_E_INVALID_ARGUMENT;
    }
    if (!IsLiveInstance(handle)) {
        return CHIPESTRA_E_INVALID_STATE;
    }
    // Not Playing: the PlaybackPump thread owns the Machine while playback is
    // running (playback_pump.h), so SendCommand must not be called
    // concurrently with it. Created has no Machine at all.
    if (handle->state != ChipestraInstanceOpaque::State::MachineLoaded &&
        handle->state != ChipestraInstanceOpaque::State::Ready &&
        handle->state != ChipestraInstanceOpaque::State::Paused) {
        return CHIPESTRA_E_INVALID_STATE;
    }

    try {
        MusicCommand command;
        command.Kind = static_cast<MusicCommandKind>(kind);
        command.Value = value;
        command.Auxiliary = auxiliary;
        if (!handle->machine->AcceptsCommand(command)) {
            return CHIPESTRA_E_UNSUPPORTED_COMMAND;
        }
        handle->machine->SendCommand(command);
        // The level readings (Peak/RMS/SourcePeak...) are only rewritten by the next rendered block, so
        // until then they would still show the PREVIOUS song. Clear them: a new command starts a new
        // song, and the auto-stop-by-silence check must not take the old song's sound for the new one's.
        // Not Playing here (checked above), so the mixer is not writing them.
        {
            std::lock_guard<std::mutex> lock(handle->channels_mutex);
            for (LogicalChannel& channel : handle->machine->LogicalChannels()) {
                channel.Peak = channel.RMS = 0.0f;
                channel.PeakLeft = channel.PeakRight = channel.RMSLeft = channel.RMSRight = 0.0f;
                channel.SourcePeak = 0.0f;
            }
        }
        return CHIPESTRA_OK;
    } catch (...) {
        return CHIPESTRA_E_INTERNAL;
    }
}

extern "C" ChipestraResult chipestra_select_null_output(ChipestraHandle handle) noexcept {
    if (handle == nullptr) {
        return CHIPESTRA_E_INVALID_ARGUMENT;
    }
    if (!IsLiveInstance(handle)) {
        return CHIPESTRA_E_INVALID_STATE;
    }
    if (handle->state != ChipestraInstanceOpaque::State::MachineLoaded || handle->output) {
        // Already has a selected output: require chipestra_stop_audio_output
        // first rather than silently replacing it (which would skip the
        // old output's Shutdown()).
        return CHIPESTRA_E_INVALID_STATE;
    }

    try {
        auto output = std::make_unique<NullAudioOutput>();
        output->Initialize();
        handle->output = std::move(output);
        return CHIPESTRA_OK;
    } catch (...) {
        return CHIPESTRA_E_INTERNAL;
    }
}

extern "C" ChipestraResult chipestra_start_audio_output(ChipestraHandle handle) noexcept {
    if (handle == nullptr) {
        return CHIPESTRA_E_INVALID_ARGUMENT;
    }
    if (!IsLiveInstance(handle)) {
        return CHIPESTRA_E_INVALID_STATE;
    }
    if (handle->state != ChipestraInstanceOpaque::State::MachineLoaded || !handle->output) {
        return CHIPESTRA_E_INVALID_STATE;
    }

    try {
        handle->output->Start();
        handle->state = ChipestraInstanceOpaque::State::Ready;
        return CHIPESTRA_OK;
    } catch (...) {
        return CHIPESTRA_E_INTERNAL;
    }
}

extern "C" ChipestraResult chipestra_stop_audio_output(ChipestraHandle handle) noexcept {
    if (handle == nullptr) {
        return CHIPESTRA_E_INVALID_ARGUMENT;
    }
    if (!IsLiveInstance(handle)) {
        return CHIPESTRA_E_INVALID_STATE;
    }
    if (handle->state != ChipestraInstanceOpaque::State::Ready) {
        // Playing/Paused must go through chipestra_stop_playback first (Playing/
        // Paused -> Ready), keeping teardown strictly ordered.
        return CHIPESTRA_E_INVALID_STATE;
    }

    ChipestraResult result = CHIPESTRA_OK;
    try {
        handle->output->Stop();
        handle->output->Shutdown();
    } catch (...) {
        result = CHIPESTRA_E_INTERNAL;
    }
    handle->output.reset();
    handle->state = ChipestraInstanceOpaque::State::MachineLoaded;
    return result;
}

extern "C" ChipestraResult chipestra_select_wasapi_shared_output(ChipestraHandle handle, uint32_t requested_rate_hz) noexcept {
    if (handle == nullptr) {
        return CHIPESTRA_E_INVALID_ARGUMENT;
    }
    switch (requested_rate_hz) {
        case 0: case 44100: case 48000: case 88200: case 96000: case 176400: case 192000:
            break;
        default:
            return CHIPESTRA_E_INVALID_ARGUMENT;
    }
    if (!IsLiveInstance(handle)) {
        return CHIPESTRA_E_INVALID_STATE;
    }
    if (handle->state != ChipestraInstanceOpaque::State::MachineLoaded || handle->output) {
        // Same discipline as chipestra_select_null_output: no implicit replacement
        // of an already-selected output.
        return CHIPESTRA_E_INVALID_STATE;
    }

    try {
        auto output = std::make_unique<WasapiSharedOutput>();
        output->SetRequestedRateHz(requested_rate_hz);
        output->Initialize();
        handle->output = std::move(output);
        handle->has_last_rejected_format = false;
        return CHIPESTRA_OK;
    } catch (const UnsupportedEndpointFormatError& e) {
        handle->last_rejected_format = e.Format();
        handle->has_last_rejected_format = true;
        return CHIPESTRA_E_UNSUPPORTED_FORMAT;
    } catch (...) {
        return CHIPESTRA_E_INTERNAL;
    }
}

extern "C" ChipestraResult chipestra_get_audio_output_format(ChipestraHandle handle, ChipestraAudioOutputFormat* out_format) noexcept {
    if (handle == nullptr || out_format == nullptr) {
        return CHIPESTRA_E_INVALID_ARGUMENT;
    }
    if (!IsLiveInstance(handle)) {
        return CHIPESTRA_E_INVALID_STATE;
    }
    try {
        if (handle->output && handle->output->GetFormat(out_format)) {
            return CHIPESTRA_OK;
        }
        if (!handle->output && handle->has_last_rejected_format) {
            *out_format = handle->last_rejected_format;
            return CHIPESTRA_OK;
        }
        return CHIPESTRA_E_INVALID_STATE;
    } catch (...) {
        return CHIPESTRA_E_INTERNAL;
    }
}

extern "C" ChipestraResult chipestra_get_audio_output_diagnostics(ChipestraHandle handle,
                                                       ChipestraAudioOutputDiagnostics* out_diagnostics) noexcept {
    if (handle == nullptr || out_diagnostics == nullptr) {
        return CHIPESTRA_E_INVALID_ARGUMENT;
    }
    if (!IsLiveInstance(handle)) {
        return CHIPESTRA_E_INVALID_STATE;
    }
    bool has_output_selected = handle->state == ChipestraInstanceOpaque::State::Ready ||
                                handle->state == ChipestraInstanceOpaque::State::Playing ||
                                handle->state == ChipestraInstanceOpaque::State::Paused;
    if (!has_output_selected || !handle->output) {
        return CHIPESTRA_E_INVALID_STATE;
    }

    try {
        *out_diagnostics = handle->output->GetDiagnostics();
        return CHIPESTRA_OK;
    } catch (...) {
        return CHIPESTRA_E_INTERNAL;
    }
}

extern "C" ChipestraResult chipestra_start_playback(ChipestraHandle handle) noexcept {
    if (handle == nullptr) {
        return CHIPESTRA_E_INVALID_ARGUMENT;
    }
    if (!IsLiveInstance(handle)) {
        return CHIPESTRA_E_INVALID_STATE;
    }
    if (handle->state != ChipestraInstanceOpaque::State::Ready) {
        return CHIPESTRA_E_INVALID_STATE;
    }

    try {
        // Captures `handle` by value (it is itself just a pointer): safe
        // because chipestra_destroy_instance always stops and joins the pump
        // before the instance it points to is torn down (see there).
        handle->resampler.Reset();
        handle->pump = std::make_unique<PlaybackPump>([handle]() {
            // One EmuTick == one canonical (192kHz) output frame, so a block is exactly
            // kBlockSizeFrames ticks. The Machine writes each channel at its own native rate;
            // the resampler brings every channel up to the canonical rate before the Mixer.
            handle->sink.ClearAll();
            handle->machine->Render(EmuDuration(Mixer::kBlockSizeFrames), &handle->sink);

            const LogicalChannelSink& canonical = handle->resampler.Resample(
                handle->sink, handle->machine->LogicalChannels(), Mixer::kBlockSizeFrames);

            std::vector<StereoFrame> out;
            {
                // channels_mutex guards this block's Mute/Pan/Gain reads and Peak/RMS writes
                // against a concurrent Channel Meter ABI call from the UI thread.
                std::lock_guard<std::mutex> channelsLock(handle->channels_mutex);
                handle->mixer.MixBlock(canonical, handle->machine->LogicalChannels(), Mixer::kBlockSizeFrames, out);
            }

            // Master VOLUME/MUTE is a second stage applied to the already-mixed stereo signal
            // (after every channel strip, like a console's master fader) - per-channel
            // Gain/Pan/Mute are already folded in by MixBlock above. The master L/R level meter
            // is measured on this same post-gain signal, i.e. what reaches the audio output next.
            const float masterGain =
                handle->master_muted.load(std::memory_order_relaxed) ? 0.0f : handle->master_gain.load(std::memory_order_relaxed);
            float peakLeft = 0.0f;
            float peakRight = 0.0f;
            double sumSqLeft = 0.0;
            double sumSqRight = 0.0;
            for (StereoFrame& frame : out) {
                frame.left *= masterGain;
                frame.right *= masterGain;
                peakLeft = std::max(peakLeft, std::fabs(frame.left));
                peakRight = std::max(peakRight, std::fabs(frame.right));
                sumSqLeft += static_cast<double>(frame.left) * frame.left;
                sumSqRight += static_cast<double>(frame.right) * frame.right;
            }
            if (!out.empty()) {
                handle->master_peak_left.store(peakLeft, std::memory_order_relaxed);
                handle->master_peak_right.store(peakRight, std::memory_order_relaxed);
                handle->master_rms_left.store(static_cast<float>(std::sqrt(sumSqLeft / static_cast<double>(out.size()))),
                                               std::memory_order_relaxed);
                handle->master_rms_right.store(static_cast<float>(std::sqrt(sumSqRight / static_cast<double>(out.size()))),
                                                std::memory_order_relaxed);
            }

            handle->output->SubmitStereo(out.data(), out.size());
        });
        handle->pump->Start();
        handle->state = ChipestraInstanceOpaque::State::Playing;
        return CHIPESTRA_OK;
    } catch (...) {
        return CHIPESTRA_E_INTERNAL;
    }
}

extern "C" ChipestraResult chipestra_pause_playback(ChipestraHandle handle) noexcept {
    if (handle == nullptr) {
        return CHIPESTRA_E_INVALID_ARGUMENT;
    }
    if (!IsLiveInstance(handle)) {
        return CHIPESTRA_E_INVALID_STATE;
    }
    if (handle->state != ChipestraInstanceOpaque::State::Playing) {
        return CHIPESTRA_E_INVALID_STATE;
    }

    handle->pump->Pause();
    handle->state = ChipestraInstanceOpaque::State::Paused;
    return CHIPESTRA_OK;
}

extern "C" ChipestraResult chipestra_resume_playback(ChipestraHandle handle) noexcept {
    if (handle == nullptr) {
        return CHIPESTRA_E_INVALID_ARGUMENT;
    }
    if (!IsLiveInstance(handle)) {
        return CHIPESTRA_E_INVALID_STATE;
    }
    if (handle->state != ChipestraInstanceOpaque::State::Paused) {
        return CHIPESTRA_E_INVALID_STATE;
    }

    handle->pump->Resume();
    handle->state = ChipestraInstanceOpaque::State::Playing;
    return CHIPESTRA_OK;
}

extern "C" ChipestraResult chipestra_stop_playback(ChipestraHandle handle) noexcept {
    if (handle == nullptr) {
        return CHIPESTRA_E_INVALID_ARGUMENT;
    }
    if (!IsLiveInstance(handle)) {
        return CHIPESTRA_E_INVALID_STATE;
    }
    if (handle->state != ChipestraInstanceOpaque::State::Playing &&
        handle->state != ChipestraInstanceOpaque::State::Paused) {
        return CHIPESTRA_E_INVALID_STATE;
    }

    handle->pump->Stop();
    handle->pump.reset();
    handle->state = ChipestraInstanceOpaque::State::Ready;
    return CHIPESTRA_OK;
}

namespace {

// Copies `value` into `buffer` (of `bufferSize` bytes), truncating and
// always leaving `buffer` null-terminated. bufferSize must be >= 1.
void CopyTruncated(const std::string& value, char* buffer, std::size_t bufferSize) {
    std::size_t toCopy = std::min(value.size(), bufferSize - 1);
    std::memcpy(buffer, value.data(), toCopy);
    buffer[toCopy] = '\0';
}

}  // namespace

extern "C" uint32_t chipestra_get_machine_count(void) noexcept {
    return static_cast<uint32_t>(DefaultMachineRegistry().Enumerate().size());
}

extern "C" ChipestraResult chipestra_get_machine_info(uint32_t index, ChipestraMachineInfo* out_info) noexcept {
    if (out_info == nullptr) {
        return CHIPESTRA_E_INVALID_ARGUMENT;
    }

    const std::vector<MachineDefinition>& definitions = DefaultMachineRegistry().Enumerate();
    if (index >= definitions.size()) {
        return CHIPESTRA_E_NOT_FOUND;
    }

    const MachineDefinition& definition = definitions[index];
    CopyTruncated(definition.Id.Value(), out_info->machine_id, sizeof(out_info->machine_id));
    CopyTruncated(definition.DisplayName, out_info->display_name, sizeof(out_info->display_name));
    CopyTruncated(definition.Category == MachineCategory::Arcade ? "ARCADE" : "CONSOLE", out_info->category,
                  sizeof(out_info->category));
    out_info->variant_count = static_cast<uint32_t>(definition.Variants.size());
    CopyTruncated(definition.Manufacturer, out_info->manufacturer, sizeof(out_info->manufacturer));
    return CHIPESTRA_OK;
}

// ---------------------------------------------------------------------
// Channel Meter group - see chipestra_abi.h's comment above this group's
// declarations for the concurrency design (channels_mutex).
// ---------------------------------------------------------------------

extern "C" ChipestraResult chipestra_get_logical_channel_info(ChipestraHandle handle, uint32_t index,
                                                   ChipestraLogicalChannelInfo* out_info) noexcept {
    if (handle == nullptr || out_info == nullptr) {
        return CHIPESTRA_E_INVALID_ARGUMENT;
    }
    if (!IsLiveInstance(handle) || handle->state == ChipestraInstanceOpaque::State::Created) {
        return CHIPESTRA_E_INVALID_STATE;
    }

    try {
        std::lock_guard<std::mutex> lock(handle->channels_mutex);
        std::vector<LogicalChannel>& channels = handle->machine->LogicalChannels();
        if (index >= channels.size()) {
            return CHIPESTRA_E_NOT_FOUND;
        }

        const LogicalChannel& channel = channels[index];
        out_info->channel_id = channel.Id;
        CopyTruncated(channel.DeviceName, out_info->device_name, sizeof(out_info->device_name));
        CopyTruncated(channel.ChannelName, out_info->channel_name, sizeof(out_info->channel_name));
        out_info->native_sample_rate = channel.NativeSampleRate;
        out_info->muted = channel.Mute ? 1 : 0;
        out_info->pan = channel.Pan;
        out_info->gain = channel.Gain;
        out_info->peak = channel.Peak;
        out_info->rms = channel.RMS;
        out_info->peak_left = channel.PeakLeft;
        out_info->peak_right = channel.PeakRight;
        out_info->rms_left = channel.RMSLeft;
        out_info->rms_right = channel.RMSRight;
        out_info->source_peak = channel.SourcePeak;
        return CHIPESTRA_OK;
    } catch (...) {
        return CHIPESTRA_E_INTERNAL;
    }
}

extern "C" ChipestraResult chipestra_set_logical_channel_muted(ChipestraHandle handle, uint32_t index, uint8_t muted) noexcept {
    if (handle == nullptr) {
        return CHIPESTRA_E_INVALID_ARGUMENT;
    }
    if (!IsLiveInstance(handle) || handle->state == ChipestraInstanceOpaque::State::Created) {
        return CHIPESTRA_E_INVALID_STATE;
    }

    try {
        std::lock_guard<std::mutex> lock(handle->channels_mutex);
        std::vector<LogicalChannel>& channels = handle->machine->LogicalChannels();
        if (index >= channels.size()) {
            return CHIPESTRA_E_NOT_FOUND;
        }
        channels[index].Mute = muted != 0;
        return CHIPESTRA_OK;
    } catch (...) {
        return CHIPESTRA_E_INTERNAL;
    }
}

extern "C" ChipestraResult chipestra_set_logical_channel_pan(ChipestraHandle handle, uint32_t index, float pan) noexcept {
    if (handle == nullptr) {
        return CHIPESTRA_E_INVALID_ARGUMENT;
    }
    if (!IsLiveInstance(handle) || handle->state == ChipestraInstanceOpaque::State::Created) {
        return CHIPESTRA_E_INVALID_STATE;
    }

    try {
        std::lock_guard<std::mutex> lock(handle->channels_mutex);
        std::vector<LogicalChannel>& channels = handle->machine->LogicalChannels();
        if (index >= channels.size()) {
            return CHIPESTRA_E_NOT_FOUND;
        }
        channels[index].Pan = pan;
        // A UI-driven Pan change always counts as a manual override - see
        // LogicalChannel::PanOverridden's comment (some boards auto-sync Pan every block).
        channels[index].PanOverridden = true;
        return CHIPESTRA_OK;
    } catch (...) {
        return CHIPESTRA_E_INTERNAL;
    }
}

extern "C" ChipestraResult chipestra_clear_logical_channel_pan_override(ChipestraHandle handle, uint32_t index) noexcept {
    if (handle == nullptr) {
        return CHIPESTRA_E_INVALID_ARGUMENT;
    }
    if (!IsLiveInstance(handle) || handle->state == ChipestraInstanceOpaque::State::Created) {
        return CHIPESTRA_E_INVALID_STATE;
    }

    try {
        std::lock_guard<std::mutex> lock(handle->channels_mutex);
        std::vector<LogicalChannel>& channels = handle->machine->LogicalChannels();
        if (index >= channels.size()) {
            return CHIPESTRA_E_NOT_FOUND;
        }
        // ALL RESET: returns this channel to song-sync mode. Pan itself resets to center
        // immediately as an interim value - a board with a live auto-sync mechanism overwrites
        // it with the driver's real value on the next rendered block, and a board without one
        // has no other way to reach a defined "un-overridden" Pan.
        channels[index].Pan = 0.0f;
        channels[index].PanOverridden = false;
        // IMachine::OnPanOverrideCleared's header comment: the interim Pan=0 above is only
        // correct for a board with no better answer - event-driven or fixed-at-Initialize
        // boards need an immediate resync here, not just on the next block.
        handle->machine->OnPanOverrideCleared(index);
        return CHIPESTRA_OK;
    } catch (...) {
        return CHIPESTRA_E_INTERNAL;
    }
}

extern "C" ChipestraResult chipestra_set_logical_channel_gain(ChipestraHandle handle, uint32_t index, float gain) noexcept {
    if (handle == nullptr) {
        return CHIPESTRA_E_INVALID_ARGUMENT;
    }
    if (!IsLiveInstance(handle) || handle->state == ChipestraInstanceOpaque::State::Created) {
        return CHIPESTRA_E_INVALID_STATE;
    }

    try {
        std::lock_guard<std::mutex> lock(handle->channels_mutex);
        std::vector<LogicalChannel>& channels = handle->machine->LogicalChannels();
        if (index >= channels.size()) {
            return CHIPESTRA_E_NOT_FOUND;
        }
        channels[index].Gain = gain;
        return CHIPESTRA_OK;
    } catch (...) {
        return CHIPESTRA_E_INTERNAL;
    }
}

extern "C" ChipestraResult chipestra_get_master_state(ChipestraHandle handle, ChipestraMasterState* out_state) noexcept {
    if (handle == nullptr || out_state == nullptr) {
        return CHIPESTRA_E_INVALID_ARGUMENT;
    }
    if (!IsLiveInstance(handle) || handle->state == ChipestraInstanceOpaque::State::Created) {
        return CHIPESTRA_E_INVALID_STATE;
    }

    out_state->gain = handle->master_gain.load(std::memory_order_relaxed);
    out_state->muted = handle->master_muted.load(std::memory_order_relaxed) ? 1 : 0;
    out_state->peak_left = handle->master_peak_left.load(std::memory_order_relaxed);
    out_state->peak_right = handle->master_peak_right.load(std::memory_order_relaxed);
    out_state->rms_left = handle->master_rms_left.load(std::memory_order_relaxed);
    out_state->rms_right = handle->master_rms_right.load(std::memory_order_relaxed);
    return CHIPESTRA_OK;
}

extern "C" ChipestraResult chipestra_set_master_gain(ChipestraHandle handle, float gain) noexcept {
    if (handle == nullptr) {
        return CHIPESTRA_E_INVALID_ARGUMENT;
    }
    if (!IsLiveInstance(handle) || handle->state == ChipestraInstanceOpaque::State::Created) {
        return CHIPESTRA_E_INVALID_STATE;
    }

    handle->master_gain.store(gain, std::memory_order_relaxed);
    return CHIPESTRA_OK;
}

extern "C" ChipestraResult chipestra_set_master_muted(ChipestraHandle handle, uint8_t muted) noexcept {
    if (handle == nullptr) {
        return CHIPESTRA_E_INVALID_ARGUMENT;
    }
    if (!IsLiveInstance(handle) || handle->state == ChipestraInstanceOpaque::State::Created) {
        return CHIPESTRA_E_INVALID_STATE;
    }

    handle->master_muted.store(muted != 0, std::memory_order_relaxed);
    return CHIPESTRA_OK;
}

// ---------------------------------------------------------------------
// GameDefinition / ResourceResolver group - see chipestra_abi.h's comment above this
// group's declarations for the overall design.
// ---------------------------------------------------------------------

// ChipestraGameDefinitionOpaque/ChipestraResourceResolutionOpaque are intentionally
// defined only here, same discipline as ChipestraInstanceOpaque above: the header
// forward-declares them so callers can never depend on their layout.
struct ChipestraGameDefinitionOpaque {
    GameDefinition Definition;
};

struct ChipestraResourceResolutionOpaque {
    ResourceResolutionResult Result;
};

extern "C" ChipestraResult chipestra_parse_game_definition(const char* xml_utf8, uint32_t xml_length,
                                                ChipestraGameDefinitionHandle* out_handle, char* out_detail,
                                                uint32_t out_detail_size) noexcept {
    if (out_detail != nullptr && out_detail_size > 0) {
        out_detail[0] = '\0';
    }
    if (xml_utf8 == nullptr || out_handle == nullptr) {
        return CHIPESTRA_E_INVALID_ARGUMENT;
    }

    try {
        XmlParseResult parsed = ParseGameDefinition(std::string_view(xml_utf8, xml_length));
        if (out_detail != nullptr && out_detail_size > 0) {
            CopyTruncated(parsed.Detail, out_detail, out_detail_size);
        }
        if (parsed.Status != XmlParseStatus::Ok) {
            // Every XmlParseStatus failure maps to one ChipestraResult (see chipestra_abi.h) -
            // out_detail is where the specific reason (TooLarge/DoctypeRejected/
            // MalformedXml/NoGameElement/MissingAttribute) shows up.
            return CHIPESTRA_E_INVALID_ARGUMENT;
        }

        auto handle = std::make_unique<ChipestraGameDefinitionOpaque>();
        handle->Definition = std::move(parsed.Definition);
        *out_handle = handle.release();
        return CHIPESTRA_OK;
    } catch (...) {
        return CHIPESTRA_E_INTERNAL;
    }
}

extern "C" void chipestra_free_game_definition(ChipestraGameDefinitionHandle handle) noexcept {
    delete handle;
}

extern "C" ChipestraResult chipestra_game_definition_get_info(ChipestraGameDefinitionHandle handle, ChipestraGameInfo* out_info) noexcept {
    if (handle == nullptr || out_info == nullptr) {
        return CHIPESTRA_E_INVALID_ARGUMENT;
    }

    const GameDefinition& definition = handle->Definition;
    CopyTruncated(definition.Id, out_info->game_id, sizeof(out_info->game_id));
    CopyTruncated(definition.Title, out_info->title, sizeof(out_info->title));
    CopyTruncated(definition.Machine, out_info->machine_id, sizeof(out_info->machine_id));
    CopyTruncated(definition.Variant, out_info->variant_id, sizeof(out_info->variant_id));
    CopyTruncated(definition.Manufacturer, out_info->manufacturer, sizeof(out_info->manufacturer));
    out_info->resource_count = static_cast<uint32_t>(definition.Resources.size());
    out_info->track_count = static_cast<uint32_t>(definition.Tracks.size());
    CopyTruncated(definition.Archive, out_info->archive, sizeof(out_info->archive));
    return CHIPESTRA_OK;
}

extern "C" ChipestraResult chipestra_game_definition_get_resource(ChipestraGameDefinitionHandle handle, uint32_t index,
                                                       ChipestraRequiredResourceInfo* out_info) noexcept {
    if (handle == nullptr || out_info == nullptr) {
        return CHIPESTRA_E_INVALID_ARGUMENT;
    }

    const std::vector<RomResourceDefinition>& resources = handle->Definition.Resources;
    if (index >= resources.size()) {
        return CHIPESTRA_E_NOT_FOUND;
    }

    const RomResourceDefinition& rom = resources[index];
    CopyTruncated(rom.Name, out_info->logical_name, sizeof(out_info->logical_name));
    CopyTruncated(rom.File, out_info->accepted_name, sizeof(out_info->accepted_name));
    out_info->has_size_bytes = rom.SizeBytes.has_value() ? 1 : 0;
    out_info->size_bytes = rom.SizeBytes.value_or(0);
    out_info->has_crc32 = rom.Crc32.has_value() ? 1 : 0;
    out_info->crc32 = rom.Crc32.value_or(0);
    CopyTruncated(rom.Sha1.value_or(std::string()), out_info->sha1, sizeof(out_info->sha1));
    out_info->required = 1;  // every declared resource is Required=true
    // archive is a per-<game> attribute, not per-<rom> (see this struct's own
    // comment in chipestra_abi.h) - always "" here; read it once from
    // chipestra_game_definition_get_info's ChipestraGameInfo::archive instead.
    out_info->archive[0] = '\0';
    return CHIPESTRA_OK;
}

extern "C" ChipestraResult chipestra_game_definition_get_track(ChipestraGameDefinitionHandle handle, uint32_t index,
                                                    ChipestraTrackInfo* out_info) noexcept {
    if (handle == nullptr || out_info == nullptr) {
        return CHIPESTRA_E_INVALID_ARGUMENT;
    }

    const std::vector<TrackDefinition>& tracks = handle->Definition.Tracks;
    if (index >= tracks.size()) {
        return CHIPESTRA_E_NOT_FOUND;
    }

    const TrackDefinition& track = tracks[index];
    CopyTruncated(track.Id, out_info->id, sizeof(out_info->id));
    CopyTruncated(track.Command, out_info->command, sizeof(out_info->command));
    CopyTruncated(track.Title, out_info->title, sizeof(out_info->title));
    out_info->is_control_command = track.IsControlCommand ? 1 : 0;
    out_info->loops = track.Loops ? 1 : 0;
    out_info->is_wsg = track.IsWsg ? 1 : 0;
    return CHIPESTRA_OK;
}

extern "C" ChipestraResult chipestra_resolve_resources(const char* rom_root, const ChipestraRequiredResourceInfo* resources,
                                            uint32_t resource_count,
                                            ChipestraResourceResolutionHandle* out_handle) noexcept {
    if (rom_root == nullptr || out_handle == nullptr) {
        return CHIPESTRA_E_INVALID_ARGUMENT;
    }
    if (resource_count > 0 && resources == nullptr) {
        return CHIPESTRA_E_INVALID_ARGUMENT;
    }

    try {
        // Inverse of chipestra_game_definition_get_resource below: rebuild the
        // RequiredResource list ResourceResolver::Resolve() expects from the
        // caller's ChipestraRequiredResourceInfo array (deliberately not tied
        // to a still-alive ChipestraGameDefinitionHandle).
        std::vector<RequiredResource> required;
        required.reserve(resource_count);
        for (uint32_t i = 0; i < resource_count; ++i) {
            const ChipestraRequiredResourceInfo& info = resources[i];
            RequiredResource req;
            req.LogicalName = info.logical_name;
            req.AcceptedNames = {info.accepted_name};
            if (info.has_size_bytes) {
                req.SizeBytes = info.size_bytes;
            }
            if (info.has_crc32) {
                req.Crc32 = info.crc32;
            }
            if (info.sha1[0] != '\0') {
                req.Sha1 = info.sha1;
            }
            req.Required = info.required != 0;
            if (info.archive[0] != '\0') {
                req.Archive = info.archive;
            }
            required.push_back(std::move(req));
        }

        ResourceResolver resolver{std::filesystem::path(rom_root)};
        auto handle = std::make_unique<ChipestraResourceResolutionOpaque>();
        handle->Result = resolver.Resolve(required);
        *out_handle = handle.release();
        return CHIPESTRA_OK;
    } catch (...) {
        return CHIPESTRA_E_INTERNAL;
    }
}

extern "C" void chipestra_free_resource_resolution(ChipestraResourceResolutionHandle handle) noexcept {
    delete handle;
}

extern "C" ChipestraResult chipestra_resource_resolution_get_summary(ChipestraResourceResolutionHandle handle,
                                                          ChipestraResolutionSummary* out_summary) noexcept {
    if (handle == nullptr || out_summary == nullptr) {
        return CHIPESTRA_E_INVALID_ARGUMENT;
    }

    out_summary->ok = handle->Result.Ok ? 1 : 0;
    out_summary->resolved_count = static_cast<uint32_t>(handle->Result.Resources.Count());
    out_summary->failure_count = static_cast<uint32_t>(handle->Result.Failures.size());
    return CHIPESTRA_OK;
}

extern "C" ChipestraResult chipestra_resource_resolution_get_failure(ChipestraResourceResolutionHandle handle, uint32_t index,
                                                          ChipestraResolutionFailure* out_failure) noexcept {
    if (handle == nullptr || out_failure == nullptr) {
        return CHIPESTRA_E_INVALID_ARGUMENT;
    }

    const std::vector<ResourceResolutionFailure>& failures = handle->Result.Failures;
    if (index >= failures.size()) {
        return CHIPESTRA_E_NOT_FOUND;
    }

    const ResourceResolutionFailure& failure = failures[index];
    CopyTruncated(failure.LogicalName, out_failure->logical_name, sizeof(out_failure->logical_name));
    out_failure->result = failure.Result;
    CopyTruncated(failure.Detail, out_failure->detail, sizeof(out_failure->detail));
    return CHIPESTRA_OK;
}

extern "C" ChipestraResult chipestra_resource_resolution_get_resolved(ChipestraResourceResolutionHandle handle, uint32_t index,
                                                           ChipestraResolvedResource* out_resolved) noexcept {
    if (handle == nullptr || out_resolved == nullptr) {
        return CHIPESTRA_E_INVALID_ARGUMENT;
    }

    const MachineResources& resources = handle->Result.Resources;
    if (index >= resources.Count()) {
        return CHIPESTRA_E_NOT_FOUND;
    }

    const ResolvedResource& resource = resources.At(index);
    CopyTruncated(resource.LogicalName(), out_resolved->logical_name, sizeof(out_resolved->logical_name));
    out_resolved->data = resource.Data();  // non-owning: valid only until `handle` is freed (ChipestraResolvedResource's own doc comment)
    out_resolved->size = static_cast<uint64_t>(resource.Size());
    return CHIPESTRA_OK;
}
