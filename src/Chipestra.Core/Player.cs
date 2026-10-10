// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using System.Collections.Generic;
using System.Linq;

namespace Chipestra.Core
{
    /// <summary>
    /// High-level Player API: the single place UI code
    /// should drive "pick a game, pick a track, play/pause/resume/stop" from,
    /// instead of assembling <see cref="NativeRuntimeSession"/> calls and raw
    /// command bytes itself. Owns one <see cref="NativeRuntimeSession"/> (and so
    /// one Native Runtime instance) for its lifetime.
    ///
    /// Not thread-safe: like <see cref="NativeRuntimeSession"/> itself, every
    /// member here is meant to be called from one thread (typically the UI
    /// thread) - the PlaybackPump's own background thread is Native-internal and
    /// never touched directly (playback_pump.h).
    /// </summary>
    public sealed class Player : IDisposable
    {
        // Mixer::kCanonicalSampleRateHz / kNativeRuntimeTickRateHz (chipestra_abi.h, mixer.h) -
        // not exposed across the C ABI as a queryable constant, so mirrored here for Position's
        // frame-count -> TimeSpan conversion.
        private const double CanonicalSampleRateHz = 192000.0;

        private readonly IReadOnlyDictionary<string, GameDefinition> _catalog;
        private readonly NativeRuntimeSession _session;

        private PlaybackState _state = PlaybackState.Created;
        private GameDefinition? _game;
        private TrackDefinition? _track;
        private OutputKind _outputKind = OutputKind.None;
        private ulong _positionBaselineFrames;
        private bool _disposed;

        // --- Silence-based auto-stop (loops="false" tracks) ---

        /// <summary>
        /// Linear peak amplitude below which a LogicalChannel is considered
        /// silent for <see cref="PollTrackEndBySilence"/> (0.001 &#8776; -60dBFS by
        /// default). Adjustable per board; the default is a starting point, not a
        /// verified-safe value for every board's residual signal.
        /// </summary>
        public float SilenceThresholdLinear { get; set; } = 0.001f;

        /// <summary>
        /// How long every LogicalChannel must stay continuously below
        /// <see cref="SilenceThresholdLinear"/> before a non-looping track
        /// (<see cref="TrackDefinition.Loops"/> == false) is considered finished
        /// (4.0s default). Adjustable at runtime.
        /// </summary>
        public double SilenceDurationSeconds { get; set; } = 4.0;

        private DateTime? _silenceStreakStartUtc;

        // True once some channel has been above the threshold since the track
        // was (re)started. Silence is only counted after that, so a song with a
        // silent intro is not auto-stopped before its first sound. Cleared by
        // SelectTrack and StopAndRewind (not by Pause/Resume).
        private bool _soundHeardSinceStart;

        /// <param name="catalog">
        /// GameId -> GameDefinition, e.g. from <see cref="GameDefinitionCatalog.LoadDirectory"/>.
        /// Held by reference, not copied - a caller that wants to add games later
        /// can pass a dictionary it keeps mutating.
        /// </param>
        public Player(IReadOnlyDictionary<string, GameDefinition> catalog)
        {
            _catalog = catalog;
            _session = new NativeRuntimeSession();
        }

        public static Player FromDefinitionsDirectory(string definitionsDirectory)
        {
            return new Player(GameDefinitionCatalog.LoadDirectory(definitionsDirectory).Catalog);
        }

        /// <summary>
        /// A fresh snapshot of this Player's current state - see <see cref="Core.PlaybackStatus"/>.
        /// </summary>
        public PlaybackStatus Status
        {
            get
            {
                AudioOutputDiagnostics? diagnostics = null;
                TimeSpan position = TimeSpan.Zero;
                if (_outputKind != OutputKind.None &&
                    _session.GetAudioOutputDiagnostics(out AudioOutputDiagnostics diag) == ChipestraResult.Ok)
                {
                    diagnostics = diag;
                    ulong elapsedFrames =
                        diag.SubmittedFrameCount > _positionBaselineFrames ? diag.SubmittedFrameCount - _positionBaselineFrames : 0UL;
                    position = TimeSpan.FromSeconds(elapsedFrames / CanonicalSampleRateHz);
                }

                return new PlaybackStatus
                {
                    State = _state,
                    Game = _game,
                    Track = _track,
                    Position = position,
                    OutputState = _outputKind,
                    RenderCounters = diagnostics,
                    ChannelCount = ReadChannelCount(),
                };
            }
        }

        /// <summary>
        /// See <see cref="PlaybackStatus.ChannelCount"/>'s remarks: null before
        /// any Machine is loaded, otherwise read fresh from Native every time -
        /// <see cref="NativeRuntimeSession.GetChannelCount"/> is safe to call in
        /// any state that has a Machine loaded, Playing included.
        /// </summary>
        private int? ReadChannelCount()
        {
            if (_state == PlaybackState.Created)
            {
                return null;
            }

            return _session.GetChannelCount(out uint channelCount) == ChipestraResult.Ok ? (int)channelCount : null;
        }

        // --- Channel Meter ---
        //
        // Thin pass-throughs to NativeRuntimeSession's Channel Meter group; Native itself rejects
        // (ChipestraResult.InvalidState) when no Machine is loaded. The getters return null on any
        // failure since a UI poll loop reads them every tick and should not branch per tick.

        /// <summary>
        /// The index-th LogicalChannel's current Channel Strip state (0-based,
        /// &lt; <see cref="PlaybackStatus.ChannelCount"/>), or null if unavailable
        /// (no Machine loaded, or index out of range).
        /// </summary>
        public ChipestraLogicalChannelInfo? GetChannelInfo(int index)
        {
            return _session.GetChannelInfo((uint)index, out ChipestraLogicalChannelInfo info) == ChipestraResult.Ok ? info : null;
        }

        /// <summary>Sets the index-th LogicalChannel's Mute flag (the Channel Strip's M button).</summary>
        public ChipestraResult SetChannelMuted(int index, bool muted) => _session.SetChannelMuted((uint)index, muted);

        /// <summary>Sets the index-th LogicalChannel's Pan (the Channel Strip's pan knob).</summary>
        public ChipestraResult SetChannelPan(int index, float pan) => _session.SetChannelPan((uint)index, pan);

        /// <summary>Returns the index-th LogicalChannel's Pan to song-sync mode (the Channel Strip's ALL RESET).</summary>
        public ChipestraResult ClearChannelPanOverride(int index) => _session.ClearChannelPanOverride((uint)index);

        /// <summary>Sets the index-th LogicalChannel's Gain (the Channel Strip's LEVEL fader).</summary>
        public ChipestraResult SetChannelGain(int index, float gain) => _session.SetChannelGain((uint)index, gain);

        /// <summary>
        /// The master VOLUME/MUTE/meter state, or null if unavailable
        /// (no Machine loaded).
        /// </summary>
        public ChipestraMasterState? MasterState =>
            _session.GetMasterState(out ChipestraMasterState state) == ChipestraResult.Ok ? state : null;

        /// <summary>Sets the master VOLUME slider's gain.</summary>
        public ChipestraResult SetMasterGain(float gain) => _session.SetMasterGain(gain);

        /// <summary>Sets the master MUTE button.</summary>
        public ChipestraResult SetMasterMuted(bool muted) => _session.SetMasterMuted(muted);

        /// <summary>
        /// Loads a game by id: loads its declared Machine/Variant via the MachineRegistry - with,
        /// if <paramref name="romRoot"/> is given, its declared ROM resources resolved and wired
        /// into the Machine.
        ///
        /// Returns <see cref="ChipestraResult.NotFound"/> if gameId isn't in the catalog.
        /// If <paramref name="romRoot"/> is given and any of the game's declared
        /// resources fails to resolve (missing/ambiguous/hash-mismatched), the Machine is NOT
        /// loaded and the first such failure's <see cref="ChipestraResult"/> is returned - inspect
        /// <see cref="RomResolution"/> for the full per-resource detail. If
        /// <paramref name="romRoot"/> is omitted, the Machine loads with no ROM data
        /// (one that genuinely needs none, e.g. TEST_MACHINE). Otherwise returns whatever
        /// <see cref="NativeRuntimeSession.LoadMachine"/>/<see cref="NativeRuntimeSession.LoadMachineWithResources"/>
        /// returned (e.g. the XML's machine/variant isn't registered). Valid from
        /// <see cref="PlaybackState.Created"/> only.
        /// </summary>
        public ChipestraResult LoadGame(string gameId, string? romRoot = null)
        {
            if (!_catalog.TryGetValue(gameId, out GameDefinition? game))
            {
                return ChipestraResult.NotFound;
            }

            ChipestraResult loadResult;
            if (romRoot != null)
            {
                loadResult = _session.LoadMachineWithResources(
                    game.MachineId, game.VariantId, game.RequiredResources, romRoot, out RomResolutionResult resolution,
                    game.Archive);
                RomResolution = resolution;
            }
            else
            {
                loadResult = _session.LoadMachine(game.MachineId, game.VariantId);
                RomResolution = null;
            }

            if (loadResult != ChipestraResult.Ok)
            {
                return loadResult;
            }

            _game = game;
            _track = null;
            _state = PlaybackState.MachineLoaded;
            return ChipestraResult.Ok;
        }

        /// <summary>
        /// The most recent <see cref="LoadGame"/> call's ROM resolution, if a
        /// romRoot was given - populated whether or not resolution fully succeeded.
        /// Null otherwise (including: no game loaded yet, or the
        /// most recent LoadGame call omitted romRoot).
        /// </summary>
        public RomResolutionResult? RomResolution { get; private set; }

        /// <summary>
        /// The selected WASAPI output's mix format (actual rate, bit depth, ...); null for the
        /// Null output or before a successful <see cref="SelectOutput"/>.
        /// </summary>
        public AudioOutputFormat? OutputFormat { get; private set; }

        /// <summary>
        /// After <see cref="SelectOutput"/> failed with <see cref="ChipestraResult.UnsupportedFormat"/>:
        /// the endpoint format that was rejected (rate, bit depth, sample format). Null otherwise.
        /// </summary>
        public AudioOutputFormat? LastRejectedOutputFormat { get; private set; }

        /// <summary>
        /// Selects and starts an audio output, transitioning
        /// MachineLoaded -&gt; Ready. Must be called (with either output kind, even
        /// in tests that never intend to hear anything - <see cref="OutputKind.Null"/>
        /// exists for exactly that) before <see cref="Play"/>.
        /// </summary>
        public ChipestraResult SelectOutput(OutputKind kind, uint requestedRateHz = 0)
        {
            OutputFormat = null;
            LastRejectedOutputFormat = null;
            ChipestraResult selectResult = kind switch
            {
                OutputKind.Null => _session.SelectNullOutput(),
                OutputKind.WasapiShared => _session.SelectWasapiSharedOutput(requestedRateHz),
                _ => ChipestraResult.InvalidArgument,
            };
            if (selectResult == ChipestraResult.UnsupportedFormat &&
                _session.GetAudioOutputFormat(out AudioOutputFormat rejected) == ChipestraResult.Ok)
            {
                LastRejectedOutputFormat = rejected;
            }

            if (selectResult != ChipestraResult.Ok)
            {
                return selectResult;
            }

            if (kind == OutputKind.WasapiShared &&
                _session.GetAudioOutputFormat(out AudioOutputFormat selected) == ChipestraResult.Ok)
            {
                OutputFormat = selected;
            }

            ChipestraResult startResult = _session.StartAudio();
            if (startResult != ChipestraResult.Ok)
            {
                return startResult;
            }

            _outputKind = kind;
            _state = PlaybackState.Ready;
            return ChipestraResult.Ok;
        }

        /// <summary>
        /// Selects a track by id and starts it playing immediately: looks the track up in
        /// the current game's <see cref="GameDefinition.Tracks"/>, sends its raw
        /// command byte (<see cref="MusicCommandKind.Raw"/>) to the Machine -
        /// callers never touch a command hex value directly - and always leaves
        /// this Player in <see cref="PlaybackState.Playing"/> afterward, regardless
        /// of whether it was already Playing, Paused, or Ready (Stopped) beforehand.
        ///
        /// If already Playing (a track switch mid-playback), this stops first, sends
        /// the new track's command, then restarts - <see cref="NativeRuntimeSession.SendCommand"/>
        /// can only be delivered outside of Playing (the PlaybackPump thread owns the
        /// Machine while it runs; see its own remarks). If Ready or Paused, the
        /// command is sent directly (both states accept it) and playback is then
        /// started/resumed - clicking a track in the Library is meant to play it,
        /// not just queue it up silently.
        ///
        /// Returns <see cref="ChipestraResult.InvalidState"/> if no game is loaded yet.
        /// Returns <see cref="ChipestraResult.NotFound"/> if trackId isn't one of the
        /// current game's tracks. If the stop/send/(re)start sequence fails partway
        /// (e.g. the Machine was destroyed concurrently, or no output has been
        /// selected yet - <see cref="SelectOutput"/> must run first), the intermediate
        /// <see cref="ChipestraResult"/> is returned and playback is left wherever that
        /// step left it - <see cref="Status"/> always reflects the true state.
        /// </summary>
        public ChipestraResult SelectTrack(string trackId)
        {
            if (_game == null)
            {
                return ChipestraResult.InvalidState;
            }

            TrackDefinition? track = _game.Tracks.FirstOrDefault(t => t.Id == trackId);
            if (track == null)
            {
                return ChipestraResult.NotFound;
            }

            PlaybackState stateBeforeSend = _state;
            if (stateBeforeSend == PlaybackState.Playing)
            {
                ChipestraResult stopResult = _session.StopPlayback();
                if (stopResult != ChipestraResult.Ok)
                {
                    return stopResult;
                }
                _state = PlaybackState.Ready;
            }

            ChipestraResult sendResult = _session.SendCommand(track.CommandKind, track.Command);
            if (sendResult != ChipestraResult.Ok)
            {
                return sendResult;
            }
            _track = track;
            ResetPositionBaseline();
            _silenceStreakStartUtc = null;  // fresh track: any earlier silence streak no longer applies
            _soundHeardSinceStart = false;

            // Control-command tracks (e.g. STOP) are not real songs: Command was still sent
            // above (hardware side effects are kept) but the Player stops short of Playing,
            // so Play Time never advances. From Paused, StopPlayback() finishes the
            // transition back to Ready (SendCommand alone does not change state).
            if (track.IsControlCommand)
            {
                if (_state == PlaybackState.Paused)
                {
                    ChipestraResult stopFromPausedResult = _session.StopPlayback();
                    if (stopFromPausedResult != ChipestraResult.Ok)
                    {
                        return stopFromPausedResult;
                    }
                }
                _state = PlaybackState.Ready;
                return ChipestraResult.Ok;
            }

            // Always ends Playing: after the stop above, a session that was Playing
            // session is now Ready, same as one that was already Ready (never
            // played, or explicitly Stopped) - both resume via StartPlayback().
            // A Paused session stays Paused across SendCommand (native allows it
            // there too) and resumes via ResumePlayback() instead, since
            // chipestra_start_playback only accepts the Ready state.
            ChipestraResult playResult = stateBeforeSend == PlaybackState.Paused
                ? _session.ResumePlayback()
                : _session.StartPlayback();
            if (playResult != ChipestraResult.Ok)
            {
                return playResult;
            }
            _state = PlaybackState.Playing;

            return ChipestraResult.Ok;
        }

        /// <summary>
        /// Auto-stops a non-looping track once every LogicalChannel has been
        /// silent for <see cref="SilenceDurationSeconds"/> - a Music Player
        /// convenience for one-shot tracks (e.g. Metal Black's Staff Roll), not a
        /// hardware concept. A no-op
        /// unless currently Playing a track with <see cref="TrackDefinition.Loops"/>
        /// == false - every looping track (the default, and every existing
        /// track XML) is never affected, by construction. Meant to be called from
        /// the same UI poll tick that already reads <see cref="Status"/>
        /// (MainViewModel.RefreshStatus), before
        /// reading Status, so a tick that trips this also reports the new Ready
        /// state immediately rather than one frame late.
        /// </summary>
        public void PollTrackEndBySilence()
        {
            if (_state != PlaybackState.Playing || _track == null || _track.Loops)
            {
                _silenceStreakStartUtc = null;
                return;
            }

            if (!IsAllChannelsSilent())
            {
                _soundHeardSinceStart = true;
                _silenceStreakStartUtc = null;
                return;
            }

            if (!_soundHeardSinceStart)
            {
                // Still in the intro (or the song never sounds): not counted.
                _silenceStreakStartUtc = null;
                return;
            }

            DateTime now = DateTime.UtcNow;
            _silenceStreakStartUtc ??= now;
            if ((now - _silenceStreakStartUtc.Value).TotalSeconds < SilenceDurationSeconds)
            {
                return;
            }

            // Goes through StopAndRewind() like Stop(), not a bare StopPlayback(), so a later
            // Play() genuinely restarts the song instead of resuming from the silent post-song
            // state. Its result is not surfaced (background poll): on failure _state is left
            // untouched and the next tick tries again.
            StopAndRewind();
        }

        /// <summary>
        /// True only if every LogicalChannel's most recent SourcePeak (the sound
        /// before the user's Mute/Gain/Pan and the master volume, so silencing
        /// a channel never looks like the song ending) is below <see cref="SilenceThresholdLinear"/>. Fails safe (returns false,
        /// never auto-stopping) if channel data can't be read at all, rather than
        /// treating "no data" as "silent".
        /// </summary>
        private bool IsAllChannelsSilent()
        {
            int? channelCount = ReadChannelCount();
            if (channelCount is not > 0)
            {
                return false;
            }

            for (int i = 0; i < channelCount.Value; i++)
            {
                ChipestraLogicalChannelInfo? info = GetChannelInfo(i);
                if (info == null)
                {
                    return false;
                }
                if (info.Value.SourcePeak >= SilenceThresholdLinear)
                {
                    return false;
                }
            }
            return true;
        }

        /// <summary>Starts playback, transitioning Ready -&gt; Playing.</summary>
        public ChipestraResult Play()
        {
            ChipestraResult result = _session.StartPlayback();
            if (result == ChipestraResult.Ok)
            {
                _state = PlaybackState.Playing;
            }
            return result;
        }

        /// <summary>Pauses playback, transitioning Playing -&gt; Paused.</summary>
        public ChipestraResult Pause()
        {
            ChipestraResult result = _session.PausePlayback();
            if (result == ChipestraResult.Ok)
            {
                _state = PlaybackState.Paused;
            }
            return result;
        }

        /// <summary>Resumes playback, transitioning Paused -&gt; Playing.</summary>
        public ChipestraResult Resume()
        {
            ChipestraResult result = _session.ResumePlayback();
            if (result == ChipestraResult.Ok)
            {
                _state = PlaybackState.Playing;
            }
            return result;
        }

        /// <summary>
        /// Stops playback and rewinds to the beginning, transitioning
        /// Playing/Paused back to Ready - distinct from <see cref="Pause"/>,
        /// which freezes the Machine's own internal driver state so
        /// <see cref="Resume"/> can continue exactly where it left off.
        /// Native's chipestra_stop_playback only tears down the PlaybackPump - it
        /// never resets the Machine's internal driver state or resends a
        /// command on its own - without the rewind below, a later <see cref="Play"/>
        /// would silently resume from wherever rendering was frozen, which is
        /// indistinguishable from Pause/Resume. See <see cref="StopAndRewind"/>.
        /// </summary>
        public ChipestraResult Stop() => StopAndRewind();

        /// <summary>
        /// Shared by <see cref="Stop"/> and <see cref="PollTrackEndBySilence"/>
        /// (both are "this track is done, go back to a from-the-top-ready
        /// state" - the only difference is who triggered it): stops the
        /// PlaybackPump, then - for a real (non-control-command) selected
        /// track - resends its command while Ready (the same one
        /// <see cref="SelectTrack"/> sends, and just as valid there per
        /// chipestra_send_command's own state rules) so the Machine's internal
        /// driver genuinely restarts the song from the top instead of staying
        /// wherever it was frozen. Position is reset to zero regardless (even
        /// for a control-command track or no track at all), so Play Time
        /// visibly reflects "stopped, at the beginning" immediately, not just
        /// once <see cref="Play"/> is next pressed.
        /// </summary>
        private ChipestraResult StopAndRewind()
        {
            _silenceStreakStartUtc = null;
            _soundHeardSinceStart = false;

            ChipestraResult result = _session.StopPlayback();
            if (result != ChipestraResult.Ok)
            {
                return result;
            }
            _state = PlaybackState.Ready;
            ResetPositionBaseline();

            if (_track != null && !_track.IsControlCommand)
            {
                // Best-effort: a resend failure does not turn an otherwise-successful Stop
                // into a reported failure; Status.State already reflects Ready.
                _session.SendCommand(_track.CommandKind, _track.Command);
            }

            return ChipestraResult.Ok;
        }

        private void ResetPositionBaseline()
        {
            _positionBaselineFrames =
                _session.GetAudioOutputDiagnostics(out AudioOutputDiagnostics diagnostics) == ChipestraResult.Ok
                    ? diagnostics.SubmittedFrameCount
                    : 0UL;
        }

        public void Dispose()
        {
            if (_disposed)
            {
                return;
            }
            _session.Dispose();
            _disposed = true;
        }
    }
}
