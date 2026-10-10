// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using System.Linq;
using System.Windows.Input;
using System.Windows.Threading;
using Chipestra.App.Settings;
using Chipestra.Core;

namespace Chipestra.App.ViewModels
{
    /// <summary>
    /// Top-level UI state: owns the <see cref="Library"/> screen, the current
    /// <see cref="Player"/> (never touched directly by the View), Player Information text, and
    /// the transport (Play/Pause/Resume/Stop) commands.
    ///
    /// UI updates via polling: a <see cref="DispatcherTimer"/> on the UI thread reads a fresh
    /// <see cref="Player.Status"/> snapshot ~30 times/second and refreshes bound properties.
    /// Nothing here is called back from the audio render thread/PlaybackPump worker.
    /// </summary>
    public sealed class MainViewModel : ViewModelBase, IDisposable
    {
        // 30Hz: within the 20-60Hz range and plenty for a time/state readout.
        private static readonly TimeSpan PollInterval = TimeSpan.FromMilliseconds(33);

        private readonly MachineCatalogLookup _machines;
        private readonly DispatcherTimer _pollTimer;

        private Player? _player;
        private string? _loadedGameId;
        private AppSettings _settings;
        private bool _disposed;

        public LibraryViewModel Library { get; }

        /// <summary>Channel Meter panel state - see its own class remarks.</summary>
        public ChannelMixerViewModel ChannelMixer { get; } = new();

        public MainViewModel(LibraryViewModel library, MachineCatalogLookup machines, AppSettings settings)
        {
            Library = library;
            _machines = machines;
            _settings = settings;

            PlayCommand = new RelayCommand(OnPlay, () => CanPlay);
            PauseCommand = new RelayCommand(OnPause, () => CanPause);
            ResumeCommand = new RelayCommand(OnResume, () => CanResume);
            StopCommand = new RelayCommand(OnStop, () => CanStop);

            Library.PropertyChanged += (_, e) =>
            {
                if (e.PropertyName == nameof(ViewModels.LibraryViewModel.SelectedTrack))
                {
                    OnTrackSelected(Library.SelectedTrack);
                }
            };

            _pollTimer = new DispatcherTimer { Interval = PollInterval };
            _pollTimer.Tick += (_, _) => RefreshStatus();
            _pollTimer.Start();
            RefreshStatus();
        }

        // --- Player Information ---

        private string _gameText = "-";
        public string GameText { get => _gameText; private set => SetField(ref _gameText, value); }

        private string _musicText = "-";
        public string MusicText { get => _musicText; private set => SetField(ref _musicText, value); }

        private string _systemText = "-";
        public string SystemText { get => _systemText; private set => SetField(ref _systemText, value); }

        // HH:MM:SS.sss (PlayTimeFormatter), shown large in the Playlist row's "Play Time".
        private string _timeText = "00:00:00.000";
        public string TimeText { get => _timeText; private set => SetField(ref _timeText, value); }

        private string _channelsText = "-";
        public string ChannelsText { get => _channelsText; private set => SetField(ref _channelsText, value); }

        private string _streamingText = "Idle";
        public string StreamingText { get => _streamingText; private set => SetField(ref _streamingText, value); }

        // --- System information. Internal Rate is the Mixer's fixed canonical rate
        // (kNativeRuntimeTickRateHz), a constant rather than something RefreshStatus computes. ---
        public string InternalRateText => "192000 Hz";

        private string _outputFormatText = "-";

        /// <summary>
        /// The audio endpoint actually in use ("48000 Hz / 32bit float / 2ch"), with a note when the
        /// Output Sample Rate setting could not be honored. "-" before a game is loaded or for the
        /// Null output.
        /// </summary>
        public string OutputFormatText { get => _outputFormatText; private set => SetField(ref _outputFormatText, value); }

        private string? _errorMessage;

        /// <summary>User-facing error text - ROM resolution failures, Machine load failures, etc. Empty/null when nothing needs surfacing.</summary>
        public string? ErrorMessage { get => _errorMessage; private set => SetField(ref _errorMessage, value); }

        // --- Transport ---

        public ICommand PlayCommand { get; }
        public ICommand PauseCommand { get; }
        public ICommand ResumeCommand { get; }
        public ICommand StopCommand { get; }

        private bool _canPlay;
        public bool CanPlay { get => _canPlay; private set => SetField(ref _canPlay, value); }

        private bool _canPause;
        public bool CanPause { get => _canPause; private set => SetField(ref _canPause, value); }

        private bool _canResume;
        public bool CanResume { get => _canResume; private set => SetField(ref _canResume, value); }

        private bool _canStop;
        public bool CanStop { get => _canStop; private set => SetField(ref _canStop, value); }

        private void OnPlay()
        {
            if (_player == null)
            {
                return;
            }
            SetResultOrError(_player.Play(), "Play");
        }

        private void OnPause()
        {
            if (_player == null)
            {
                return;
            }
            SetResultOrError(_player.Pause(), "Pause");
        }

        private void OnResume()
        {
            if (_player == null)
            {
                return;
            }
            SetResultOrError(_player.Resume(), "Resume");
        }

        private void OnStop()
        {
            if (_player == null)
            {
                return;
            }
            SetResultOrError(_player.Stop(), "Stop");
        }

        private void SetResultOrError(ChipestraResult result, string action)
        {
            ErrorMessage = result == ChipestraResult.Ok ? null : $"{action} failed: {result}";
            RefreshStatus();
        }

        // --- Track selection -> LoadGame (if needed) + SelectTrack ---

        private void OnTrackSelected(TrackDefinition? track)
        {
            if (track == null || Library.SelectedGame == null)
            {
                return;
            }

            GameDefinition game = Library.SelectedGame;
            if (_player == null || _loadedGameId != game.GameId)
            {
                if (!TryLoadGame(game))
                {
                    return;
                }
            }

            ChipestraResult result = _player!.SelectTrack(track.Id);
            ErrorMessage = result == ChipestraResult.Ok
                ? null
                : result == ChipestraResult.UnsupportedCommand
                    ? $"Cannot play \"{track.DisplayName}\": this game's sound driver does not support this kind of track."
                    : $"Failed to select track: {result}";
            RefreshStatus();
        }

        private bool TryLoadGame(GameDefinition game)
        {
            if (string.IsNullOrWhiteSpace(_settings.RomRoot))
            {
                ErrorMessage = "ROM root is not set. Open Settings to configure it before selecting a track.";
                return false;
            }

            // A fresh Native Runtime instance per loaded game: Player.LoadGame is only valid from the
            // Created state, so switching games starts a new Player.
            _player?.Dispose();
            _player = new Player(Library.Catalog);
            _loadedGameId = null;
            ChannelMixer.Rebind(null);  // clears any channels from the previous game while this one loads

            ChipestraResult loadResult = _player.LoadGame(game.GameId, _settings.RomRoot);
            if (loadResult != ChipestraResult.Ok)
            {
                ErrorMessage = BuildLoadErrorMessage(loadResult, _player.RomResolution);
                RefreshStatus();
                return false;
            }

            OutputFormatText = "-";
            ChipestraResult outputResult = _player.SelectOutput(
                _settings.OutputKind == OutputKind.None ? OutputKind.WasapiShared : _settings.OutputKind,
                OutputRateOptions.Normalize(_settings.OutputRateHz));
            if (outputResult != ChipestraResult.Ok)
            {
                ErrorMessage = OutputMessages.DescribeOutputFailure(outputResult, _player.LastRejectedOutputFormat);
                RefreshStatus();
                return false;
            }

            if (_player.OutputFormat.HasValue)
            {
                OutputFormatText = OutputMessages.DescribeOutput(_player.OutputFormat.Value);
            }

            // The Machine is loaded: channel metadata is valid from MachineLoaded onward, so build
            // the Channel Strip list now.
            ChannelMixer.Rebind(_player);

            _loadedGameId = game.GameId;
            ErrorMessage = null;
            return true;
        }

        private static string BuildLoadErrorMessage(ChipestraResult result, RomResolutionResult? resolution)
        {
            if (resolution != null && !resolution.AllResolved)
            {
                string problems = string.Join("; ", resolution.Entries
                    .Where(e => e.Status != RomResolutionStatus.Resolved)
                    .Select(e => e.Detail != null ? $"{e.Resource.Name}: {e.Status} ({e.Detail})" : $"{e.Resource.Name}: {e.Status}"));
                return $"ROM resolution failed: {problems}";
            }
            return $"Failed to load game: {result}";
        }

        // --- Settings ---

        public AppSettings CurrentSettings => _settings;

        public void ApplySettings(AppSettings settings)
        {
            _settings = settings;
            AppSettingsStore.Save(settings);
        }

        // --- Status polling ---

        private void RefreshStatus()
        {
            // Polled from this 30Hz tick, never pushed from the audio render thread.
            ChannelMixer.RefreshMeters();
            // loops="false" auto-stop is also poll-driven, and called before Status is read so a tick
            // that trips it reports the new Ready state immediately.
            _player?.PollTrackEndBySilence();

            if (_player == null)
            {
                GameText = "-";
                MusicText = "-";
                SystemText = "-";
                TimeText = "00:00:00.000";
                ChannelsText = "-";
                StreamingText = "Idle";
                UpdateTransportEnabled(PlaybackState.Created);
                return;
            }

            PlaybackStatus status = _player.Status;
            GameText = status.Game?.Title ?? "-";
            MusicText = status.Track?.DisplayName ?? "-";
            SystemText = status.Game != null
                ? _machines.Find(status.Game.MachineId)?.DisplayName ?? status.Game.MachineId
                : "-";
            TimeText = PlayTimeFormatter.Format(status.Position);
            ChannelsText = status.ChannelCount?.ToString() ?? "-";
            StreamingText = BuildStreamingText(status);
            UpdateTransportEnabled(status.State);
        }

        private static string OutputDisplayName(OutputKind kind) => kind == OutputKind.WasapiShared ? "WASAPI Shared" : "Null";

        private static string BuildStreamingText(PlaybackStatus status)
        {
            if (status.OutputState == OutputKind.None)
            {
                return "Idle";
            }

            string outputName = OutputDisplayName(status.OutputState);
            string activity = status.State switch
            {
                PlaybackState.Playing => "Streaming",
                PlaybackState.Paused => "Paused",
                _ => "Ready",
            };
            ulong overflow = status.RenderCounters?.QueueOverflowCount ?? 0;
            return overflow > 0 ? $"{outputName} - {activity} (overflow: {overflow})" : $"{outputName} - {activity}";
        }

        private void UpdateTransportEnabled(PlaybackState state)
        {
            CanPlay = state == PlaybackState.Ready;
            CanPause = state == PlaybackState.Playing;
            CanResume = state == PlaybackState.Paused;
            CanStop = state == PlaybackState.Playing || state == PlaybackState.Paused;
            // RelayCommand.CanExecuteChanged is wired to CommandManager.RequerySuggested,
            // which WPF re-raises on UI input but not on a background timer tick by
            // itself - nudge it explicitly so button enabled-state follows the poll,
            // not just mouse/keyboard activity.
            CommandManager.InvalidateRequerySuggested();
        }

        public void Dispose()
        {
            if (_disposed)
            {
                return;
            }
            _pollTimer.Stop();
            _player?.Dispose();
            _disposed = true;
        }
    }
}
