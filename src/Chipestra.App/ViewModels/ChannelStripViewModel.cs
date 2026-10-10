// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using Chipestra.Core;

namespace Chipestra.App.ViewModels
{
    /// <summary>
    /// One Channel Strip's UI state: LEVEL fader (Gain), pan knob, mute/solo buttons, name
    /// label, and the LEVEL meter (Peak/RMS).
    ///
    /// <see cref="Gain"/>/<see cref="IsMuted"/>/<see cref="IsSoloed"/> are this ViewModel's own
    /// authoritative state - set once from Native at <see cref="SeedFrom"/> time, then only
    /// written back to Native by this class's setters, never re-read. Native's Mute flag holds
    /// the *effective* (possibly solo-forced) mute that <see cref="ChannelMixerViewModel"/>
    /// pushes, which is not the channel's own user-facing Mute toggle.
    /// <see cref="Peak"/>/<see cref="Rms"/> and <see cref="Pan"/> are refreshed from Native every
    /// poll tick: Peak/Rms change on their own with the render thread, and some boards program
    /// Pan live from the sound driver unless the UI has overridden it
    /// (<see cref="Pan"/>'s setter, via <see cref="Player.SetChannelPan"/>). Native holds whichever
    /// is in effect, so re-reading it every tick is always correct.
    /// </summary>
    public sealed class ChannelStripViewModel : ViewModelBase
    {
        // Null for an inactive placeholder slot (before a Machine is loaded, or past the loaded
        // Machine's real channel count); the setters guard every Native call accordingly.
        private readonly Player? _player;

        // Default elapsed time for direct/test callers; ChannelMixerViewModel measures the real
        // time and passes it into UpdateMeter (the app polls at ~30Hz).
        private const double DefaultElapsedSeconds = 1.0 / 30.0;

        private double _pan;
        private double _gain = 1.0;
        private bool _isMuted;
        private bool _isSoloed;
        private float _peak;
        private float _rms;
        private float _peakLeft;
        private float _peakRight;
        private float _rmsLeft;
        private float _rmsRight;
        private readonly PeakHoldTracker _holdLeft = new();
        private readonly PeakHoldTracker _holdRight = new();

        public ChannelStripViewModel(Player? player, int index, string channelName, string deviceName,
                                      string? displayChannelName = null, string? displayDeviceName = null)
        {
            _player = player;
            Index = index;
            ChannelName = channelName;
            DeviceName = deviceName;
            DisplayChannelName = displayChannelName ?? channelName;
            DisplayDeviceName = displayDeviceName ?? deviceName;
        }

        /// <summary>0-based LogicalChannel index (the Channel Meter addresses channels this way).</summary>
        public int Index { get; }

        /// <summary>Raw Native ChannelName, unchanged - used for the automation names below.</summary>
        public string ChannelName { get; }

        /// <summary>Raw Native DeviceName, unchanged.</summary>
        public string DeviceName { get; }

        /// <summary>
        /// Channel Strip's ch-element row text ("category Nch", e.g. "FM 1ch") - see
        /// <see cref="ChannelDisplayNameFormatter"/>. Defaults to the raw <see cref="ChannelName"/>
        /// when not supplied.
        /// </summary>
        public string DisplayChannelName { get; }

        /// <summary>
        /// Channel Strip's device-name row text (the chip model, with a "-N" suffix only when that
        /// model has more than one instance in this Machine) - see <see cref="ChannelDisplayNameFormatter"/>.
        /// Defaults to the raw <see cref="DeviceName"/> when not supplied.
        /// </summary>
        public string DisplayDeviceName { get; }

        private bool _isActive = true;

        /// <summary>
        /// False for a placeholder slot beyond the loaded Machine's real channel count, or before
        /// any Machine is loaded. The View grays these out and disables their controls via this flag
        /// instead of showing/hiding rows, so the Channel Meter's width never jumps.
        /// </summary>
        public bool IsActive { get => _isActive; set => SetField(ref _isActive, value); }

        // Distinct AutomationProperties.Name text per control, so UI Automation sees e.g.
        // "Channel 0 Mute" rather than a repeated name or the type name.
        public string MuteAutomationName => $"{ChannelName} Mute";
        public string SoloAutomationName => $"{ChannelName} Solo";
        public string PanAutomationName => $"{ChannelName} Pan";
        public string GainAutomationName => $"{ChannelName} Level";

        /// <summary>-1 (left) to +1 (right), 0 = center - pushed to Native on every change.</summary>
        public double Pan
        {
            get => _pan;
            set
            {
                double clamped = Math.Clamp(value, -1.0, 1.0);
                if (SetField(ref _pan, clamped))
                {
                    _player?.SetChannelPan(Index, (float)clamped);
                    OnPropertyChanged(nameof(PanAngleDegrees));
                    OnPropertyChanged(nameof(PanLabelText));
                }
            }
        }

        /// <summary>
        /// Returns this channel to song-sync mode (ALL RESET): clears the Native-side override so a
        /// board that drives Pan live from the sound driver resumes tracking it. Sets the local value
        /// to center as an interim readout; the next poll tick's <see cref="RefreshPanFromNative"/>
        /// picks up whatever Native actually holds.
        /// </summary>
        public void ClearPanOverride()
        {
            _player?.ClearChannelPanOverride(Index);
            if (SetField(ref _pan, 0.0, nameof(Pan)))
            {
                OnPropertyChanged(nameof(PanAngleDegrees));
                OnPropertyChanged(nameof(PanLabelText));
            }
        }

        /// <summary>
        /// Refreshes <see cref="Pan"/> from Native (poll loop, every tick) -
        /// bypasses the public <see cref="Pan"/> setter so this never pushes
        /// back to Native or re-triggers an override (see this class's own
        /// remarks on why re-reading Pan is always safe).
        /// </summary>
        public void RefreshPanFromNative(float pan)
        {
            if (SetField(ref _pan, pan, nameof(Pan)))
            {
                OnPropertyChanged(nameof(PanAngleDegrees));
                OnPropertyChanged(nameof(PanLabelText));
            }
        }

        /// <summary>
        /// <see cref="Pan"/> as a knob-needle rotation angle (Center=0, +-100% = +-100 degrees,
        /// linear 1% = 1 degree - see <see cref="PanKnobMath"/>).
        /// </summary>
        public double PanAngleDegrees => PanKnobMath.ToAngleDegrees(_pan);

        /// <summary>
        /// L/R/C label shown under the pan knob: "C" at dead center, otherwise "L"/"R" with the
        /// 0-99 percent magnitude appended (e.g. "L90"); the percent is omitted at exactly 100%.
        ///
        /// The center check uses the rounded percent, not `_pan == 0.0`: mouse-wheel steps of 0.01
        /// leave tiny nonzero residuals (e.g. -1e-17) that would otherwise print "L0" or "R0".
        /// </summary>
        public string PanLabelText
        {
            get
            {
                int percent = (int)Math.Round(Math.Abs(_pan) * 100.0, MidpointRounding.AwayFromZero);
                if (percent == 0)
                {
                    return "C";
                }
                string side = _pan < 0.0 ? "L" : "R";
                return percent >= 100 ? side : $"{side}{percent}";
            }
        }

        /// <summary>Linear multiplier, 1.0 = unity (the LEVEL fader) - pushed to Native on every change.</summary>
        public double Gain
        {
            get => _gain;
            set
            {
                if (SetField(ref _gain, value))
                {
                    _player?.SetChannelGain(Index, (float)value);
                }
            }
        }

        /// <summary>
        /// The user-facing Mute toggle. Not pushed to Native directly - raises
        /// <see cref="MuteOrSoloChanged"/> so <see cref="ChannelMixerViewModel"/>
        /// can recompute every channel's *effective* mute (solo interacts, see
        /// this class's own remarks) and push that instead.
        /// </summary>
        public bool IsMuted
        {
            get => _isMuted;
            set
            {
                if (SetField(ref _isMuted, value))
                {
                    MuteOrSoloChanged?.Invoke(this, EventArgs.Empty);
                }
            }
        }

        /// <summary>The user-facing Solo toggle - see <see cref="IsMuted"/>'s remarks.</summary>
        public bool IsSoloed
        {
            get => _isSoloed;
            set
            {
                if (SetField(ref _isSoloed, value))
                {
                    MuteOrSoloChanged?.Invoke(this, EventArgs.Empty);
                }
            }
        }

        /// <summary>Raised when <see cref="IsMuted"/> or <see cref="IsSoloed"/> changes.</summary>
        public event EventHandler? MuteOrSoloChanged;

        /// <summary>Most recent block's peak absolute sample value (linear), refreshed by the poll loop.</summary>
        public float Peak
        {
            get => _peak;
            private set => SetField(ref _peak, value);
        }

        /// <summary>Most recent block's RMS level (linear), refreshed by the poll loop.</summary>
        public float Rms
        {
            get => _rms;
            private set => SetField(ref _rms, value);
        }

        /// <summary>Peak, in dB - see <see cref="MeterScale"/>.</summary>
        public double PeakDb => MeterScale.LinearToDb(_peak);

        /// <summary><see cref="PeakDb"/> remapped to 0.0-1.0 - what the meter bar's fill actually binds to.</summary>
        public double MeterFillFraction => MeterScale.ToFillFraction(PeakDb);

        // True per-channel L/R meter, Peak Hold and zone state. Peak/Rms/PeakDb/MeterFillFraction
        // above stay mono.

        /// <summary>Most recent block's left-channel peak (linear), refreshed by the poll loop.</summary>
        public float PeakLeft { get => _peakLeft; private set => SetField(ref _peakLeft, value); }

        /// <summary>Most recent block's right-channel peak (linear), refreshed by the poll loop.</summary>
        public float PeakRight { get => _peakRight; private set => SetField(ref _peakRight, value); }

        /// <summary>Most recent block's left-channel RMS (linear), refreshed by the poll loop.</summary>
        public float RmsLeft { get => _rmsLeft; private set => SetField(ref _rmsLeft, value); }

        /// <summary>Most recent block's right-channel RMS (linear), refreshed by the poll loop.</summary>
        public float RmsRight { get => _rmsRight; private set => SetField(ref _rmsRight, value); }

        public double PeakLeftDb => MeterScale.LinearToDb(_peakLeft);
        public double PeakRightDb => MeterScale.LinearToDb(_peakRight);
        public double MeterFillFractionLeft => MeterScale.ToFillFraction(PeakLeftDb);
        public double MeterFillFractionRight => MeterScale.ToFillFraction(PeakRightDb);

        /// <summary>The held (decaying) peak-hold marker's fill fraction - see <see cref="PeakHoldTracker"/>.</summary>
        public double HoldFillFractionLeft => MeterScale.ToFillFraction(MeterScale.LinearToDb((float)_holdLeft.Value));
        public double HoldFillFractionRight => MeterScale.ToFillFraction(MeterScale.LinearToDb((float)_holdRight.Value));

        /// <summary>
        /// Which color zone the peak-hold marker sits in (Green/Yellow/Red, matching the live bar's
        /// 3-zone coloring at that position) - bound via a DataTrigger, since the hold marker is a thin
        /// line drawn on top of the bar.
        /// </summary>
        public MeterZone HoldZoneLeft => MeterScale.GetZone(MeterScale.LinearToDb((float)_holdLeft.Value));
        public MeterZone HoldZoneRight => MeterScale.GetZone(MeterScale.LinearToDb((float)_holdRight.Value));

        private static readonly string[] MeterDerivedPropertyNames =
        {
            nameof(PeakDb), nameof(MeterFillFraction),
            nameof(PeakLeftDb), nameof(PeakRightDb),
            nameof(MeterFillFractionLeft), nameof(MeterFillFractionRight),
            nameof(HoldFillFractionLeft), nameof(HoldFillFractionRight),
            nameof(HoldZoneLeft), nameof(HoldZoneRight),
        };

        private void RaiseMeterDerivedPropertiesChanged()
        {
            foreach (string name in MeterDerivedPropertyNames)
            {
                OnPropertyChanged(name);
            }
        }

        /// <summary>
        /// One-time initial sync from Native (<see cref="ChannelMixerViewModel"/>,
        /// when the channel list is (re)built) - sets every field directly,
        /// bypassing the property setters above so this doesn't push anything
        /// back to Native (it's already Native's own current state).
        /// </summary>
        public void SeedFrom(ChipestraLogicalChannelInfo info)
        {
            SetField(ref _pan, info.Pan, nameof(Pan));
            SetField(ref _gain, info.Gain, nameof(Gain));
            SetField(ref _isMuted, info.Muted != 0, nameof(IsMuted));
            SetField(ref _peak, info.Peak, nameof(Peak));
            SetField(ref _rms, info.Rms, nameof(Rms));
            SetField(ref _peakLeft, info.PeakLeft, nameof(PeakLeft));
            SetField(ref _peakRight, info.PeakRight, nameof(PeakRight));
            SetField(ref _rmsLeft, info.RmsLeft, nameof(RmsLeft));
            SetField(ref _rmsRight, info.RmsRight, nameof(RmsRight));
            _holdLeft.Reset();
            _holdRight.Reset();
            OnPropertyChanged(nameof(PanAngleDegrees));
            OnPropertyChanged(nameof(PanLabelText));
            RaiseMeterDerivedPropertiesChanged();
        }

        /// <summary>
        /// Refreshes only the meter (poll loop, every tick) - never touches
        /// Pan/Gain/Mute/Solo. <paramref name="elapsedSeconds"/> drives the
        /// Peak Hold decay (<see cref="PeakHoldTracker"/>) - callers that
        /// don't have a real elapsed-time measurement (e.g. tests calling
        /// this directly) can omit it and get the app's nominal poll rate.
        /// </summary>
        public void UpdateMeter(float peak, float rms, float peakLeft = 0f, float peakRight = 0f,
                                 float rmsLeft = 0f, float rmsRight = 0f, double elapsedSeconds = DefaultElapsedSeconds)
        {
            Peak = peak;
            Rms = rms;
            PeakLeft = peakLeft;
            PeakRight = peakRight;
            RmsLeft = rmsLeft;
            RmsRight = rmsRight;
            _holdLeft.Update(peakLeft, elapsedSeconds);
            _holdRight.Update(peakRight, elapsedSeconds);
            RaiseMeterDerivedPropertiesChanged();
        }

        public override string ToString() => ChannelName;
    }
}
