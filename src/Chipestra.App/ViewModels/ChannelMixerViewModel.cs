// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Linq;
using System.Windows.Input;
using Chipestra.Core;

namespace Chipestra.App.ViewModels
{
    /// <summary>
    /// The Channel Meter panel's UI state: the Channel Strip list (up to 56; horizontal
    /// scrolling beyond that is the View's job), bulk operations (ALL MUTE/ALL UNMUTE/ALL
    /// RESET/SOLO OFF), and the master VOLUME/MUTE/L-R meter.
    ///
    /// Owned by <see cref="MainViewModel"/>, which calls <see cref="Rebind"/> whenever its
    /// Player changes and <see cref="RefreshMeters"/> from its 30Hz poll tick (no separate
    /// timer, no audio-thread callback).
    /// </summary>
    public sealed class ChannelMixerViewModel : ViewModelBase
    {
        // The Channel Meter is always shown, even before a Machine is loaded, and renders at
        // least this many strips (more if the loaded Machine has more).
        private const int MinDisplaySlots = 30;

        private Player? _player;

        // Bulk operations set this before looping over Channels and clear it (plus one explicit
        // recompute) after, avoiding an O(n^2) effective-mute recompute per channel.
        private bool _suppressRecompute;

        // Real elapsed time between RefreshMeters calls, for Peak-Hold decay; the poll timer is not
        // hard real-time. A fresh Stopwatch per Rebind avoids a bogus value across a game load.
        private readonly Stopwatch _meterStopwatch = new();
        private TimeSpan _lastMeterTimestamp;

        public ObservableCollection<ChannelStripViewModel> Channels { get; } = new();

        public ICommand AllMuteCommand { get; }
        public ICommand AllUnmuteCommand { get; }
        public ICommand AllResetCommand { get; }
        public ICommand SoloOffCommand { get; }

        private double _masterGain = 1.0;
        public double MasterGain
        {
            get => _masterGain;
            set
            {
                if (SetField(ref _masterGain, value))
                {
                    OnPropertyChanged(nameof(MasterGainPercentText));
                    _player?.SetMasterGain((float)value);
                }
            }
        }

        // MASTER VOLUME readout: Gain 1.0 (unity) reads as 100%.
        public string MasterGainPercentText => $"{(int)Math.Round(_masterGain * 100)}%";

        private bool _masterMuted;
        public bool MasterMuted
        {
            get => _masterMuted;
            set
            {
                if (SetField(ref _masterMuted, value))
                {
                    _player?.SetMasterMuted(value);
                }
            }
        }

        private float _masterPeakLeft;
        public float MasterPeakLeft
        {
            get => _masterPeakLeft;
            private set
            {
                if (SetField(ref _masterPeakLeft, value))
                {
                    OnPropertyChanged(nameof(MasterPeakLeftFillFraction));
                }
            }
        }

        private float _masterPeakRight;
        public float MasterPeakRight
        {
            get => _masterPeakRight;
            private set
            {
                if (SetField(ref _masterPeakRight, value))
                {
                    OnPropertyChanged(nameof(MasterPeakRightFillFraction));
                }
            }
        }

        private float _masterRmsLeft;
        public float MasterRmsLeft { get => _masterRmsLeft; private set => SetField(ref _masterRmsLeft, value); }

        private float _masterRmsRight;
        public float MasterRmsRight { get => _masterRmsRight; private set => SetField(ref _masterRmsRight, value); }

        /// <summary>Master L/R meter, in dB and 0.0-1.0 fill fraction - see <see cref="MeterScale"/>.</summary>
        public double MasterPeakLeftFillFraction => MeterScale.ToFillFraction(MeterScale.LinearToDb(_masterPeakLeft));
        public double MasterPeakRightFillFraction => MeterScale.ToFillFraction(MeterScale.LinearToDb(_masterPeakRight));

        public ChannelMixerViewModel()
        {
            AllMuteCommand = new RelayCommand(() => SetAllMuted(true));
            AllUnmuteCommand = new RelayCommand(() => SetAllMuted(false));
            AllResetCommand = new RelayCommand(OnAllReset);
            SoloOffCommand = new RelayCommand(OnSoloOff);

            // Shown from app startup, before any Machine is loaded; all strips start inactive.
            Rebind(null);
        }

        /// <summary>
        /// Rebuilds <see cref="Channels"/> from <paramref name="player"/>'s loaded Machine, or an
        /// all-inactive placeholder row if null/no Machine is loaded. Always renders at least
        /// <see cref="MinDisplaySlots"/> strips, graying out
        /// (<see cref="ChannelStripViewModel.IsActive"/> = false) the tail that isn't real channels.
        /// </summary>
        public void Rebind(Player? player)
        {
            foreach (ChannelStripViewModel strip in Channels)
            {
                strip.MuteOrSoloChanged -= OnChannelMuteOrSoloChanged;
            }
            Channels.Clear();

            _player = player;
            MasterGainSilent(1.0);
            MasterMutedSilent(false);
            MasterPeakLeft = MasterPeakRight = MasterRmsLeft = MasterRmsRight = 0f;
            _meterStopwatch.Restart();
            _lastMeterTimestamp = TimeSpan.Zero;

            int activeCount = player?.Status.ChannelCount ?? 0;
            int totalSlots = Math.Max(MinDisplaySlots, activeCount);

            // Fetch every active channel's info up front so ChannelDisplayNameFormatter sees all raw
            // strings at once (the device row's "-N" suffix depends on instance counts across the Machine).
            var infos = new ChipestraLogicalChannelInfo?[activeCount];
            for (int i = 0; i < activeCount; i++)
            {
                infos[i] = player!.GetChannelInfo(i);
            }
            var rawNames = new (string DeviceName, string ChannelName)[activeCount];
            for (int i = 0; i < activeCount; i++)
            {
                ChipestraLogicalChannelInfo? info = infos[i];
                rawNames[i] = (info?.DeviceName ?? string.Empty,
                               info?.ChannelName is { Length: > 0 } name ? name : $"Ch {i}");
            }
            IReadOnlyList<(string DeviceDisplay, string ChannelDisplay)> display =
                ChannelDisplayNameFormatter.Format(rawNames);

            _suppressRecompute = true;
            for (int i = 0; i < totalSlots; i++)
            {
                bool isActive = i < activeCount;
                ChipestraLogicalChannelInfo? info = isActive ? infos[i] : null;
                var strip = new ChannelStripViewModel(
                    player, i,
                    isActive ? rawNames[i].ChannelName : "-",
                    isActive ? rawNames[i].DeviceName : "-",
                    isActive ? display[i].ChannelDisplay : "-",
                    isActive ? display[i].DeviceDisplay : "-")
                {
                    IsActive = isActive,
                };
                if (info != null)
                {
                    strip.SeedFrom(info.Value);
                }
                strip.MuteOrSoloChanged += OnChannelMuteOrSoloChanged;
                Channels.Add(strip);
            }
            _suppressRecompute = false;
            RecomputeEffectiveMutes();
        }

        /// <summary>
        /// Refreshes every Channel Strip's meter and Pan, and the master meter, from Native
        /// (poll tick) - never touches Gain/Mute/Solo.
        /// </summary>
        public void RefreshMeters()
        {
            if (_player == null)
            {
                return;
            }

            // Clamped so a debugger pause or a missed tick doesn't hand
            // PeakHoldTracker an implausibly large elapsed value and snap
            // every hold marker straight down to the live level.
            TimeSpan now = _meterStopwatch.Elapsed;
            double elapsedSeconds = Math.Clamp((now - _lastMeterTimestamp).TotalSeconds, 0.0, 1.0);
            _lastMeterTimestamp = now;

            foreach (ChannelStripViewModel strip in Channels)
            {
                if (!strip.IsActive)
                {
                    continue;
                }
                ChipestraLogicalChannelInfo? info = _player.GetChannelInfo(strip.Index);
                if (info != null)
                {
                    strip.UpdateMeter(info.Value.Peak, info.Value.Rms,
                                       info.Value.PeakLeft, info.Value.PeakRight,
                                       info.Value.RmsLeft, info.Value.RmsRight,
                                       elapsedSeconds);
                    strip.RefreshPanFromNative(info.Value.Pan);
                }
            }

            ChipestraMasterState? master = _player.MasterState;
            if (master != null)
            {
                MasterPeakLeft = master.Value.PeakLeft;
                MasterPeakRight = master.Value.PeakRight;
                MasterRmsLeft = master.Value.RmsLeft;
                MasterRmsRight = master.Value.RmsRight;
            }
        }

        private void OnChannelMuteOrSoloChanged(object? sender, EventArgs e)
        {
            if (!_suppressRecompute)
            {
                RecomputeEffectiveMutes();
            }
        }

        /// <summary>
        /// Pushes every channel's *effective* mute to Native: muted directly by the user, or
        /// another channel is soloed and this one isn't. The user's own Mute toggles are untouched.
        /// </summary>
        private void RecomputeEffectiveMutes()
        {
            if (_player == null)
            {
                return;
            }

            bool anySoloed = Channels.Any(c => c.IsActive && c.IsSoloed);
            foreach (ChannelStripViewModel strip in Channels)
            {
                if (!strip.IsActive)
                {
                    continue;
                }
                _player.SetChannelMuted(strip.Index, ComputeEffectiveMute(strip.IsMuted, strip.IsSoloed, anySoloed));
            }
        }

        /// <summary>
        /// A channel is effectively muted if the user muted it directly, or if
        /// any channel (this one included) is soloed and this one isn't -
        /// extracted as a pure function so it can be unit-tested independent of Native/Player.
        /// </summary>
        public static bool ComputeEffectiveMute(bool isMuted, bool isSoloed, bool anySoloed) =>
            isMuted || (anySoloed && !isSoloed);

        private void SetAllMuted(bool muted)
        {
            _suppressRecompute = true;
            foreach (ChannelStripViewModel strip in Channels)
            {
                if (!strip.IsActive)
                {
                    continue;
                }
                strip.IsMuted = muted;
            }
            _suppressRecompute = false;
            RecomputeEffectiveMutes();
        }

        private void OnAllReset()
        {
            _suppressRecompute = true;
            foreach (ChannelStripViewModel strip in Channels)
            {
                if (!strip.IsActive)
                {
                    continue;
                }
                strip.IsMuted = false;
                strip.IsSoloed = false;
                // ALL RESET returns Pan to song-sync mode rather than pinning it to center.
                strip.ClearPanOverride();
                strip.Gain = 1.0;
            }
            _suppressRecompute = false;
            RecomputeEffectiveMutes();
            MasterGain = 1.0;
            MasterMuted = false;
        }

        private void OnSoloOff()
        {
            _suppressRecompute = true;
            foreach (ChannelStripViewModel strip in Channels)
            {
                if (!strip.IsActive)
                {
                    continue;
                }
                strip.IsSoloed = false;
            }
            _suppressRecompute = false;
            RecomputeEffectiveMutes();
        }

        // Rebind's reset restores MasterGain/MasterMuted to their defaults without pushing a
        // SetMasterGain/SetMasterMuted call (the fresh Native Machine already starts at 1.0/unmuted).
        private void MasterGainSilent(double value)
        {
            if (SetField(ref _masterGain, value, nameof(MasterGain)))
            {
                OnPropertyChanged(nameof(MasterGainPercentText));
            }
        }
        private void MasterMutedSilent(bool value) => SetField(ref _masterMuted, value, nameof(MasterMuted));
    }
}
