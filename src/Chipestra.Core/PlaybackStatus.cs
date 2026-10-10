// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;

namespace Chipestra.Core
{
    /// <summary>
    /// A snapshot of <see cref="Player"/>'s current state. Built fresh on every read of
    /// <see cref="Player.Status"/> - never cached - including the render counters (from
    /// <see cref="NativeRuntimeSession.GetAudioOutputDiagnostics"/>).
    /// </summary>
    public sealed class PlaybackStatus
    {
        public required PlaybackState State { get; init; }

        /// <summary>The currently loaded game, or null before the first successful <see cref="Player.LoadGame"/>.</summary>
        public GameDefinition? Game { get; init; }

        /// <summary>The currently selected track, or null before the first successful <see cref="Player.SelectTrack"/>.</summary>
        public TrackDefinition? Track { get; init; }

        /// <summary>
        /// Elapsed playback time since <see cref="Track"/> was selected (or since
        /// <see cref="Player.Play"/> if no explicit track selection preceded it).
        /// Derived from <see cref="RenderCounters"/>.SubmittedFrameCount at the
        /// canonical 192kHz rate (Mixer::kCanonicalSampleRateHz, chipestra_abi.h) - the
        /// PlaybackPump feeds that rate in real time, so this is real
        /// elapsed time, not an estimate. Frozen (not advancing) while
        /// <see cref="State"/> is Paused, since SubmittedFrameCount itself stops
        /// advancing then. Zero until an output has been selected.
        /// </summary>
        public TimeSpan Position { get; init; }

        public OutputKind OutputState { get; init; }

        /// <summary>Null until an output has been selected (<see cref="OutputState"/> == None).</summary>
        public AudioOutputDiagnostics? RenderCounters { get; init; }

        /// <summary>
        /// The currently loaded Machine's LogicalChannel count - null before any Machine has been
        /// loaded, otherwise read fresh from <see cref="NativeRuntimeSession.GetChannelCount"/> on
        /// every snapshot (safe in any state that has a Machine loaded, Playing included).
        /// </summary>
        public int? ChannelCount { get; init; }
    }
}
