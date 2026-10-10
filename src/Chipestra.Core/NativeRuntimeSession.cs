// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using System.Collections.Generic;
using System.Linq;

namespace Chipestra.Core
{
    /// <summary>
    /// Owns one Native Runtime instance's lifecycle. UI and other
    /// managed code must hold and Dispose this rather than calling
    /// <see cref="NativeMethods"/> directly.
    /// </summary>
    public sealed class NativeRuntimeSession : IDisposable
    {
        private IntPtr _handle;
        private bool _disposed;

        public NativeRuntimeSession()
        {
            // Before any other native call: a DLL from a different build (stale/new) must fail
            // loudly here rather than misread arguments it does not know about.
            ChipestraRuntime.EnsureCompatibleNativeVersion();

            ChipestraResult result = NativeMethods.chipestra_create_instance(out IntPtr handle);
            if (result != ChipestraResult.Ok || handle == IntPtr.Zero)
            {
                throw new InvalidOperationException($"Failed to create native instance. ChipestraResult: {result}");
            }

            _handle = handle;
        }

        public void Dispose()
        {
            if (_disposed)
            {
                return;
            }

            NativeMethods.chipestra_destroy_instance(_handle);
            _handle = IntPtr.Zero;
            _disposed = true;
        }

        /// <summary>
        /// Loads the Machine/Variant identified by <paramref name="machineId"/> and
        /// <paramref name="variantId"/>, transitioning Created -> MachineLoaded, with no
        /// resolved ROM/data resources wired in (equivalent to calling
        /// <see cref="LoadMachineWithResources"/> with an empty resource list - a
        /// Machine that genuinely needs no ROM, e.g. TEST_MACHINE). Both ids must exactly match a MachineRegistry
        /// entry (see <see cref="GetMachineCount"/>/<see cref="GetMachineInfo"/> to
        /// enumerate what's registered) - no fuzzy matching.
        /// </summary>
        public ChipestraResult LoadMachine(string machineId, string variantId)
        {
            if (_disposed)
            {
                throw new ObjectDisposedException(nameof(NativeRuntimeSession));
            }

            return NativeMethods.chipestra_load_machine(_handle, machineId, variantId, null, 0);
        }

        /// <summary>
        /// Resolves <paramref name="requiredResources"/> against
        /// <paramref name="romRoot"/> (via the C ABI's chipestra_resolve_resources) and, only if
        /// every one resolves successfully, loads (<paramref name="machineId"/>,
        /// <paramref name="variantId"/>) with those resolved bytes wired in via
        /// chipestra_load_machine.
        ///
        /// <paramref name="resolution"/> always reports what resolution found,
        /// success or not (ambiguous candidates are never auto-selected). If any
        /// resource fails to resolve, the Machine is NOT loaded (this instance's
        /// state is left exactly as it was) and the returned ChipestraResult is that of
        /// the first unresolved resource, in <paramref name="requiredResources"/>
        /// order (matching ResourceResolver::Resolve()'s own deterministic order).
        ///
        /// <paramref name="archive"/>, if given, scopes resolution to one
        /// archive/folder (<see cref="GameDefinition.Archive"/>); null searches the whole ROM Root.
        /// </summary>
        public ChipestraResult LoadMachineWithResources(string machineId, string variantId,
            IReadOnlyList<RomResource> requiredResources, string romRoot, out RomResolutionResult resolution,
            string? archive = null)
        {
            if (_disposed)
            {
                throw new ObjectDisposedException(nameof(NativeRuntimeSession));
            }

            ChipestraRequiredResourceInfo[] requestArray = requiredResources.Select(r => ToNativeRequiredResourceInfo(r, archive)).ToArray();
            ChipestraResult resolveResult = NativeMethods.chipestra_resolve_resources(
                romRoot, requestArray.Length > 0 ? requestArray : null, (uint)requestArray.Length,
                out IntPtr resolutionHandle);
            if (resolveResult != ChipestraResult.Ok)
            {
                resolution = new RomResolutionResult { Entries = Array.Empty<RomResolutionEntry>() };
                return resolveResult;
            }

            try
            {
                ChipestraResult summaryResult = NativeMethods.chipestra_resource_resolution_get_summary(resolutionHandle, out ChipestraResolutionSummary summary);
                if (summaryResult != ChipestraResult.Ok)
                {
                    resolution = new RomResolutionResult { Entries = Array.Empty<RomResolutionEntry>() };
                    return summaryResult;
                }

                var failuresByLogicalName = new Dictionary<string, ChipestraResolutionFailure>(StringComparer.Ordinal);
                for (uint i = 0; i < summary.FailureCount; i++)
                {
                    ChipestraResult failureResult = NativeMethods.chipestra_resource_resolution_get_failure(resolutionHandle, i, out ChipestraResolutionFailure failure);
                    if (failureResult == ChipestraResult.Ok)
                    {
                        failuresByLogicalName[failure.LogicalName] = failure;
                    }
                }

                var entries = new List<RomResolutionEntry>(requiredResources.Count);
                foreach (RomResource resource in requiredResources)
                {
                    if (failuresByLogicalName.TryGetValue(resource.Name, out ChipestraResolutionFailure failure))
                    {
                        entries.Add(new RomResolutionEntry
                        {
                            Resource = resource,
                            Status = ToRomResolutionStatus(failure.Result),
                            Detail = failure.Detail,
                        });
                    }
                    else
                    {
                        entries.Add(new RomResolutionEntry { Resource = resource, Status = RomResolutionStatus.Resolved });
                    }
                }
                resolution = new RomResolutionResult { Entries = entries };

                if (summary.Ok == 0)
                {
                    // At least one Required resource failed - report the first
                    // failure (requiredResources order, matching Resolve()'s own
                    // deterministic order) and do not touch the Machine.
                    RomResolutionEntry? firstFailure = entries.FirstOrDefault(e => e.Status != RomResolutionStatus.Resolved);
                    return firstFailure != null
                        ? failuresByLogicalName[firstFailure.Resource.Name].Result
                        : ChipestraResult.NotFound;
                }

                var resolvedArray = new ChipestraResolvedResourceNative[summary.ResolvedCount];
                for (uint i = 0; i < summary.ResolvedCount; i++)
                {
                    ChipestraResult resolvedResult = NativeMethods.chipestra_resource_resolution_get_resolved(resolutionHandle, i, out ChipestraResolvedResourceNative resolved);
                    if (resolvedResult != ChipestraResult.Ok)
                    {
                        return resolvedResult;
                    }
                    resolvedArray[i] = resolved;
                }

                return NativeMethods.chipestra_load_machine(_handle, machineId, variantId, resolvedArray, (uint)resolvedArray.Length);
            }
            finally
            {
                // Only after chipestra_load_machine above has returned - it copies the
                // bytes it needs out of resolvedArray's Data pointers before
                // returning, but those pointers are only valid while this handle
                // is alive (ChipestraResolvedResourceNative's own remarks).
                NativeMethods.chipestra_free_resource_resolution(resolutionHandle);
            }
        }

        private static ChipestraRequiredResourceInfo ToNativeRequiredResourceInfo(RomResource resource, string? archive)
        {
            return new ChipestraRequiredResourceInfo
            {
                LogicalName = resource.Name,
                AcceptedName = resource.FileName,
                SizeBytes = resource.SizeBytes.HasValue ? (ulong)resource.SizeBytes.Value : 0UL,
                HasSizeBytes = resource.SizeBytes.HasValue ? (byte)1 : (byte)0,
                Crc32 = resource.Crc32 != null ? Convert.ToUInt32(resource.Crc32, 16) : 0U,
                HasCrc32 = resource.Crc32 != null ? (byte)1 : (byte)0,
                Sha1 = resource.Sha1 ?? string.Empty,
                Required = 1,
                // archive is a per-Game value (GameDefinition.Archive), not per-resource, so it is
                // passed as its own parameter; every resource of one Game shares the same scope.
                Archive = archive ?? string.Empty,
            };
        }

        private static RomResolutionStatus ToRomResolutionStatus(ChipestraResult result) => result switch
        {
            ChipestraResult.AmbiguousMatch => RomResolutionStatus.AmbiguousMatch,
            ChipestraResult.HashMismatch => RomResolutionStatus.HashMismatch,
            ChipestraResult.Unsupported => RomResolutionStatus.Unsupported,
            _ => RomResolutionStatus.NotFound,
        };

        /// <summary>Unloads the current Machine, transitioning MachineLoaded -> Created.</summary>
        public ChipestraResult UnloadMachine()
        {
            if (_disposed)
            {
                throw new ObjectDisposedException(nameof(NativeRuntimeSession));
            }

            return NativeMethods.chipestra_unload_machine(_handle);
        }

        /// <summary>Resets the currently loaded Machine to its post-Initialize state.</summary>
        public ChipestraResult Reset()
        {
            if (_disposed)
            {
                throw new ObjectDisposedException(nameof(NativeRuntimeSession));
            }

            return NativeMethods.chipestra_reset_machine(_handle);
        }

        /// <summary>
        /// Number of LogicalChannels the currently loaded Machine exposes.
        /// Safe to call in any state that
        /// has a Machine loaded, including Playing - see
        /// chipestra_get_machine_channel_count's own remarks (chipestra_abi.h) for why that
        /// doesn't run into the same concurrency restriction
        /// <see cref="SendCommand"/> has.
        /// </summary>
        public ChipestraResult GetChannelCount(out uint channelCount)
        {
            channelCount = 0;
            if (_disposed)
            {
                throw new ObjectDisposedException(nameof(NativeRuntimeSession));
            }

            return NativeMethods.chipestra_get_machine_channel_count(_handle, out channelCount);
        }

        // --- Channel Meter group ---

        /// <summary>Gets the index-th LogicalChannel's info.</summary>
        public ChipestraResult GetChannelInfo(uint index, out ChipestraLogicalChannelInfo info)
        {
            info = default;
            if (_disposed)
            {
                throw new ObjectDisposedException(nameof(NativeRuntimeSession));
            }

            return NativeMethods.chipestra_get_logical_channel_info(_handle, index, out info);
        }

        /// <summary>Sets the index-th LogicalChannel's Mute flag.</summary>
        public ChipestraResult SetChannelMuted(uint index, bool muted)
        {
            if (_disposed)
            {
                throw new ObjectDisposedException(nameof(NativeRuntimeSession));
            }

            return NativeMethods.chipestra_set_logical_channel_muted(_handle, index, muted ? (byte)1 : (byte)0);
        }

        /// <summary>Sets the index-th LogicalChannel's Pan value.</summary>
        public ChipestraResult SetChannelPan(uint index, float pan)
        {
            if (_disposed)
            {
                throw new ObjectDisposedException(nameof(NativeRuntimeSession));
            }

            return NativeMethods.chipestra_set_logical_channel_pan(_handle, index, pan);
        }

        /// <summary>Returns the index-th LogicalChannel's Pan to song-sync mode.</summary>
        public ChipestraResult ClearChannelPanOverride(uint index)
        {
            if (_disposed)
            {
                throw new ObjectDisposedException(nameof(NativeRuntimeSession));
            }

            return NativeMethods.chipestra_clear_logical_channel_pan_override(_handle, index);
        }

        /// <summary>Sets the index-th LogicalChannel's Gain value.</summary>
        public ChipestraResult SetChannelGain(uint index, float gain)
        {
            if (_disposed)
            {
                throw new ObjectDisposedException(nameof(NativeRuntimeSession));
            }

            return NativeMethods.chipestra_set_logical_channel_gain(_handle, index, gain);
        }

        /// <summary>Gets the master VOLUME/MUTE/meter state.</summary>
        public ChipestraResult GetMasterState(out ChipestraMasterState state)
        {
            state = default;
            if (_disposed)
            {
                throw new ObjectDisposedException(nameof(NativeRuntimeSession));
            }

            return NativeMethods.chipestra_get_master_state(_handle, out state);
        }

        /// <summary>Sets the master VOLUME slider's gain value.</summary>
        public ChipestraResult SetMasterGain(float gain)
        {
            if (_disposed)
            {
                throw new ObjectDisposedException(nameof(NativeRuntimeSession));
            }

            return NativeMethods.chipestra_set_master_gain(_handle, gain);
        }

        /// <summary>Sets the master MUTE flag.</summary>
        public ChipestraResult SetMasterMuted(bool muted)
        {
            if (_disposed)
            {
                throw new ObjectDisposedException(nameof(NativeRuntimeSession));
            }

            return NativeMethods.chipestra_set_master_muted(_handle, muted ? (byte)1 : (byte)0);
        }

        /// <summary>
        /// Sends one command to the currently loaded Machine. Only valid
        /// outside of Playing - the PlaybackPump thread owns the Machine's mutable
        /// state once playback has started (see <see cref="StartPlayback"/>'s remarks);
        /// call <see cref="StopPlayback"/> first if a command needs to reach a track
        /// mid-playback (a "track switch"; <see cref="Player.SelectTrack"/> does this).
        /// </summary>
        public ChipestraResult SendCommand(MusicCommandKind kind, byte value, byte auxiliary = 0)
        {
            if (_disposed)
            {
                throw new ObjectDisposedException(nameof(NativeRuntimeSession));
            }

            return NativeMethods.chipestra_send_command(_handle, (byte)kind, value, auxiliary);
        }

        /// <summary>Selects NullAudioOutput as this session's audio output (MachineLoaded state only).</summary>
        public ChipestraResult SelectNullOutput()
        {
            if (_disposed)
            {
                throw new ObjectDisposedException(nameof(NativeRuntimeSession));
            }

            return NativeMethods.chipestra_select_null_output(_handle);
        }

        /// <summary>Starts the selected audio output, transitioning MachineLoaded -> Ready.</summary>
        public ChipestraResult StartAudio()
        {
            if (_disposed)
            {
                throw new ObjectDisposedException(nameof(NativeRuntimeSession));
            }

            return NativeMethods.chipestra_start_audio_output(_handle);
        }

        /// <summary>
        /// Stops and releases the audio output, transitioning Ready back to MachineLoaded.
        /// Requires playback to already be stopped (<see cref="StopPlayback"/>) if it was
        /// ever started.
        /// </summary>
        public ChipestraResult StopAudio()
        {
            if (_disposed)
            {
                throw new ObjectDisposedException(nameof(NativeRuntimeSession));
            }

            return NativeMethods.chipestra_stop_audio_output(_handle);
        }

        /// <summary>
        /// Selects WasapiSharedOutput as this session's audio output (MachineLoaded state only).
        /// <paramref name="requestedRateHz"/>: 0 = auto, or 44100/48000/88200/96000/176400/192000.
        /// Shared mode follows the endpoint's own mix rate, so the request is advisory (see
        /// <see cref="AudioOutputFormat.SelectionHonored"/>).
        /// </summary>
        public ChipestraResult SelectWasapiSharedOutput(uint requestedRateHz = 0)
        {
            if (_disposed)
            {
                throw new ObjectDisposedException(nameof(NativeRuntimeSession));
            }

            return NativeMethods.chipestra_select_wasapi_shared_output(_handle, requestedRateHz);
        }

        /// <summary>
        /// The mix format of the selected WASAPI output - or, after a
        /// <see cref="ChipestraResult.UnsupportedFormat"/> selection failure, the format that was rejected.
        /// </summary>
        public ChipestraResult GetAudioOutputFormat(out AudioOutputFormat format)
        {
            format = default;
            if (_disposed)
            {
                throw new ObjectDisposedException(nameof(NativeRuntimeSession));
            }

            return NativeMethods.chipestra_get_audio_output_format(_handle, out format);
        }

        /// <summary>
        /// Starts playback, transitioning Ready -> Playing. From this point on, a background
        /// PlaybackPump thread owns rendering; nothing else calls into the Machine directly.
        /// </summary>
        public ChipestraResult StartPlayback()
        {
            if (_disposed)
            {
                throw new ObjectDisposedException(nameof(NativeRuntimeSession));
            }

            return NativeMethods.chipestra_start_playback(_handle);
        }

        /// <summary>Pauses playback, transitioning Playing -> Paused.</summary>
        public ChipestraResult PausePlayback()
        {
            if (_disposed)
            {
                throw new ObjectDisposedException(nameof(NativeRuntimeSession));
            }

            return NativeMethods.chipestra_pause_playback(_handle);
        }

        /// <summary>Resumes playback, transitioning Paused -> Playing.</summary>
        public ChipestraResult ResumePlayback()
        {
            if (_disposed)
            {
                throw new ObjectDisposedException(nameof(NativeRuntimeSession));
            }

            return NativeMethods.chipestra_resume_playback(_handle);
        }

        /// <summary>Stops playback, transitioning Playing/Paused back to Ready.</summary>
        public ChipestraResult StopPlayback()
        {
            if (_disposed)
            {
                throw new ObjectDisposedException(nameof(NativeRuntimeSession));
            }

            return NativeMethods.chipestra_stop_playback(_handle);
        }

        /// <summary>
        /// Reads the currently selected audio output's diagnostic counters. Valid in the
        /// Ready, Playing, or Paused state (i.e. once an output has been selected/started).
        /// </summary>
        public ChipestraResult GetAudioOutputDiagnostics(out AudioOutputDiagnostics diagnostics)
        {
            diagnostics = default;
            if (_disposed)
            {
                throw new ObjectDisposedException(nameof(NativeRuntimeSession));
            }

            return NativeMethods.chipestra_get_audio_output_diagnostics(_handle, out diagnostics);
        }

        /// <summary>
        /// Number of MachineDefinitions currently registered in the MachineRegistry
        /// (Registry group). Process-wide, not tied to any particular session/instance.
        /// </summary>
        public static uint GetMachineCount()
        {
            return NativeMethods.chipestra_get_machine_count();
        }

        /// <summary>
        /// Reads metadata for the registered MachineDefinition at <paramref name="index"/>
        /// (0-based, in registration order). Process-wide, not tied to any particular
        /// session/instance.
        /// </summary>
        public static ChipestraResult GetMachineInfo(uint index, out MachineInfo info)
        {
            return NativeMethods.chipestra_get_machine_info(index, out info);
        }
    }
}
