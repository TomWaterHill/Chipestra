// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// Chipestra Native Core - public C ABI.
//
// Rules for everything declared in this header:
//   - extern "C" linkage only; no C++ name mangling.
//   - No C++ exceptions, no STL types, no C++ classes cross this boundary.
//   - Every function is noexcept and must not let an exception escape.
//
// The header declares the instance lifecycle (Created/MachineLoaded/Ready/
// Playing/Paused/Dead), audio output, playback, channel meter, machine registry
// and game definition / resource resolver groups. The C++-only section at the
// end (IMachine and related types) is internal to Native and is not part of the
// C ABI: no chipestra_* function takes or returns those types.

#include <stdint.h>

#if defined(_WIN32)
#if defined(CHIPESTRA_BUILD_DLL)
#define CHIPESTRA_API __declspec(dllexport)
#else
#define CHIPESTRA_API __declspec(dllimport)
#endif
#else
#define CHIPESTRA_API
#endif

#ifdef __cplusplus
#define CHIPESTRA_NOEXCEPT noexcept
#else
#define CHIPESTRA_NOEXCEPT
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Returns the C ABI version implemented by this Native build.
//
// This is the contract version of the functions declared in this header,
// not a product/marketing version. Managed Core should call this once at
// startup and refuse to proceed if the returned value is not a version it
// understands.
//
// Current value: 4. Changing a function signature or contract incompatibly bumps
// this value; appending enum values or struct fields does not require it.
CHIPESTRA_API uint32_t chipestra_get_api_version(void) CHIPESTRA_NOEXCEPT;

// Opaque handle to one Native Runtime instance. Its pointee type is never defined
// in this header. Managed Core must treat this as an opaque token and never
// dereference it.
typedef struct ChipestraInstanceOpaque* ChipestraHandle;

// Result codes shared by every C ABI function that can fail.
//
// New codes are only ever appended after the last one below - never inserted or
// renumbered - so existing numeric values stay stable across Native builds.
typedef enum ChipestraResult {
    CHIPESTRA_OK = 0,
    CHIPESTRA_E_INVALID_ARGUMENT = 1,
    CHIPESTRA_E_INVALID_STATE = 2,
    CHIPESTRA_E_INTERNAL = 3,
    CHIPESTRA_E_NOT_FOUND = 4,
    CHIPESTRA_E_AMBIGUOUS = 5,      // multiple candidates matched a RequiredResource; never auto-selected
    CHIPESTRA_E_HASH_MISMATCH = 6,  // a candidate's actual CRC32/SHA1 did not match the expected value
    CHIPESTRA_E_UNSUPPORTED = 7,    // candidate exists but can't be read (unsupported ZIP compression, exceeds size cap)
    CHIPESTRA_E_UNSUPPORTED_FORMAT = 8,  // the audio endpoint's mix format is not one WasapiSharedOutput can write
                                   // (see chipestra_get_audio_output_format for the offending format)
    // The loaded Machine does not accept this kind of command (e.g. a WSG
    // sound-effect request sent to a board that has none).
    CHIPESTRA_E_UNSUPPORTED_COMMAND = 9,
} ChipestraResult;

// Creates a new Native Runtime instance in the Created state and writes its
// handle to *out_handle.
//
// Returns CHIPESTRA_E_INVALID_ARGUMENT (and leaves *out_handle untouched) if
// out_handle is null. Returns CHIPESTRA_E_INTERNAL if instance allocation fails;
// no C++ exception ever escapes this function.
CHIPESTRA_API ChipestraResult chipestra_create_instance(ChipestraHandle* out_handle) CHIPESTRA_NOEXCEPT;

// Destroys a Native Runtime instance created by chipestra_create_instance,
// transitioning it (from either Created or MachineLoaded) to Dead and
// releasing its resources. If a Machine is currently loaded, it is shut down
// first, preserving Machine-before-Instance teardown order.
//
// Safe to call with a null handle (no-op). A handle that was already destroyed is
// detected and ignored (a no-op, not a double free) only as long as its address has not
// been given to a newer instance: the handle is the instance's raw pointer, so once the
// memory is reused a stale handle is indistinguishable from the new instance's handle and
// would act on (or destroy) that instance. Callers must therefore treat a destroyed
// handle as dead and never use or destroy it again; the detection is a safety net for
// mistakes, not a guarantee.
CHIPESTRA_API void chipestra_destroy_instance(ChipestraHandle handle) CHIPESTRA_NOEXCEPT;

// One resolved resource's bytes, as a non-owning view: `data` points at memory owned
// by the ChipestraResourceResolutionHandle that produced it (see
// chipestra_resource_resolution_get_resolved) and stays valid only until that
// handle is freed. chipestra_load_machine copies the bytes it needs out of `data`
// before returning, so the resolution handle may be freed immediately afterwards.
typedef struct ChipestraResolvedResource {
    char logical_name[64];    // matches RequiredResource::LogicalName / <rom name="...">
    const uint8_t* data;      // non-owning; see lifetime note above
    uint64_t size;
} ChipestraResolvedResource;

// Loads a Machine into `handle`, transitioning it Created -> MachineLoaded.
// `machine_id`/`variant_id` are looked up in the MachineRegistry: both must be
// null-terminated strings that exactly match a registered MachineDefinition's Id
// and one of its Variants' Id - no fuzzy/partial matching.
//
// `resources`/`resource_count` are the resolved ROM/data resources (see
// chipestra_resolve_resources below) handed to the matched MachineDefinition's
// factory, which maps logical names onto the ROM bytes. Pass resource_count == 0
// (resources may then be null) for a Machine that needs none.
//
// Returns CHIPESTRA_E_INVALID_ARGUMENT if handle, machine_id, or variant_id is
// null, or if resource_count > 0 and resources is null.
// Returns CHIPESTRA_E_INVALID_STATE if handle is not a live instance currently in
// the Created state (e.g. it is Dead, or already has a Machine loaded).
// Returns CHIPESTRA_E_NOT_FOUND if machine_id is not registered, or if it is
// registered but variant_id does not exactly match any of its Variants -
// both cases are indistinguishable to the caller through this return value.
// Returns CHIPESTRA_E_INTERNAL if constructing/initializing the Machine throws; no
// exception ever escapes this function.
CHIPESTRA_API ChipestraResult chipestra_load_machine(ChipestraHandle handle, const char* machine_id, const char* variant_id,
                                    const ChipestraResolvedResource* resources, uint32_t resource_count) CHIPESTRA_NOEXCEPT;

// Unloads the Machine currently loaded into `handle`, shutting it down and
// transitioning MachineLoaded -> Created.
//
// Returns CHIPESTRA_E_INVALID_ARGUMENT if handle is null.
// Returns CHIPESTRA_E_INVALID_STATE if handle is not a live instance currently in
// the MachineLoaded state.
CHIPESTRA_API ChipestraResult chipestra_unload_machine(ChipestraHandle handle) CHIPESTRA_NOEXCEPT;

// Resets the Machine currently loaded into `handle` to its post-Initialize
// state (IMachine::Reset), deterministically.
//
// Returns CHIPESTRA_E_INVALID_ARGUMENT if handle is null.
// Returns CHIPESTRA_E_INVALID_STATE if handle is not a live instance currently in
// the MachineLoaded state (Created has no Machine to reset).
// Returns CHIPESTRA_E_INTERNAL if the underlying IMachine::Reset() throws.
CHIPESTRA_API ChipestraResult chipestra_reset_machine(ChipestraHandle handle) CHIPESTRA_NOEXCEPT;

// Number of LogicalChannels the Machine currently loaded into `handle` exposes.
// A summary count only; per-channel detail is in the Channel Meter group below.
//
// Safe to call in any state, including Playing: the size of a Machine's channel
// list is fixed when the Machine is initialized, and the playback thread only
// updates existing elements in place.
//
// Returns CHIPESTRA_E_INVALID_ARGUMENT if handle or out_count is null.
// Returns CHIPESTRA_E_INVALID_STATE if handle is not a live instance, or has no
// Machine currently loaded (Created state).
CHIPESTRA_API ChipestraResult chipestra_get_machine_channel_count(ChipestraHandle handle, uint32_t* out_count) CHIPESTRA_NOEXCEPT;

// Sends one command to the Machine currently loaded into `handle`. `kind` is:
//   0 = TrackSelect (value carries an abstract track number)
//   1 = Stop
//   2 = Raw (value/auxiliary are the target board's own raw command bytes,
//       passed through as-is)
//   3 = Wsg (value is a WSG sound-effect number; only Machines that accept this
//       kind handle it)
// `auxiliary` is only meaningful for some Kind/board combinations; pass 0
// when unused.
//
// Only valid outside of Playing: once playback has started, the PlaybackPump
// thread is the only thread allowed to touch the Machine's mutable state. To
// switch tracks, stop playback, send the command, then start playback again.
//
// Returns CHIPESTRA_E_INVALID_ARGUMENT if handle is null or kind is not one of the
// values above.
// Returns CHIPESTRA_E_INVALID_STATE if handle is not a live instance, or its state
// is not one of MachineLoaded/Ready/Paused (in particular: Created has no
// Machine yet, and Playing means the pump thread owns the Machine).
// Returns CHIPESTRA_E_UNSUPPORTED_COMMAND if the loaded Machine does not accept this
// kind of command.
// Returns CHIPESTRA_E_INTERNAL if the underlying IMachine::SendCommand() throws.
CHIPESTRA_API ChipestraResult chipestra_send_command(ChipestraHandle handle, uint8_t kind, uint8_t value,
                                    uint8_t auxiliary) CHIPESTRA_NOEXCEPT;

// Selects NullAudioOutput as `handle`'s audio output and initializes it. Does not
// by itself change Instance state - it only prepares the output that
// chipestra_start_audio_output below then starts; the MachineLoaded -> Ready
// transition happens once both have succeeded.
// Calling this again (to pick a different output) requires
// chipestra_stop_audio_output first.
//
// Returns CHIPESTRA_E_INVALID_ARGUMENT if handle is null.
// Returns CHIPESTRA_E_INVALID_STATE if handle is not a live instance currently in
// the MachineLoaded state, or if an output has already been selected (call
// chipestra_stop_audio_output first to select a different one).
// Returns CHIPESTRA_E_INTERNAL if constructing/initializing the output throws.
CHIPESTRA_API ChipestraResult chipestra_select_null_output(ChipestraHandle handle) CHIPESTRA_NOEXCEPT;

// Starts the audio output selected by chipestra_select_null_output, transitioning
// MachineLoaded -> Ready.
//
// Returns CHIPESTRA_E_INVALID_ARGUMENT if handle is null.
// Returns CHIPESTRA_E_INVALID_STATE if handle is not a live instance currently in
// the MachineLoaded state with an output already selected.
// Returns CHIPESTRA_E_INTERNAL if the underlying IAudioOutput::Start() throws.
CHIPESTRA_API ChipestraResult chipestra_start_audio_output(ChipestraHandle handle) CHIPESTRA_NOEXCEPT;

// Stops and releases `handle`'s audio output, transitioning Ready back to
// MachineLoaded. Requires playback to already be stopped
// (chipestra_stop_playback) if it was ever started - this only works from Ready,
// not Playing/Paused, so teardown always goes Playing/Paused -> Ready ->
// MachineLoaded in that order. A fresh chipestra_select_null_output +
// chipestra_start_audio_output is required to reach Ready again afterwards.
//
// Returns CHIPESTRA_E_INVALID_ARGUMENT if handle is null.
// Returns CHIPESTRA_E_INVALID_STATE if handle is not a live instance currently in
// the Ready state.
CHIPESTRA_API ChipestraResult chipestra_stop_audio_output(ChipestraHandle handle) CHIPESTRA_NOEXCEPT;

// Starts playback, transitioning Ready -> Playing: creates and starts the
// PlaybackPump that thereafter owns rendering (Machine::Render + Mixer::MixBlock +
// IAudioOutput::SubmitStereo) on its own dedicated thread. Nothing else may call
// into the Machine or the audio output while playback is running; UI code
// controls playback only through this function and pause/resume/stop below.
//
// Returns CHIPESTRA_E_INVALID_ARGUMENT if handle is null.
// Returns CHIPESTRA_E_INVALID_STATE if handle is not a live instance currently in
// the Ready state.
CHIPESTRA_API ChipestraResult chipestra_start_playback(ChipestraHandle handle) CHIPESTRA_NOEXCEPT;

// Pauses playback, transitioning Playing -> Paused: the
// PlaybackPump's worker thread stays alive but stops rendering until
// chipestra_resume_playback.
//
// Returns CHIPESTRA_E_INVALID_ARGUMENT if handle is null.
// Returns CHIPESTRA_E_INVALID_STATE if handle is not a live instance currently in
// the Playing state.
CHIPESTRA_API ChipestraResult chipestra_pause_playback(ChipestraHandle handle) CHIPESTRA_NOEXCEPT;

// Resumes playback, transitioning Paused -> Playing.
//
// Returns CHIPESTRA_E_INVALID_ARGUMENT if handle is null.
// Returns CHIPESTRA_E_INVALID_STATE if handle is not a live instance currently in
// the Paused state.
CHIPESTRA_API ChipestraResult chipestra_resume_playback(ChipestraHandle handle) CHIPESTRA_NOEXCEPT;

// Stops playback, transitioning Playing or Paused back to Ready:
// stops and joins the PlaybackPump's worker thread. The audio output stays
// selected and started (Ready), so chipestra_start_playback can resume rendering
// without reselecting/restarting the output; use chipestra_stop_audio_output
// afterwards to tear the output down too.
//
// Returns CHIPESTRA_E_INVALID_ARGUMENT if handle is null.
// Returns CHIPESTRA_E_INVALID_STATE if handle is not a live instance currently in
// the Playing or Paused state.
CHIPESTRA_API ChipestraResult chipestra_stop_playback(ChipestraHandle handle) CHIPESTRA_NOEXCEPT;

// Selects WasapiSharedOutput as `handle`'s audio output and
// initializes it: opens the default Windows render endpoint in Shared mode.
// Follows the same selection discipline as chipestra_select_null_output - see
// that function's docs; the two share the "already selected" guard.
//
// `requested_rate_hz`: 0 = "auto" (follow the endpoint), or
// one of 44100/48000/88200/96000/176400/192000. Shared mode always opens at
// the endpoint's own mix-format rate (AUTOCONVERTPCM is NOT used), so the
// request is advisory: it is recorded and reported back through
// chipestra_get_audio_output_format (selection_honored), but the output rate is
// whatever the Windows mixer runs at.
//
// Returns CHIPESTRA_E_INVALID_ARGUMENT if handle is null or requested_rate_hz is
// not one of the values above.
// Returns CHIPESTRA_E_INVALID_STATE if handle is not a live instance currently in
// the MachineLoaded state, or if an output has already been selected.
// Returns CHIPESTRA_E_UNSUPPORTED_FORMAT if the endpoint's mix format is not
// float32 / 16-bit PCM; the format is then retrievable via
// chipestra_get_audio_output_format.
// Returns CHIPESTRA_E_INTERNAL if opening the device/audio client otherwise fails
// (e.g. no default render endpoint) or initialization otherwise throws.
CHIPESTRA_API ChipestraResult chipestra_select_wasapi_shared_output(ChipestraHandle handle, uint32_t requested_rate_hz) CHIPESTRA_NOEXCEPT;

typedef enum ChipestraSampleFormat {
    CHIPESTRA_SAMPLE_FORMAT_OTHER = 0,
    CHIPESTRA_SAMPLE_FORMAT_PCM = 1,
    CHIPESTRA_SAMPLE_FORMAT_FLOAT = 2,
} ChipestraSampleFormat;

// An audio endpoint's mix format as reported by IAudioClient::GetMixFormat.
typedef struct ChipestraAudioOutputFormat {
    uint32_t requested_rate_hz;  // what chipestra_select_wasapi_shared_output was asked for (0 = auto)
    uint32_t endpoint_rate_hz;   // the mix format's sample rate
    uint32_t bits_per_sample;    // container bits
    uint32_t valid_bits;         // valid bits (== bits_per_sample unless WAVE_FORMAT_EXTENSIBLE says otherwise)
    uint32_t channels;
    uint32_t sample_format;      // a ChipestraSampleFormat value
    uint32_t is_extensible;      // 1 if the mix format is WAVE_FORMAT_EXTENSIBLE
    uint32_t selection_honored;  // 1 if requested_rate_hz is 0 or equals endpoint_rate_hz
} ChipestraAudioOutputFormat;

// Writes the mix format of `handle`'s selected WASAPI output to *out_format.
// If no output is selected but the most recent chipestra_select_wasapi_shared_output
// failed with CHIPESTRA_E_UNSUPPORTED_FORMAT, writes the format that was rejected
// (so a caller can say exactly what the device offered).
//
// Returns CHIPESTRA_E_INVALID_ARGUMENT if handle or out_format is null.
// Returns CHIPESTRA_E_INVALID_STATE if handle is not a live instance, or no format
// is available (no WASAPI output selected and no rejected format on record -
// e.g. a Null output).
CHIPESTRA_API ChipestraResult chipestra_get_audio_output_format(ChipestraHandle handle, ChipestraAudioOutputFormat* out_format) CHIPESTRA_NOEXCEPT;

// Diagnostic counters for `handle`'s currently selected audio output.
// Meaningful for WasapiSharedOutput; a
// NullAudioOutput reports the fields it has no equivalent for (every
// queue/worker counter) as zero, since it has no queue or worker thread of
// its own.
typedef struct ChipestraAudioOutputDiagnostics {
    uint64_t submitted_frame_count;    // matches IAudioOutput::SubmittedFrameCount()
    uint64_t worker_wake_count;        // times the render loop attempted to drain into the endpoint
    uint64_t queue_drain_count;        // frames successfully written to the endpoint buffer
    uint64_t queue_overflow_count;     // frames dropped because the bounded queue was full
    uint64_t queued_frame_count;       // frames currently waiting in the bounded queue
    uint64_t buffer_frame_capacity;    // the endpoint's total buffer size, in frames
    uint64_t last_padding_frames;      // GetCurrentPadding() result from the most recent drain attempt
} ChipestraAudioOutputDiagnostics;

// Writes `handle`'s currently selected audio output's diagnostic counters
// to *out_diagnostics.
//
// Returns CHIPESTRA_E_INVALID_ARGUMENT if handle or out_diagnostics is null.
// Returns CHIPESTRA_E_INVALID_STATE if handle is not a live instance currently in
// the Ready, Playing, or Paused state (i.e. no output has been
// selected/started).
CHIPESTRA_API ChipestraResult chipestra_get_audio_output_diagnostics(ChipestraHandle handle,
                                                    ChipestraAudioOutputDiagnostics* out_diagnostics) CHIPESTRA_NOEXCEPT;

// ---------------------------------------------------------------------
// Channel Meter group - per-channel LEVEL/PAN/MUTE detail.
// chipestra_get_machine_channel_count above gives only the count; these give the
// per-index detail and the get/set access a Channel Strip UI needs.
//
// All functions in this group take a 0-based `index` (< the count from
// chipestra_get_machine_channel_count). `channel_id` inside
// ChipestraLogicalChannelInfo is the Machine-internal id at that index, for
// diagnostics only - never an input to these functions.
//
// Concurrency: the getter/setters take the same instance-level lock the playback
// thread holds once per render block, so they may be called from the UI thread
// while Playing/Paused.
// ---------------------------------------------------------------------

// One LogicalChannel's full Channel Strip state.
typedef struct ChipestraLogicalChannelInfo {
    uint32_t channel_id;          // LogicalChannel::Id - diagnostic/logging only, see group comment above
    char device_name[64];         // LogicalChannel::DeviceName - which sound chip this channel belongs to
    char channel_name[64];        // LogicalChannel::ChannelName - the Channel Strip's name label
    uint32_t native_sample_rate;  // LogicalChannel::NativeSampleRate
    uint8_t muted;                // LogicalChannel::Mute
    float pan;                    // LogicalChannel::Pan, -1 (left) to +1 (right), 0 = center
    float gain;                   // LogicalChannel::Gain, linear, 1.0 = unity - the Channel Strip's LEVEL fader
    float peak;                   // LogicalChannel::Peak, most recent block's peak absolute sample value (mono)
    float rms;                    // LogicalChannel::RMS, most recent block's RMS level (mono)
    // True per-channel L/R meter values (post-pan); peak/rms above stay mono.
    // Appended at the tail (append-only).
    float peak_left;
    float peak_right;
    float rms_left;
    float rms_right;
    // Peak of the channel's own sound before the user's Mute/Gain/Pan and
    // the master (still scaled by the board's fixed RoutingGain). Unlike
    // peak/peak_left/peak_right it does not drop to 0 when the user mutes the
    // channel, so a caller can tell "the song is silent" from "the user
    // silenced it". Appended at the tail.
    float source_peak;
} ChipestraLogicalChannelInfo;

// Writes the `index`-th LogicalChannel's current Channel Strip state to
// *out_info (0-based, < chipestra_get_machine_channel_count's result).
//
// Returns CHIPESTRA_E_INVALID_ARGUMENT if handle or out_info is null.
// Returns CHIPESTRA_E_INVALID_STATE if handle is not a live instance, or has no
// Machine currently loaded (Created state) - same gating as
// chipestra_get_machine_channel_count.
// Returns CHIPESTRA_E_NOT_FOUND if index >= the channel count.
CHIPESTRA_API ChipestraResult chipestra_get_logical_channel_info(ChipestraHandle handle, uint32_t index,
                                                ChipestraLogicalChannelInfo* out_info) CHIPESTRA_NOEXCEPT;

// Sets the `index`-th LogicalChannel's Mute flag (the Channel Strip's M
// button). Takes effect on the very next render block.
//
// Returns CHIPESTRA_E_INVALID_ARGUMENT if handle is null.
// Returns CHIPESTRA_E_INVALID_STATE if handle is not a live instance, or has no
// Machine currently loaded (Created state).
// Returns CHIPESTRA_E_NOT_FOUND if index >= the channel count.
CHIPESTRA_API ChipestraResult chipestra_set_logical_channel_muted(ChipestraHandle handle, uint32_t index,
                                                 uint8_t muted) CHIPESTRA_NOEXCEPT;

// Sets the `index`-th LogicalChannel's Pan (the Channel Strip's pan knob).
// Not clamped to [-1, +1] - Mixer::MixBlock's linear pan law
// still produces a well-defined, finite result outside that range, so this
// deliberately doesn't reject a caller's out-of-range value the way an
// unknown index is rejected; a UI control should itself constrain the knob
// to [-1, +1] since that's the meaningful range, but nothing downstream
// requires it.
//
// Returns CHIPESTRA_E_INVALID_ARGUMENT if handle is null.
// Returns CHIPESTRA_E_INVALID_STATE if handle is not a live instance, or has no
// Machine currently loaded (Created state).
// Returns CHIPESTRA_E_NOT_FOUND if index >= the channel count.
CHIPESTRA_API ChipestraResult chipestra_set_logical_channel_pan(ChipestraHandle handle, uint32_t index, float pan) CHIPESTRA_NOEXCEPT;

// Returns the `index`-th LogicalChannel's Pan to song-sync mode (the Channel
// Meter's ALL RESET): on boards where the sound driver
// programs stereo routing itself (e.g. XBoardMachine's YM2151 RL bits/
// SegaPCM per-voice volumes), a prior chipestra_set_logical_channel_pan call marks
// the channel overridden, which freezes Pan at that value instead of
// tracking the driver in real time (chipestra_get_logical_channel_info's own Pan
// field, polled by the UI, reflects whichever is currently in effect).
// This call clears that override and resets Pan to 0.0 (center) as an
// immediate, well-defined value - a board with live auto-sync overwrites it
// with the driver's real value on the very next rendered block; a board
// without one (e.g. TestMachine) has no other way to reach a defined
// "un-overridden" Pan.
//
// Returns CHIPESTRA_E_INVALID_ARGUMENT if handle is null.
// Returns CHIPESTRA_E_INVALID_STATE if handle is not a live instance, or has no
// Machine currently loaded (Created state).
// Returns CHIPESTRA_E_NOT_FOUND if index >= the channel count.
CHIPESTRA_API ChipestraResult chipestra_clear_logical_channel_pan_override(ChipestraHandle handle, uint32_t index) CHIPESTRA_NOEXCEPT;

// Sets the `index`-th LogicalChannel's Gain (the Channel Strip's LEVEL
// fader). Linear multiplier, 1.0 = unity - not clamped, same rationale as
// chipestra_set_logical_channel_pan above.
//
// Returns CHIPESTRA_E_INVALID_ARGUMENT if handle is null.
// Returns CHIPESTRA_E_INVALID_STATE if handle is not a live instance, or has no
// Machine currently loaded (Created state).
// Returns CHIPESTRA_E_NOT_FOUND if index >= the channel count.
CHIPESTRA_API ChipestraResult chipestra_set_logical_channel_gain(ChipestraHandle handle, uint32_t index, float gain) CHIPESTRA_NOEXCEPT;

// Master section state: master VOLUME slider, MUTE button and L/R level meter,
// applied to the final mixed stereo block after every Channel Strip and before
// the audio output. Not a LogicalChannel.
typedef struct ChipestraMasterState {
    float gain;         // linear multiplier, 1.0 = unity - the master VOLUME slider
    uint8_t muted;       // the master MUTE button
    float peak_left;     // most recent block's peak absolute sample value, left
    float peak_right;    // most recent block's peak absolute sample value, right
    float rms_left;      // most recent block's RMS level, left
    float rms_right;     // most recent block's RMS level, right
} ChipestraMasterState;

// Writes `handle`'s current master gain/mute/meter state to *out_state.
// peak_*/rms_* read 0 until at least one render block has been produced
// (i.e. before Playing has rendered anything yet).
//
// Returns CHIPESTRA_E_INVALID_ARGUMENT if handle or out_state is null.
// Returns CHIPESTRA_E_INVALID_STATE if handle is not a live instance, or has no
// Machine currently loaded (Created state).
CHIPESTRA_API ChipestraResult chipestra_get_master_state(ChipestraHandle handle, ChipestraMasterState* out_state) CHIPESTRA_NOEXCEPT;

// Sets the master VOLUME slider's linear gain. Not clamped - same rationale
// as chipestra_set_logical_channel_pan above.
//
// Returns CHIPESTRA_E_INVALID_ARGUMENT if handle is null.
// Returns CHIPESTRA_E_INVALID_STATE if handle is not a live instance, or has no
// Machine currently loaded (Created state).
CHIPESTRA_API ChipestraResult chipestra_set_master_gain(ChipestraHandle handle, float gain) CHIPESTRA_NOEXCEPT;

// Sets the master MUTE button. When muted, the final mixed output is
// silenced (multiplied by 0) before submission to the audio output - the
// per-channel Mute states underneath are untouched, so unmuting master
// restores exactly what each channel's own Mute/Pan/Gain was already
// producing.
//
// Returns CHIPESTRA_E_INVALID_ARGUMENT if handle is null.
// Returns CHIPESTRA_E_INVALID_STATE if handle is not a live instance, or has no
// Machine currently loaded (Created state).
CHIPESTRA_API ChipestraResult chipestra_set_master_muted(ChipestraHandle handle, uint8_t muted) CHIPESTRA_NOEXCEPT;

// Metadata for one registered MachineDefinition. Fixed-size, null-terminated,
// UTF-8 buffers; a name longer than its buffer is truncated (still
// null-terminated) rather than overflowing.
typedef struct ChipestraMachineInfo {
    char machine_id[64];
    char display_name[128];
    char category[16];          // "ARCADE" or "CONSOLE"
    uint32_t variant_count;     // number of selectable Variants this Machine has
    // MachineDefinition::Manufacturer (appended). Library grouping
    // (Category->Manufacturer->System->Game) should use this field, not
    // GameDefinition.Manufacturer, which is informational only.
    char manufacturer[64];
} ChipestraMachineInfo;

// Number of MachineDefinitions currently registered in the MachineRegistry.
// Never fails; instance-independent (the registry is a process-wide read-only
// catalog, not per-instance state).
CHIPESTRA_API uint32_t chipestra_get_machine_count(void) CHIPESTRA_NOEXCEPT;

// Writes metadata for the registered MachineDefinition at `index` (0-based,
// in registration order - the same order Enumerate() returns) to
// *out_info.
//
// Returns CHIPESTRA_E_INVALID_ARGUMENT if out_info is null.
// Returns CHIPESTRA_E_NOT_FOUND if index >= chipestra_get_machine_count().
CHIPESTRA_API ChipestraResult chipestra_get_machine_info(uint32_t index, ChipestraMachineInfo* out_info) CHIPESTRA_NOEXCEPT;

// ---------------------------------------------------------------------
// GameDefinition / ResourceResolver group.
//
// Exposes Native's XML definition parser and ResourceResolver across the C ABI, so
// Managed Core can drive chipestra_load_machine's `resources`/`resource_count`
// parameters with real, verified ROM bytes.
// ---------------------------------------------------------------------

// Opaque handle to one parsed GameDefinition. Never dereferenced by
// Managed Core - see chipestra_game_definition_get_info/_get_resource/_get_track
// below for how its contents are read back.
typedef struct ChipestraGameDefinitionOpaque* ChipestraGameDefinitionHandle;

// Opaque handle to one ResourceResolver::Resolve() outcome. Owns
// the resolved resource bytes that ChipestraResolvedResource.data points into
// (see chipestra_resource_resolution_get_resolved) - see that function's own
// lifetime note.
typedef struct ChipestraResourceResolutionOpaque* ChipestraResourceResolutionHandle;

// Parses `xml_utf8` (a game definition XML document, not a file path; the caller
// reads the file itself and passes its contents here) and
// writes the resulting handle to *out_handle on success.
//
// If out_detail/out_detail_size are non-null, a human-readable diagnostic is
// always written there (truncated, always null-terminated) regardless of
// success or failure - empty on success. It is a debugging aid, not part of the
// stable contract, so callers must not match on its text.
//
// Returns CHIPESTRA_E_INVALID_ARGUMENT if xml_utf8 or out_handle is null.
// Returns CHIPESTRA_E_INVALID_ARGUMENT if the input fails to parse for any reason
// (too large, contains a DOCTYPE, malformed XML, no <chipestra><game> element, or
// a required attribute is missing) - see out_detail for which. Every
// failure maps to this one ChipestraResult. DOCTYPE declarations are rejected, so
// external entities are never loaded.
// Returns CHIPESTRA_E_INTERNAL if parsing throws; no C++ exception ever escapes
// this function.
CHIPESTRA_API ChipestraResult chipestra_parse_game_definition(const char* xml_utf8, uint32_t xml_length,
                                             ChipestraGameDefinitionHandle* out_handle, char* out_detail,
                                             uint32_t out_detail_size) CHIPESTRA_NOEXCEPT;

// Releases a handle from chipestra_parse_game_definition. Safe to call with a null
// handle (no-op). Frees any ChipestraResourceResolutionHandle obtained by
// resolving against this GameDefinition independently - resolution handles
// do not need to outlive the ChipestraGameDefinitionHandle they were resolved from,
// but nothing stops them from being freed in either order.
CHIPESTRA_API void chipestra_free_game_definition(ChipestraGameDefinitionHandle handle) CHIPESTRA_NOEXCEPT;

// Metadata for one parsed GameDefinition. Fixed-size, null-terminated, UTF-8
// buffers, as in ChipestraMachineInfo - a name longer than its buffer is
// truncated (still null-terminated) rather than overflowing.
typedef struct ChipestraGameInfo {
    char game_id[64];
    char title[128];
    char machine_id[64];      // looked up via chipestra_get_machine_count/_get_machine_info, or passed straight to chipestra_load_machine
    char variant_id[64];
    char manufacturer[64];    // informational only - "" if the XML omitted it
    uint32_t resource_count;  // number of <rom> entries - valid indices for chipestra_game_definition_get_resource
    uint32_t track_count;     // number of <track> entries - valid indices for chipestra_game_definition_get_track
    // <game archive="..."> verbatim (appended); "" if the XML omitted it. A caller
    // that wants archive-scoped resolution must copy this value into every
    // ChipestraRequiredResourceInfo::archive it builds before calling
    // chipestra_resolve_resources.
    char archive[64];
} ChipestraGameInfo;

// Writes `handle`'s GameDefinition-level metadata to *out_info.
//
// Returns CHIPESTRA_E_INVALID_ARGUMENT if handle or out_info is null.
CHIPESTRA_API ChipestraResult chipestra_game_definition_get_info(ChipestraGameDefinitionHandle handle, ChipestraGameInfo* out_info) CHIPESTRA_NOEXCEPT;

// One <rom> entry, as declared - not yet resolved against any ROM Root. Used both
// as an output (from chipestra_game_definition_get_resource) and as an input array
// element (to chipestra_resolve_resources).
typedef struct ChipestraRequiredResourceInfo {
    char logical_name[64];    // <rom name="..."> - RequiredResource::LogicalName
    char accepted_name[260];  // <rom file="..."> (one file per <rom>)
    uint64_t size_bytes;      // meaningful only if has_size_bytes is nonzero
    uint8_t has_size_bytes;
    uint32_t crc32;           // meaningful only if has_crc32 is nonzero
    uint8_t has_crc32;
    char sha1[64];            // "" if the XML omitted it
    uint8_t required;         // currently always nonzero
    // Optional archive scope. "" means unscoped. chipestra_game_definition_get_resource
    // always leaves this "" (archive is a per-<game> attribute; read it from
    // ChipestraGameInfo::archive); callers building a chipestra_resolve_resources
    // request populate it themselves.
    char archive[64];
} ChipestraRequiredResourceInfo;

// Writes `handle`'s `index`-th declared resource (0-based, matching
// GameDefinition::Resources order) to *out_info.
//
// Returns CHIPESTRA_E_INVALID_ARGUMENT if handle or out_info is null.
// Returns CHIPESTRA_E_NOT_FOUND if index >= that GameDefinition's resource_count.
CHIPESTRA_API ChipestraResult chipestra_game_definition_get_resource(ChipestraGameDefinitionHandle handle, uint32_t index,
                                                    ChipestraRequiredResourceInfo* out_info) CHIPESTRA_NOEXCEPT;

// One <track> entry. `command` is an opaque, null-terminated string (e.g. "0x92"):
// what a command value means is up to the target board's sound driver, not the
// Native Runtime. Managed Core parses it into the raw byte chipestra_send_command
// expects (decimal or "0x"-prefixed hex).
typedef struct ChipestraTrackInfo {
    char id[64];       // <track id="..."> - stable handle a Player selects by
    char command[32];
    char title[128];
    // Appended fields: <track type="control">/<track loops="false"> (both 0 when
    // absent). Native only carries them across the ABI boundary for Managed Core's
    // Player to use.
    uint8_t is_control_command;
    uint8_t loops;
    // Appended: <track type="wsg"> (0 when absent): a request for a WSG sound effect
    // (the game's own test-screen number in `command`), sent as MusicCommandKind::Wsg.
    // The Machine decides what it means.
    uint8_t is_wsg;
} ChipestraTrackInfo;

// Writes `handle`'s `index`-th declared track (0-based, matching
// GameDefinition::Tracks/document order) to *out_info.
//
// Returns CHIPESTRA_E_INVALID_ARGUMENT if handle or out_info is null.
// Returns CHIPESTRA_E_NOT_FOUND if index >= that GameDefinition's track_count.
CHIPESTRA_API ChipestraResult chipestra_game_definition_get_track(ChipestraGameDefinitionHandle handle, uint32_t index,
                                                 ChipestraTrackInfo* out_info) CHIPESTRA_NOEXCEPT;

// Resolves `resources` (normally read from a GameDefinition via
// chipestra_game_definition_get_resource, one ChipestraRequiredResourceInfo per
// declared <rom>) against `rom_root` (loose files + ZIP archives found under it)
// and writes the resulting handle to *out_handle. Never auto-selects among
// multiple candidates for the same resource - see
// chipestra_resource_resolution_get_summary/_get_failure for how an
// ambiguous/missing/hash-mismatched resource is reported.
//
// It takes `resources`/`resource_count` directly rather than a
// ChipestraGameDefinitionHandle, so the caller need not keep a GameDefinition
// alive just to resolve against it later.
//
// This call itself succeeding (CHIPESTRA_OK) says only that the resolution
// machinery ran without error - it does NOT mean every resource resolved.
// Always check chipestra_resource_resolution_get_summary's `ok` field afterwards.
//
// Returns CHIPESTRA_E_INVALID_ARGUMENT if rom_root or out_handle is null, or if
// resource_count > 0 and resources is null.
// Returns CHIPESTRA_E_INTERNAL if resolution throws; no C++ exception ever escapes
// this function.
CHIPESTRA_API ChipestraResult chipestra_resolve_resources(const char* rom_root, const ChipestraRequiredResourceInfo* resources,
                                         uint32_t resource_count,
                                         ChipestraResourceResolutionHandle* out_handle) CHIPESTRA_NOEXCEPT;

// Releases a handle from chipestra_resolve_resources. Safe to call with a null
// handle (no-op).
//
// Frees the memory ChipestraResolvedResource.data pointers from
// chipestra_resource_resolution_get_resolved point into (see that function's
// lifetime note) - callers must finish using any such pointers, in
// particular any chipestra_load_machine call built from them, before calling this.
CHIPESTRA_API void chipestra_free_resource_resolution(ChipestraResourceResolutionHandle handle) CHIPESTRA_NOEXCEPT;

// Overall outcome of one chipestra_resolve_resources call.
typedef struct ChipestraResolutionSummary {
    uint8_t ok;                // nonzero iff every Required resource resolved successfully
    uint32_t resolved_count;   // valid indices for chipestra_resource_resolution_get_resolved
    uint32_t failure_count;    // valid indices for chipestra_resource_resolution_get_failure
} ChipestraResolutionSummary;

// Writes `handle`'s overall resolution outcome to *out_summary.
//
// Returns CHIPESTRA_E_INVALID_ARGUMENT if handle or out_summary is null.
CHIPESTRA_API ChipestraResult chipestra_resource_resolution_get_summary(ChipestraResourceResolutionHandle handle,
                                                       ChipestraResolutionSummary* out_summary) CHIPESTRA_NOEXCEPT;

// Why one declared resource failed to resolve. `result` is one of
// CHIPESTRA_E_NOT_FOUND / CHIPESTRA_E_AMBIGUOUS / CHIPESTRA_E_HASH_MISMATCH / CHIPESTRA_E_UNSUPPORTED
// - never CHIPESTRA_OK, since this only ever describes a failure.
typedef struct ChipestraResolutionFailure {
    char logical_name[64];
    ChipestraResult result;
    char detail[256];  // human-readable diagnostic, not part of the stable ABI - do not match on its text
} ChipestraResolutionFailure;

// Writes `handle`'s `index`-th resolution failure (0-based) to *out_failure.
//
// Returns CHIPESTRA_E_INVALID_ARGUMENT if handle or out_failure is null.
// Returns CHIPESTRA_E_NOT_FOUND if index >= that resolution's failure_count.
CHIPESTRA_API ChipestraResult chipestra_resource_resolution_get_failure(ChipestraResourceResolutionHandle handle, uint32_t index,
                                                       ChipestraResolutionFailure* out_failure) CHIPESTRA_NOEXCEPT;

// Writes `handle`'s `index`-th successfully resolved resource (0-based) to
// *out_resolved, INCLUDING a `data` pointer into memory this
// ChipestraResourceResolutionHandle owns (see ChipestraResolvedResource's own lifetime
// note above chipestra_load_machine) - it stays valid only until `handle` is freed
// with chipestra_free_resource_resolution. Typical use: call this for every index
// up to resolved_count, assemble the results into an array, pass that array
// straight to chipestra_load_machine (which copies what it needs), then free
// `handle`.
//
// Returns CHIPESTRA_E_INVALID_ARGUMENT if handle or out_resolved is null.
// Returns CHIPESTRA_E_NOT_FOUND if index >= that resolution's resolved_count.
CHIPESTRA_API ChipestraResult chipestra_resource_resolution_get_resolved(ChipestraResourceResolutionHandle handle, uint32_t index,
                                                        ChipestraResolvedResource* out_resolved) CHIPESTRA_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------
// C++-only internal contract below this point. IMachine never crosses the
// extern "C" ABI boundary above: no chipestra_* function takes or returns an
// IMachine pointer. Managed Core never sees these types.
// ---------------------------------------------------------------------

// EmuTick: the Native Runtime's smallest schedulable unit of emulation time. It is
// an integer count with no fixed real-world duration of its own, and nothing here
// ever reads a real-time clock (emulation time is deterministic).
using EmuTick = std::uint64_t;

// The Native Runtime's global EmuTick rate: one EmuTick is one frame of the
// canonical stereo mix (Mixer::kCanonicalSampleRateHz = 192kHz). IMachine::Render
// therefore receives a duration measured in canonical output frames; a Machine
// converts that to its own device clocks with ClockConverter.
inline constexpr std::uint64_t kNativeRuntimeTickRateHz = 192'000;

// A span of emulation time, measured in EmuTicks. Distinct from EmuTime (a
// point in time) so the two can never be accidentally interchanged - e.g.
// IMachine::Render below takes a duration, not a point.
struct EmuDuration {
    EmuTick ticks = 0;

    constexpr EmuDuration() = default;
    constexpr explicit EmuDuration(EmuTick t) : ticks(t) {}
};

constexpr bool operator==(EmuDuration a, EmuDuration b) { return a.ticks == b.ticks; }
constexpr bool operator!=(EmuDuration a, EmuDuration b) { return !(a == b); }
constexpr bool operator<(EmuDuration a, EmuDuration b) { return a.ticks < b.ticks; }
constexpr bool operator<=(EmuDuration a, EmuDuration b) { return !(b < a); }
constexpr bool operator>(EmuDuration a, EmuDuration b) { return b < a; }
constexpr bool operator>=(EmuDuration a, EmuDuration b) { return !(a < b); }
constexpr EmuDuration operator+(EmuDuration a, EmuDuration b) { return EmuDuration(a.ticks + b.ticks); }

// A point in emulation time, measured in EmuTicks elapsed since the owning
// Machine's Initialize()/Reset(). Distinct from EmuDuration (a span) - see
// above.
struct EmuTime {
    EmuTick ticks = 0;

    constexpr EmuTime() = default;
    constexpr explicit EmuTime(EmuTick t) : ticks(t) {}
};

constexpr bool operator==(EmuTime a, EmuTime b) { return a.ticks == b.ticks; }
constexpr bool operator!=(EmuTime a, EmuTime b) { return !(a == b); }
constexpr bool operator<(EmuTime a, EmuTime b) { return a.ticks < b.ticks; }
constexpr bool operator<=(EmuTime a, EmuTime b) { return !(b < a); }
constexpr bool operator>(EmuTime a, EmuTime b) { return b < a; }
constexpr bool operator>=(EmuTime a, EmuTime b) { return !(a < b); }
constexpr EmuTime operator+(EmuTime t, EmuDuration d) { return EmuTime(t.ticks + d.ticks); }
constexpr EmuDuration operator-(EmuTime a, EmuTime b) { return EmuDuration(a.ticks - b.ticks); }

// ChannelId identifies one LogicalChannel within a Machine. A Machine assigns
// these itself and they stay stable for as long as it is loaded.
using ChannelId = std::uint32_t;

// LogicalChannelBuffer - one channel's mono float32 samples, produced by a
// Machine's Render() and consumed by the Mixer.
class LogicalChannelBuffer {
public:
    void Append(const float* samples, std::size_t count);
    void Clear();

    std::size_t Size() const;
    const float* Data() const;
    float operator[](std::size_t index) const;

private:
    std::vector<float> samples_;
};

// LogicalChannelSink - the destination a Machine's Render() implementation
// writes its per-channel audio into. A Machine only ever sees this sink, never a
// Mixer directly.
class LogicalChannelSink {
public:
    // Registers a channel this sink accepts samples for. Must be called
    // before any WriteSamples()/HasChannel()/ChannelBuffer() call for that
    // channel - typically from Machine::Initialize().
    // Throws std::logic_error if `channel` is already registered.
    void RegisterChannel(ChannelId channel);

    bool HasChannel(ChannelId channel) const;

    // Appends `count` mono float32 samples for `channel`.
    // Throws std::out_of_range if `channel` was never registered.
    void WriteSamples(ChannelId channel, const float* samples, std::size_t count);

    // Non-owning access to a channel's accumulated buffer.
    // Throws std::out_of_range if `channel` was never registered.
    const LogicalChannelBuffer& ChannelBuffer(ChannelId channel) const;

    // Clears every registered channel's buffer (e.g. before the next block).
    void ClearAll();

private:
    std::unordered_map<ChannelId, LogicalChannelBuffer> buffers_;
};

// LogicalChannel - per-channel metadata: identity, mixer controls (Mute/Pan/Gain),
// and meter readouts (Peak/RMS) for the UI's Channel Strip. Distinct from
// LogicalChannelBuffer/LogicalChannelSink above, which carry the samples.
struct LogicalChannel {
    ChannelId Id{0};
    std::string DeviceName{};
    std::string ChannelName{};
    std::uint32_t NativeSampleRate{0};  // the channel's native output rate in Hz
    bool Mute{false};
    float Pan{0.0f};    // range is -1 (left) to +1 (right)
    float Gain{1.0f};   // linear multiplier, 1.0 = unity - the UI's Channel Strip LEVEL (user-facing)
    float Peak{0.0f};   // written by the Mixer, most recent block's peak absolute sample value
    float RMS{0.0f};    // written by the Mixer, most recent block's RMS level
    // Fixed per-device mix weight set by the Machine (never touched by the UI),
    // modelling the real board's per-sound-chip routing gain. The Mixer folds it in
    // on top of Gain. 1.0 = no attenuation.
    float RoutingGain{1.0f};
    // True per-channel L/R meter values, written by the Mixer alongside Peak/RMS
    // above (which stay mono): the post-pan left/right contributions this channel
    // adds into the mixed stereo output for the block.
    float PeakLeft{0.0f};
    float PeakRight{0.0f};
    float RMSLeft{0.0f};
    float RMSRight{0.0f};
    // Peak of raw * RoutingGain, i.e. before the user's Mute/Gain/Pan.
    float SourcePeak{0.0f};
    // Some boards drive Pan live from the sound driver's own real-time state every
    // block. When true, that auto-sync must leave Pan alone (it still updates
    // RoutingGain); set when the UI sets Pan, cleared by
    // chipestra_clear_logical_channel_pan_override.
    bool PanOverridden{false};
};

// MusicCommandKind - what a MusicCommand asks the Machine to do. Raw is for command
// values whose meaning is entirely up to the Machine's own sound driver convention.
enum class MusicCommandKind {
    TrackSelect,  // select/start a track; Value carries the track number
    Stop,         // stop playback
    Raw,          // Value (and optionally Auxiliary) are passed through as-is
    Wsg,          // Value is a WSG sound-effect number (the game's own test-screen number); only a Machine
                  // whose AcceptsCommand() says yes gets it - the generic runtime never interprets it
};

// MusicCommand - one command delivered to a Machine via IMachine::SendCommand().
//
// Value/Auxiliary are deliberately opaque bytes: what they mean is a property of
// the target board's sound driver, not something the generic Native Runtime
// decides. A Machine's command interface turns a MusicCommand into the physical
// bus write(s)/interrupt(s) its board needs.
struct MusicCommand {
    MusicCommandKind Kind{MusicCommandKind::Raw};
    std::uint8_t Value{0};
    std::uint8_t Auxiliary{0};
};

// Contract implemented by every board-specific Machine.
class IMachine {
public:
    virtual ~IMachine() = default;

    // Deterministic setup, called exactly once right after construction and
    // before any other method. Must not depend on wall-clock time or perform
    // audio device I/O; ROM data comes through the Resource Resolver, not here.
    virtual void Initialize() = 0;

    // Returns this Machine to its post-Initialize state, deterministically:
    // the same call sequence must always produce the same
    // observable state.
    virtual void Reset() = 0;

    // Releases any resources acquired since Initialize. Called exactly once,
    // right before the Machine is destroyed.
    virtual void Shutdown() = 0;

    // This Machine's own LogicalChannel metadata (one entry per channel it
    // exposes to the Mixer), owned by the Machine. Non-const because the
    // Mixer writes each channel's Peak/RMS back through this same reference
    // after mixing a block - the Mixer never owns a separate copy.
    virtual std::vector<LogicalChannel>& LogicalChannels() = 0;

    // Renders `duration` worth of audio, writing each of this Machine's
    // channels' samples into `sink` via LogicalChannelSink::WriteSamples.
    // Initialize() receives no sink (a Machine can be constructed before any
    // sink exists), so each channel this Machine exposes must be registered
    // with `sink` (LogicalChannelSink::RegisterChannel) before this Machine
    // writes to it for the first time - in practice, on this method's first
    // call.
    virtual void Render(EmuDuration duration, LogicalChannelSink* sink) = 0;

    // Delivers one music command to the Machine.
    virtual void SendCommand(const MusicCommand& command) = 0;

    // Whether SendCommand() can act on this command (checked by chipestra_send_command before it calls
    // SendCommand, which then returns CHIPESTRA_E_UNSUPPORTED_COMMAND for a no). The default accepts every
    // kind except Wsg.
    virtual bool AcceptsCommand(const MusicCommand& command) const { return command.Kind != MusicCommandKind::Wsg; }

    // Called by chipestra_clear_logical_channel_pan_override() right after it
    // clears LogicalChannel::PanOverridden for `channelId`, so a Machine whose
    // Pan-sync only runs from register-write handlers (or is fixed once at
    // Initialize()) can re-apply its own current Pan immediately. Default no-op: a
    // Machine that re-derives Pan every Render() block needs no help here.
    virtual void OnPanOverrideCleared(std::uint32_t channelId) { (void)channelId; }
};

#endif  // __cplusplus
