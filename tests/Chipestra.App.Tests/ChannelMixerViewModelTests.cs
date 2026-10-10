// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using Chipestra.App.ViewModels;
using Chipestra.Core;
using Xunit;

namespace Chipestra.App.Tests
{
    // ChannelMixerViewModel/ChannelStripViewModel: the Channel Meter panel's UI state.
    // Runs against TEST_MACHINE (1 channel, no ROM or real audio device needed), plus
    // pure-logic tests for the solo/mute interaction (ComputeEffectiveMute).
    public class ChannelMixerViewModelTests
    {
        private static IReadOnlyDictionary<string, GameDefinition> TestCatalog()
        {
            var game = new GameDefinition
            {
                GameId = "TEST_GAME",
                Title = "Test Game",
                MachineId = "TEST_MACHINE",
                VariantId = "STANDARD",
                RequiredResources = Array.Empty<RomResource>(),
                Tracks = new List<TrackDefinition>
                {
                    new TrackDefinition { Id = "T1", DisplayName = "Test Track 1", Command = 1 },
                },
            };
            return new Dictionary<string, GameDefinition>(StringComparer.Ordinal) { { "TEST_GAME", game } };
        }

        private static bool WaitForSubmittedFrames(Player player, ulong minFrames, int timeoutMs)
        {
            const int pollIntervalMs = 5;
            for (int waited = 0; waited <= timeoutMs; waited += pollIntervalMs)
            {
                if ((player.Status.RenderCounters?.SubmittedFrameCount ?? 0) >= minFrames)
                {
                    return true;
                }
                Thread.Sleep(pollIntervalMs);
            }
            return false;
        }

        // --- ComputeEffectiveMute: pure solo/mute interaction logic ---

        [Theory]
        [InlineData(false, false, false, false)]  // nothing muted/soloed anywhere -> not muted
        [InlineData(true, false, false, true)]    // muted directly -> muted, regardless of solo
        [InlineData(false, false, true, true)]    // another channel is soloed, this one isn't -> forced muted
        [InlineData(false, true, true, false)]    // this channel is the soloed one -> not muted
        [InlineData(true, true, true, true)]      // muted directly AND soloed -> still muted (direct mute wins)
        public void ComputeEffectiveMute_MatchesExpectedSoloMuteInteraction(
            bool isMuted, bool isSoloed, bool anySoloed, bool expected)
        {
            Assert.Equal(expected, ChannelMixerViewModel.ComputeEffectiveMute(isMuted, isSoloed, anySoloed));
        }

        // --- ChannelStripViewModel: meter scaling ---

        [Fact]
        public void MeterFillFraction_FullScalePeak_IsOne()
        {
            using var player = new Player(TestCatalog());
            var strip = new ChannelStripViewModel(player, 0, "Channel 0", "TEST_MACHINE");

            strip.SeedFrom(new ChipestraLogicalChannelInfo { Peak = 1.0f, Gain = 1.0f });

            Assert.Equal(1.0, strip.MeterFillFraction, precision: 6);
        }

        [Fact]
        public void MeterFillFraction_Silence_IsZero()
        {
            using var player = new Player(TestCatalog());
            var strip = new ChannelStripViewModel(player, 0, "Channel 0", "TEST_MACHINE");

            strip.SeedFrom(new ChipestraLogicalChannelInfo { Peak = 0.0f, Gain = 1.0f });

            Assert.Equal(0.0, strip.MeterFillFraction, precision: 6);
        }

        [Fact]
        public void UpdateMeter_RaisesPropertyChangedForDerivedMeterProperties()
        {
            using var player = new Player(TestCatalog());
            var strip = new ChannelStripViewModel(player, 0, "Channel 0", "TEST_MACHINE");

            var raised = new List<string?>();
            strip.PropertyChanged += (_, e) => raised.Add(e.PropertyName);

            strip.UpdateMeter(0.5f, 0.3f);

            Assert.Contains(nameof(ChannelStripViewModel.Peak), raised);
            Assert.Contains(nameof(ChannelStripViewModel.Rms), raised);
            Assert.Contains(nameof(ChannelStripViewModel.PeakDb), raised);
            Assert.Contains(nameof(ChannelStripViewModel.MeterFillFraction), raised);
        }

        // --- per-channel L/R meter, Peak Hold, clip color ---

        [Fact]
        public void UpdateMeter_RaisesPropertyChangedForStereoAndHoldProperties()
        {
            using var player = new Player(TestCatalog());
            var strip = new ChannelStripViewModel(player, 0, "Channel 0", "TEST_MACHINE");

            var raised = new List<string?>();
            strip.PropertyChanged += (_, e) => raised.Add(e.PropertyName);

            strip.UpdateMeter(0.5f, 0.3f, peakLeft: 0.4f, peakRight: 0.6f, rmsLeft: 0.2f, rmsRight: 0.3f);

            Assert.Contains(nameof(ChannelStripViewModel.PeakLeft), raised);
            Assert.Contains(nameof(ChannelStripViewModel.PeakRight), raised);
            Assert.Contains(nameof(ChannelStripViewModel.RmsLeft), raised);
            Assert.Contains(nameof(ChannelStripViewModel.RmsRight), raised);
            Assert.Contains(nameof(ChannelStripViewModel.MeterFillFractionLeft), raised);
            Assert.Contains(nameof(ChannelStripViewModel.MeterFillFractionRight), raised);
            Assert.Contains(nameof(ChannelStripViewModel.HoldFillFractionLeft), raised);
            Assert.Contains(nameof(ChannelStripViewModel.HoldFillFractionRight), raised);
            Assert.Contains(nameof(ChannelStripViewModel.HoldZoneLeft), raised);
            Assert.Contains(nameof(ChannelStripViewModel.HoldZoneRight), raised);
        }

        [Fact]
        public void MeterFillFractionLeftRight_ReflectSeparateLAndRPeaks()
        {
            using var player = new Player(TestCatalog());
            var strip = new ChannelStripViewModel(player, 0, "Channel 0", "TEST_MACHINE");

            strip.SeedFrom(new ChipestraLogicalChannelInfo { Gain = 1.0f, PeakLeft = 1.0f, PeakRight = 0.0f });

            Assert.Equal(1.0, strip.MeterFillFractionLeft, precision: 6);
            Assert.Equal(0.0, strip.MeterFillFractionRight, precision: 6);
        }

        // --- -90dB floor + 3-zone coloring ---

        [Fact]
        public void PeakDb_RangeExtendsTo90dBFloor()
        {
            using var player = new Player(TestCatalog());
            var strip = new ChannelStripViewModel(player, 0, "Channel 0", "TEST_MACHINE");

            // ~-74dB lies below a -60dB floor, so this confirms the wider range.
            strip.UpdateMeter(0.0002f, 0.0002f);
            Assert.True(strip.PeakDb < -60.0, "peak dB: a level below -60dB reads correctly");

            // Very quiet: clamps at the new -90dB floor, not -infinity.
            strip.UpdateMeter(0.00001f, 0.00001f);
            Assert.Equal(-90.0, strip.PeakDb, precision: 3);
        }

        [Theory]
        [InlineData(0.0001f, MeterZone.Green)]   // deep in green (~-80dB)
        [InlineData(0.2483f, MeterZone.Green)]   // just below the yellow boundary (~-12.1dB)
        [InlineData(0.2541f, MeterZone.Yellow)]  // just into yellow (~-11.9dB)
        [InlineData(0.4954f, MeterZone.Yellow)]  // just below the red boundary (~-6.1dB)
        [InlineData(0.5069f, MeterZone.Red)]     // just into red (~-5.9dB)
        [InlineData(1.0f, MeterZone.Red)]        // 0dBFS
        public void HoldZone_ReflectsDbPosition(float peak, MeterZone expected)
        {
            using var player = new Player(TestCatalog());
            var strip = new ChannelStripViewModel(player, 0, "Channel 0", "TEST_MACHINE");

            strip.UpdateMeter(peak, peak, peakLeft: peak, peakRight: 0f);

            Assert.Equal(expected, strip.HoldZoneLeft);
            Assert.Equal(MeterZone.Green, strip.HoldZoneRight);  // silent channel stays green
        }

        [Fact]
        public void PeakHold_HoldsAtLoudestPeakUntilDecayed()
        {
            using var player = new Player(TestCatalog());
            var strip = new ChannelStripViewModel(player, 0, "Channel 0", "TEST_MACHINE");

            // A loud peak, then silence for less than the hold period -
            // the held marker should still read the loud peak's level.
            strip.UpdateMeter(0f, 0f, peakLeft: 1.0f, peakRight: 0f, elapsedSeconds: 0.0);
            strip.UpdateMeter(0f, 0f, peakLeft: 0f, peakRight: 0f, elapsedSeconds: 0.5);

            Assert.Equal(1.0, strip.HoldFillFractionLeft, precision: 3);
        }

        [Fact]
        public void PeakHold_DecaysAfterHoldPeriodElapses()
        {
            using var player = new Player(TestCatalog());
            var strip = new ChannelStripViewModel(player, 0, "Channel 0", "TEST_MACHINE");

            strip.UpdateMeter(0f, 0f, peakLeft: 1.0f, peakRight: 0f, elapsedSeconds: 0.0);
            // Past the hold period (1.5s) plus enough decay time that the
            // held marker must have dropped measurably below full scale.
            strip.UpdateMeter(0f, 0f, peakLeft: 0f, peakRight: 0f, elapsedSeconds: 3.0);

            Assert.True(strip.HoldFillFractionLeft < 1.0);
        }

        [Fact]
        public void PeakHold_NewLouderPeakOverridesImmediately()
        {
            using var player = new Player(TestCatalog());
            var strip = new ChannelStripViewModel(player, 0, "Channel 0", "TEST_MACHINE");

            strip.UpdateMeter(0f, 0f, peakLeft: 0.3f, peakRight: 0f, elapsedSeconds: 0.0);
            strip.UpdateMeter(0f, 0f, peakLeft: 1.0f, peakRight: 0f, elapsedSeconds: 0.0);

            Assert.Equal(1.0, strip.HoldFillFractionLeft, precision: 3);
        }

        [Fact]
        public void PanAngleDegrees_MatchesPanKnobMath()
        {
            using var player = new Player(TestCatalog());
            var strip = new ChannelStripViewModel(player, 0, "Channel 0", "TEST_MACHINE");

            strip.Pan = 0.5;

            Assert.Equal(PanKnobMath.ToAngleDegrees(0.5), strip.PanAngleDegrees, precision: 6);
        }

        [Theory]
        [InlineData(0.0, 0.0)]
        [InlineData(1.0, 100.0)]
        [InlineData(-1.0, -100.0)]
        [InlineData(0.5, 50.0)]
        [InlineData(2.0, 100.0)]   // out-of-range: clamped
        [InlineData(-2.0, -100.0)] // out-of-range: clamped
        public void PanKnobMath_ToAngleDegrees_IsLinearOnePercentPerDegree(double pan, double expectedDegrees)
        {
            Assert.Equal(expectedDegrees, PanKnobMath.ToAngleDegrees(pan), precision: 6);
        }

        // --- L/R/C pan label ---

        [Theory]
        [InlineData(0.0, "C")]
        [InlineData(-1.0, "L")]    // exactly 100%: number omitted
        [InlineData(1.0, "R")]    // exactly 100%: number omitted
        [InlineData(-0.5, "L50")]
        [InlineData(0.33, "R33")]
        [InlineData(-0.995, "L")]  // rounds to 100%: number omitted, same as an exact -1.0
        public void PanLabelText_MatchesLRCWithPercent(double pan, string expected)
        {
            using var player = new Player(TestCatalog());
            var strip = new ChannelStripViewModel(player, 0, "Channel 0", "TEST_MACHINE");

            strip.Pan = pan;

            Assert.Equal(expected, strip.PanLabelText);
        }

        [Fact]
        public void PanLabelText_ReturnsCAfterWheelStepsCancelOutToATinyFloatingPointResidual()
        {
            // The mouse-wheel handler accumulates Pan in 0.01 steps, and 0.01 has no exact
            // double representation, so +0.01 n times then -0.01 n times does not reliably
            // land on exactly 0.0. Reproduced here directly; the label must still read "C".
            using var player = new Player(TestCatalog());
            var strip = new ChannelStripViewModel(player, 0, "Channel 0", "TEST_MACHINE");

            const double wheelStep = 0.01;
            for (int i = 0; i < 7; i++)
            {
                strip.Pan += wheelStep;
            }
            for (int i = 0; i < 7; i++)
            {
                strip.Pan -= wheelStep;
            }

            Assert.Equal("C", strip.PanLabelText);
        }

        [Fact]
        public void IsMuted_SetTwice_RaisesMuteOrSoloChangedOnlyOnActualChange()
        {
            using var player = new Player(TestCatalog());
            var strip = new ChannelStripViewModel(player, 0, "Channel 0", "TEST_MACHINE");

            int raisedCount = 0;
            strip.MuteOrSoloChanged += (_, _) => raisedCount++;

            strip.IsMuted = true;
            strip.IsMuted = true;  // no-op: same value again
            strip.IsSoloed = true;

            Assert.Equal(2, raisedCount);
        }

        // --- ChannelMixerViewModel: end-to-end against a real (TEST_MACHINE) Player ---

        // The Channel Meter always renders at least MinDisplaySlots strips, even with no
        // Machine loaded; the tail that are not real channels are grayed out (IsActive=false).
        private const int MinDisplaySlots = 30;

        [Fact]
        public void Constructor_BeforeAnyRebind_PopulatesPlaceholderSlotsAllInactive()
        {
            var mixer = new ChannelMixerViewModel();

            Assert.Equal(MinDisplaySlots, mixer.Channels.Count);
            Assert.All(mixer.Channels, strip => Assert.False(strip.IsActive));
        }

        [Fact]
        public void Rebind_NoPlayer_LeavesAllSlotsInactive()
        {
            var mixer = new ChannelMixerViewModel();
            mixer.Rebind(null);

            Assert.Equal(MinDisplaySlots, mixer.Channels.Count);
            Assert.All(mixer.Channels, strip => Assert.False(strip.IsActive));
        }

        [Fact]
        public void Rebind_LoadedGame_PopulatesOneActiveChannelPlusInactivePadding()
        {
            using var player = new Player(TestCatalog());
            Assert.Equal(ChipestraResult.Ok, player.LoadGame("TEST_GAME"));

            var mixer = new ChannelMixerViewModel();
            mixer.Rebind(player);

            Assert.Equal(MinDisplaySlots, mixer.Channels.Count);
            ChannelStripViewModel strip = mixer.Channels[0];
            Assert.True(strip.IsActive);
            Assert.Equal(0, strip.Index);
            Assert.Equal("Channel 0", strip.ChannelName);
            Assert.False(strip.IsMuted);
            Assert.Equal(0.0, strip.Pan, precision: 3);
            Assert.Equal(1.0, strip.Gain, precision: 3);

            Assert.All(mixer.Channels.Skip(1), placeholder => Assert.False(placeholder.IsActive));
        }

        [Fact]
        public void Rebind_NullAfterLoadedGame_ReturnsToAllInactivePlaceholders()
        {
            using var player = new Player(TestCatalog());
            Assert.Equal(ChipestraResult.Ok, player.LoadGame("TEST_GAME"));

            var mixer = new ChannelMixerViewModel();
            mixer.Rebind(player);
            Assert.True(mixer.Channels[0].IsActive);

            mixer.Rebind(null);

            Assert.Equal(MinDisplaySlots, mixer.Channels.Count);
            Assert.All(mixer.Channels, strip => Assert.False(strip.IsActive));
        }

        [Fact]
        public void AllMuteCommand_MutesTheSingleChannel_AndReachesNative()
        {
            using var player = new Player(TestCatalog());
            Assert.Equal(ChipestraResult.Ok, player.LoadGame("TEST_GAME"));

            var mixer = new ChannelMixerViewModel();
            mixer.Rebind(player);

            mixer.AllMuteCommand.Execute(null);

            Assert.True(mixer.Channels[0].IsMuted);
            // Reaches Native, not just this ViewModel's own bookkeeping.
            ChipestraLogicalChannelInfo? info = player.GetChannelInfo(0);
            Assert.NotNull(info);
            Assert.NotEqual((byte)0, info!.Value.Muted);
        }

        [Fact]
        public void AllUnmuteCommand_UnmutesAfterAllMute()
        {
            using var player = new Player(TestCatalog());
            Assert.Equal(ChipestraResult.Ok, player.LoadGame("TEST_GAME"));

            var mixer = new ChannelMixerViewModel();
            mixer.Rebind(player);
            mixer.AllMuteCommand.Execute(null);

            mixer.AllUnmuteCommand.Execute(null);

            Assert.False(mixer.Channels[0].IsMuted);
            ChipestraLogicalChannelInfo? info = player.GetChannelInfo(0);
            Assert.NotNull(info);
            Assert.Equal((byte)0, info!.Value.Muted);
        }

        [Fact]
        public void AllResetCommand_RestoresDefaultsAndMasterState()
        {
            using var player = new Player(TestCatalog());
            Assert.Equal(ChipestraResult.Ok, player.LoadGame("TEST_GAME"));

            var mixer = new ChannelMixerViewModel();
            mixer.Rebind(player);

            mixer.Channels[0].IsMuted = true;
            mixer.Channels[0].IsSoloed = true;
            mixer.Channels[0].Pan = 0.5;
            mixer.Channels[0].Gain = 0.25;
            mixer.MasterGain = 0.3;
            mixer.MasterMuted = true;

            mixer.AllResetCommand.Execute(null);

            Assert.False(mixer.Channels[0].IsMuted);
            Assert.False(mixer.Channels[0].IsSoloed);
            Assert.Equal(0.0, mixer.Channels[0].Pan, precision: 3);
            Assert.Equal(1.0, mixer.Channels[0].Gain, precision: 3);
            Assert.Equal(1.0, mixer.MasterGain, precision: 3);
            Assert.False(mixer.MasterMuted);
        }

        [Fact]
        public void SoloOffCommand_ClearsSoloAndRestoresDirectMuteState()
        {
            using var player = new Player(TestCatalog());
            Assert.Equal(ChipestraResult.Ok, player.LoadGame("TEST_GAME"));

            var mixer = new ChannelMixerViewModel();
            mixer.Rebind(player);

            // Solo the only channel: not muted directly, but soloing itself
            // shouldn't force it to appear muted (it's the soloed one).
            mixer.Channels[0].IsSoloed = true;
            ChipestraLogicalChannelInfo? soloedInfo = player.GetChannelInfo(0);
            Assert.Equal((byte)0, soloedInfo!.Value.Muted);

            mixer.SoloOffCommand.Execute(null);

            Assert.False(mixer.Channels[0].IsSoloed);
            Assert.False(mixer.Channels[0].IsMuted);
        }

        [Fact]
        public void RefreshMeters_WhilePlaying_PopulatesNonZeroPeak()
        {
            using var player = new Player(TestCatalog());
            Assert.Equal(ChipestraResult.Ok, player.LoadGame("TEST_GAME"));
            Assert.Equal(ChipestraResult.Ok, player.SelectOutput(OutputKind.Null));
            // SelectTrack starts playback itself; no separate Play() call is needed.
            Assert.Equal(ChipestraResult.Ok, player.SelectTrack("T1"));
            Assert.True(WaitForSubmittedFrames(player, 1, 1000));

            var mixer = new ChannelMixerViewModel();
            mixer.Rebind(player);
            mixer.RefreshMeters();

            // TestMachine's fixed non-silent pattern (test_machine.h) - the
            // exact value is already covered by native tests; here we're only
            // proving the ViewModel's poll path actually reaches real Peak/RMS,
            // not re-deriving the pattern's magnitude.
            Assert.True(mixer.Channels[0].Peak > 0.0f);
            // The same real poll path also populates the per-channel
            // L/R split (TestMachine's pan=0 default spreads it to both).
            Assert.True(mixer.Channels[0].PeakLeft > 0.0f);
            Assert.True(mixer.Channels[0].PeakRight > 0.0f);
            Assert.True(mixer.MasterPeakLeftFillFraction > 0.0 || mixer.MasterPeakRightFillFraction > 0.0);

            player.Stop();
        }

        // --- PAN song-sync / manual override ---

        [Fact]
        public void RefreshMeters_ReReadsPanPushedDirectlyThroughNative()
        {
            using var player = new Player(TestCatalog());
            Assert.Equal(ChipestraResult.Ok, player.LoadGame("TEST_GAME"));

            var mixer = new ChannelMixerViewModel();
            mixer.Rebind(player);
            Assert.Equal(0.0, mixer.Channels[0].Pan, precision: 3);

            // Simulates a board auto-syncing Pan on its own (as
            // XBoardMachine::UpdateChannelStereoRouting does) by writing
            // straight to Native, bypassing this ViewModel's own Pan setter -
            // RefreshMeters must pick the new value up on its next poll.
            Assert.Equal(ChipestraResult.Ok, player.SetChannelPan(0, 0.6f));
            mixer.RefreshMeters();

            Assert.Equal(0.6, mixer.Channels[0].Pan, precision: 3);
        }

        [Fact]
        public void ClearPanOverride_ResetsPanAndReachesNative()
        {
            using var player = new Player(TestCatalog());
            Assert.Equal(ChipestraResult.Ok, player.LoadGame("TEST_GAME"));

            var mixer = new ChannelMixerViewModel();
            mixer.Rebind(player);
            mixer.Channels[0].Pan = -0.8;
            Assert.Equal(-0.8, mixer.Channels[0].Pan, precision: 3);

            mixer.Channels[0].ClearPanOverride();

            Assert.Equal(0.0, mixer.Channels[0].Pan, precision: 3);
            ChipestraLogicalChannelInfo? info = player.GetChannelInfo(0);
            Assert.NotNull(info);
            Assert.Equal(0.0f, info!.Value.Pan);
        }

        // --- MASTER VOLUME percent display ---

        [Theory]
        [InlineData(1.0, "100%")]
        [InlineData(0.0, "0%")]
        [InlineData(2.0, "200%")]
        [InlineData(0.5, "50%")]
        public void MasterGainPercentText_MatchesGainAsWholePercent(double gain, string expected)
        {
            var mixer = new ChannelMixerViewModel { MasterGain = gain };

            Assert.Equal(expected, mixer.MasterGainPercentText);
        }

        [Fact]
        public void AllResetCommand_ClearsPanOverride()
        {
            using var player = new Player(TestCatalog());
            Assert.Equal(ChipestraResult.Ok, player.LoadGame("TEST_GAME"));

            var mixer = new ChannelMixerViewModel();
            mixer.Rebind(player);
            mixer.Channels[0].Pan = 0.9;

            mixer.AllResetCommand.Execute(null);

            Assert.Equal(0.0, mixer.Channels[0].Pan, precision: 3);
            ChipestraLogicalChannelInfo? info = player.GetChannelInfo(0);
            Assert.NotNull(info);
            Assert.Equal(0.0f, info!.Value.Pan);
        }
    }
}
