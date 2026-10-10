// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using System.Collections.Generic;
using System.Threading;
using Chipestra.Core;
using Xunit;

namespace Chipestra.Core.Tests
{
    // Player (LoadGame -> SelectTrack -> Play/Pause/Resume/Stop, track switch) against
    // TEST_MACHINE, a synthetic no-ROM Machine, so these run without any real ROM or audio device.
    public class PlayerTests
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
                    new TrackDefinition { Id = "T2", DisplayName = "Test Track 2", Command = 2 },
                    // Control-command track: Loops is irrelevant since such tracks never
                    // reach the Playing state that PollTrackEndBySilence checks.
                    new TrackDefinition { Id = "STOP", DisplayName = "Stop", Command = 0, IsControlCommand = true },
                    // Non-looping track for silence-based auto-stop tests.
                    new TrackDefinition { Id = "T3", DisplayName = "Test Track 3 (one-shot)", Command = 3, Loops = false },
                },
            };
            return new Dictionary<string, GameDefinition>(StringComparer.Ordinal) { { "TEST_GAME", game } };
        }

        // Player renders asynchronously via PlaybackPump once Playing, so "has it started"
        // has to be polled rather than asserted right after Play()/Resume() returns.
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

        // TEST_MACHINE always renders a nonzero pattern (source peak 0.5), so the
        // tests simulate "the song is silent" by raising SilenceThresholdLinear
        // above it (and "sound" by putting it back) - muting no longer counts
        // as silence, because the check reads the sound before Mute/Gain/Pan.
        private const float SilentThreshold = 10f;
        private const float SoundThreshold = 0.001f;

        private static Player StartedOneShot(string trackId = "T3")
        {
            var player = new Player(TestCatalog());
            Assert.Equal(ChipestraResult.Ok, player.LoadGame("TEST_GAME"));
            Assert.Equal(ChipestraResult.Ok, player.SelectOutput(OutputKind.Null));
            Assert.Equal(ChipestraResult.Ok, player.SelectTrack(trackId));
            Assert.True(WaitForSubmittedFrames(player, 1, 1000));
            return player;
        }

        // Polls PollTrackEndBySilence() itself (as MainViewModel's poll tick does, before
        // reading Status) until State reaches Ready or timeoutMs elapses.
        private static bool WaitUntilReadyViaSilencePoll(Player player, int timeoutMs)
        {
            const int pollIntervalMs = 5;
            for (int waited = 0; waited <= timeoutMs; waited += pollIntervalMs)
            {
                player.PollTrackEndBySilence();
                if (player.Status.State == PlaybackState.Ready)
                {
                    return true;
                }
                Thread.Sleep(pollIntervalMs);
            }
            return false;
        }

        [Fact]
        public void PlayerLifecycle_LoadSelectPlayStop_Succeeds()
        {
            using var player = new Player(TestCatalog());

            Assert.Equal(ChipestraResult.Ok, player.LoadGame("TEST_GAME"));
            Assert.Equal(PlaybackState.MachineLoaded, player.Status.State);

            Assert.Equal(ChipestraResult.Ok, player.SelectOutput(OutputKind.Null));
            Assert.Equal(PlaybackState.Ready, player.Status.State);

            // SelectTrack starts playback itself; no separate Play() call is needed.
            Assert.Equal(ChipestraResult.Ok, player.SelectTrack("T1"));
            Assert.Equal("T1", player.Status.Track?.Id);
            Assert.Equal(PlaybackState.Playing, player.Status.State);
            Assert.True(WaitForSubmittedFrames(player, 1, 1000));

            Assert.Equal(ChipestraResult.Ok, player.Stop());
            Assert.Equal(PlaybackState.Ready, player.Status.State);
        }

        [Fact]
        public void PlayerLifecycle_FullCycle_PauseResumeAndTrackSwitch()
        {
            using var player = new Player(TestCatalog());
            Assert.Equal(ChipestraResult.Ok, player.LoadGame("TEST_GAME"));
            Assert.Equal(ChipestraResult.Ok, player.SelectOutput(OutputKind.Null));
            // SelectTrack starts playback itself; no separate Play() call is needed.
            Assert.Equal(ChipestraResult.Ok, player.SelectTrack("T1"));
            Assert.True(WaitForSubmittedFrames(player, 1, 1000));

            // Pause -> submission stops (at most one in-flight block can still complete).
            Assert.Equal(ChipestraResult.Ok, player.Pause());
            Assert.Equal(PlaybackState.Paused, player.Status.State);
            Thread.Sleep(50);
            ulong pausedFrames = player.Status.RenderCounters?.SubmittedFrameCount ?? 0;
            Thread.Sleep(50);
            Assert.Equal(pausedFrames, player.Status.RenderCounters?.SubmittedFrameCount ?? 0);

            // Resume -> rendering continues from where it left off.
            Assert.Equal(ChipestraResult.Ok, player.Resume());
            Assert.True(WaitForSubmittedFrames(player, pausedFrames + 1, 1000));
            Assert.Equal(PlaybackState.Playing, player.Status.State);

            // Track switch while Playing: stop, send the new track's command, resume. State
            // only ever reads Playing before/after, though it passes through Ready inside SelectTrack.
            Assert.Equal(ChipestraResult.Ok, player.SelectTrack("T2"));
            Assert.Equal("T2", player.Status.Track?.Id);
            Assert.Equal(PlaybackState.Playing, player.Status.State);
            // Switching tracks resets the position baseline (a fresh track), so
            // "has rendering resumed" needs its own bounded wait rather than
            // comparing against the pre-switch frame count.
            Assert.True(WaitForSubmittedFrames(player, 1, 1000));

            Assert.Equal(ChipestraResult.Ok, player.Stop());
            Assert.Equal(PlaybackState.Ready, player.Status.State);

            // Track selection while Stopped (Ready) starts playback immediately too.
            Assert.Equal(ChipestraResult.Ok, player.SelectTrack("T1"));
            Assert.Equal(PlaybackState.Playing, player.Status.State);
            Assert.Equal("T1", player.Status.Track?.Id);
            Assert.True(WaitForSubmittedFrames(player, 1, 1000));
        }

        [Fact]
        public void SelectTrack_WhilePaused_ResumesIntoPlaying()
        {
            // Selecting a track always ends in Playing, including from Paused (resumes via
            // NativeRuntimeSession.ResumePlayback, since StartPlayback only accepts Ready).
            using var player = new Player(TestCatalog());
            Assert.Equal(ChipestraResult.Ok, player.LoadGame("TEST_GAME"));
            Assert.Equal(ChipestraResult.Ok, player.SelectOutput(OutputKind.Null));
            Assert.Equal(ChipestraResult.Ok, player.SelectTrack("T1"));
            Assert.True(WaitForSubmittedFrames(player, 1, 1000));

            Assert.Equal(ChipestraResult.Ok, player.Pause());
            Assert.Equal(PlaybackState.Paused, player.Status.State);

            Assert.Equal(ChipestraResult.Ok, player.SelectTrack("T2"));
            Assert.Equal(PlaybackState.Playing, player.Status.State);
            Assert.Equal("T2", player.Status.Track?.Id);
            Assert.True(WaitForSubmittedFrames(player, 1, 1000));
        }

        [Fact]
        public void LoadGame_UnknownGameId_ReturnsNotFound()
        {
            using var player = new Player(TestCatalog());
            Assert.Equal(ChipestraResult.NotFound, player.LoadGame("NO_SUCH_GAME"));
            Assert.Equal(PlaybackState.Created, player.Status.State);
        }

        [Fact]
        public void SelectTrack_BeforeLoadGame_ReturnsInvalidState()
        {
            using var player = new Player(TestCatalog());
            Assert.Equal(ChipestraResult.InvalidState, player.SelectTrack("T1"));
        }

        [Fact]
        public void SelectTrack_UnknownTrackId_ReturnsNotFound()
        {
            using var player = new Player(TestCatalog());
            Assert.Equal(ChipestraResult.Ok, player.LoadGame("TEST_GAME"));
            Assert.Equal(ChipestraResult.NotFound, player.SelectTrack("NO_SUCH_TRACK"));
            // A failed lookup must not disturb the selected track.
            Assert.Null(player.Status.Track);
        }

        [Fact]
        public void Play_BeforeSelectOutput_ReturnsInvalidState()
        {
            using var player = new Player(TestCatalog());
            Assert.Equal(ChipestraResult.Ok, player.LoadGame("TEST_GAME"));
            Assert.Equal(ChipestraResult.InvalidState, player.Play());
        }

        [Fact]
        public void Status_RenderCountersNull_BeforeOutputSelected()
        {
            using var player = new Player(TestCatalog());
            Assert.Null(player.Status.RenderCounters);
            Assert.Equal(OutputKind.None, player.Status.OutputState);
            Assert.Equal(TimeSpan.Zero, player.Status.Position);
        }

        // PlaybackStatus.ChannelCount: TestMachine always exposes exactly 1 LogicalChannel.
        [Fact]
        public void Status_ChannelCountNull_BeforeLoadGame()
        {
            using var player = new Player(TestCatalog());
            Assert.Null(player.Status.ChannelCount);
        }

        [Fact]
        public void Status_ChannelCount_PopulatedAfterLoadGame()
        {
            using var player = new Player(TestCatalog());
            Assert.Equal(ChipestraResult.Ok, player.LoadGame("TEST_GAME"));
            Assert.Equal(1, player.Status.ChannelCount);
        }

        [Fact]
        public void Status_ChannelCount_StillReadableWhilePlaying()
        {
            // chipestra_get_machine_channel_count is safe to call while Playing (unlike
            // SendCommand), so the count must not go null/stale once playback starts.
            using var player = new Player(TestCatalog());
            Assert.Equal(ChipestraResult.Ok, player.LoadGame("TEST_GAME"));
            Assert.Equal(ChipestraResult.Ok, player.SelectOutput(OutputKind.Null));
            // SelectTrack starts playback itself; no separate Play() call is needed.
            Assert.Equal(ChipestraResult.Ok, player.SelectTrack("T1"));

            Assert.Equal(1, player.Status.ChannelCount);
        }

        // --- Control-command tracks ---

        [Fact]
        public void SelectTrack_ControlCommandTrack_StaysReadyAndPositionDoesNotAdvance()
        {
            using var player = new Player(TestCatalog());
            Assert.Equal(ChipestraResult.Ok, player.LoadGame("TEST_GAME"));
            Assert.Equal(ChipestraResult.Ok, player.SelectOutput(OutputKind.Null));

            Assert.Equal(ChipestraResult.Ok, player.SelectTrack("STOP"));
            Assert.Equal(PlaybackState.Ready, player.Status.State);
            Assert.Equal("STOP", player.Status.Track?.Id);

            Thread.Sleep(100);
            Assert.Equal(TimeSpan.Zero, player.Status.Position);
        }

        [Fact]
        public void SelectTrack_ControlCommandTrackWhilePlaying_StopsPlayback()
        {
            using var player = new Player(TestCatalog());
            Assert.Equal(ChipestraResult.Ok, player.LoadGame("TEST_GAME"));
            Assert.Equal(ChipestraResult.Ok, player.SelectOutput(OutputKind.Null));
            Assert.Equal(ChipestraResult.Ok, player.SelectTrack("T1"));
            Assert.True(WaitForSubmittedFrames(player, 1, 1000));

            Assert.Equal(ChipestraResult.Ok, player.SelectTrack("STOP"));
            Assert.Equal(PlaybackState.Ready, player.Status.State);
            Assert.Equal("STOP", player.Status.Track?.Id);
        }

        [Fact]
        public void SelectTrack_ControlCommandTrackWhilePaused_StopsPlayback()
        {
            using var player = new Player(TestCatalog());
            Assert.Equal(ChipestraResult.Ok, player.LoadGame("TEST_GAME"));
            Assert.Equal(ChipestraResult.Ok, player.SelectOutput(OutputKind.Null));
            Assert.Equal(ChipestraResult.Ok, player.SelectTrack("T1"));
            Assert.True(WaitForSubmittedFrames(player, 1, 1000));
            Assert.Equal(ChipestraResult.Ok, player.Pause());
            Assert.Equal(PlaybackState.Paused, player.Status.State);

            Assert.Equal(ChipestraResult.Ok, player.SelectTrack("STOP"));
            Assert.Equal(PlaybackState.Ready, player.Status.State);
        }

        [Fact]
        public void SelectTrack_ControlCommandTrackWhileReady_SendsCommandAndStaysReady()
        {
            // Already-Ready (never played, or Stopped) is the one starting state that needs
            // no StopPlayback() call in SelectTrack's control-command branch.
            using var player = new Player(TestCatalog());
            Assert.Equal(ChipestraResult.Ok, player.LoadGame("TEST_GAME"));
            Assert.Equal(ChipestraResult.Ok, player.SelectOutput(OutputKind.Null));
            Assert.Equal(PlaybackState.Ready, player.Status.State);

            Assert.Equal(ChipestraResult.Ok, player.SelectTrack("STOP"));
            Assert.Equal(PlaybackState.Ready, player.Status.State);
        }

        // --- Silence-based auto-stop for loops="false" tracks ---

        [Fact]
        public void PollTrackEndBySilence_NonLoopingTrackSilentBeyondDuration_TransitionsToReady()
        {
            using var player = new Player(TestCatalog());
            Assert.Equal(ChipestraResult.Ok, player.LoadGame("TEST_GAME"));
            Assert.Equal(ChipestraResult.Ok, player.SelectOutput(OutputKind.Null));
            Assert.Equal(ChipestraResult.Ok, player.SelectTrack("T3"));  // Loops == false
            Assert.True(WaitForSubmittedFrames(player, 1, 1000));

            player.SilenceDurationSeconds = 0.05;
            // Sound first (the check only starts once something has sounded),
            // then "the track finished" via the threshold (see SilentThreshold).
            player.PollTrackEndBySilence();
            player.SilenceThresholdLinear = SilentThreshold;

            Assert.True(WaitUntilReadyViaSilencePoll(player, 2000));
        }

        [Fact]
        public void PollTrackEndBySilence_LoopingTrackNeverAutoStopsEvenWhenSilent()
        {
            using var player = new Player(TestCatalog());
            Assert.Equal(ChipestraResult.Ok, player.LoadGame("TEST_GAME"));
            Assert.Equal(ChipestraResult.Ok, player.SelectOutput(OutputKind.Null));
            Assert.Equal(ChipestraResult.Ok, player.SelectTrack("T1"));  // Loops defaults to true
            Assert.True(WaitForSubmittedFrames(player, 1, 1000));

            player.SilenceDurationSeconds = 0.05;
            player.SilenceThresholdLinear = SilentThreshold;

            for (int i = 0; i < 20; i++)
            {
                player.PollTrackEndBySilence();
                Thread.Sleep(10);
            }
            Assert.Equal(PlaybackState.Playing, player.Status.State);
        }

        [Fact]
        public void PollTrackEndBySilence_NonLoopingTrackWithSignal_StaysPlaying()
        {
            using var player = new Player(TestCatalog());
            Assert.Equal(ChipestraResult.Ok, player.LoadGame("TEST_GAME"));
            Assert.Equal(ChipestraResult.Ok, player.SelectOutput(OutputKind.Null));
            Assert.Equal(ChipestraResult.Ok, player.SelectTrack("T3"));  // Loops == false
            Assert.True(WaitForSubmittedFrames(player, 1, 1000));

            player.SilenceDurationSeconds = 0.05;
            // Channel left unmuted: TEST_MACHINE's fixed nonzero pattern keeps
            // Peak above SilenceThresholdLinear the whole time.

            for (int i = 0; i < 20; i++)
            {
                player.PollTrackEndBySilence();
                Thread.Sleep(10);
            }
            Assert.Equal(PlaybackState.Playing, player.Status.State);
        }

        [Fact]
        public void PollTrackEndBySilence_SilenceInterrupted_ResetsTimer()
        {
            using var player = new Player(TestCatalog());
            Assert.Equal(ChipestraResult.Ok, player.LoadGame("TEST_GAME"));
            Assert.Equal(ChipestraResult.Ok, player.SelectOutput(OutputKind.Null));
            Assert.Equal(ChipestraResult.Ok, player.SelectTrack("T3"));
            Assert.True(WaitForSubmittedFrames(player, 1, 1000));

            player.SilenceDurationSeconds = 0.2;
            player.PollTrackEndBySilence();  // sound heard
            player.SilenceThresholdLinear = SilentThreshold;
            player.PollTrackEndBySilence();  // streak starts
            Thread.Sleep(100);
            player.PollTrackEndBySilence();
            Assert.Equal(PlaybackState.Playing, player.Status.State);  // well under 0.2s so far

            // Interrupt the silence streak before it reaches SilenceDurationSeconds.
            player.SilenceThresholdLinear = SoundThreshold;
            player.PollTrackEndBySilence();
            player.SilenceThresholdLinear = SilentThreshold;
            player.PollTrackEndBySilence();  // counting again from here
            Thread.Sleep(150);
            player.PollTrackEndBySilence();
            Assert.Equal(PlaybackState.Playing, player.Status.State);  // timer reset, not yet re-tripped
        }

        [Fact]
        public void PollTrackEndBySilence_NoOpWhenNotPlaying()
        {
            using var player = new Player(TestCatalog());
            // Never loaded/played at all - must not throw or otherwise misbehave.
            player.PollTrackEndBySilence();
            Assert.Equal(PlaybackState.Created, player.Status.State);
        }

        // --- Stop rewinds to the beginning; Pause/Resume continues ---

        [Fact]
        public void Stop_ResetsPositionToZeroImmediately()
        {
            using var player = new Player(TestCatalog());
            Assert.Equal(ChipestraResult.Ok, player.LoadGame("TEST_GAME"));
            Assert.Equal(ChipestraResult.Ok, player.SelectOutput(OutputKind.Null));
            Assert.Equal(ChipestraResult.Ok, player.SelectTrack("T1"));
            Assert.True(WaitForSubmittedFrames(player, 1, 1000));
            Thread.Sleep(50);
            Assert.NotEqual(TimeSpan.Zero, player.Status.Position);  // real elapsed time has accrued

            Assert.Equal(ChipestraResult.Ok, player.Stop());
            Assert.Equal(TimeSpan.Zero, player.Status.Position);
        }

        [Fact]
        public void Stop_ThenPlay_RestartsPositionFromZeroInsteadOfResuming()
        {
            // Native's chipestra_stop_playback only tears down the PlaybackPump, so Stop()
            // itself must reset the Position baseline (and resend the track's command);
            // otherwise Play() would just continue like Pause/Resume. Testable via Position
            // alone, independent of TestMachine's (nonexistent) internal song state.
            using var player = new Player(TestCatalog());
            Assert.Equal(ChipestraResult.Ok, player.LoadGame("TEST_GAME"));
            Assert.Equal(ChipestraResult.Ok, player.SelectOutput(OutputKind.Null));
            Assert.Equal(ChipestraResult.Ok, player.SelectTrack("T1"));
            Assert.True(WaitForSubmittedFrames(player, 1, 1000));
            Thread.Sleep(50);

            Assert.Equal(ChipestraResult.Ok, player.Stop());
            Assert.Equal(TimeSpan.Zero, player.Status.Position);

            Assert.Equal(ChipestraResult.Ok, player.Play());
            Assert.Equal(PlaybackState.Playing, player.Status.State);
            Thread.Sleep(20);
            // Position must start fresh near zero, not continue on top of the old baseline.
            Assert.True(player.Status.Position < TimeSpan.FromMilliseconds(500));
        }

        [Fact]
        public void Pause_ThenResume_PositionContinuesWithoutResetting()
        {
            using var player = new Player(TestCatalog());
            Assert.Equal(ChipestraResult.Ok, player.LoadGame("TEST_GAME"));
            Assert.Equal(ChipestraResult.Ok, player.SelectOutput(OutputKind.Null));
            Assert.Equal(ChipestraResult.Ok, player.SelectTrack("T1"));
            Assert.True(WaitForSubmittedFrames(player, 1, 1000));
            Thread.Sleep(80);

            Assert.Equal(ChipestraResult.Ok, player.Pause());
            TimeSpan pausedPosition = player.Status.Position;
            Assert.True(pausedPosition > TimeSpan.Zero);  // Pause must NOT rewind, unlike Stop

            Assert.Equal(ChipestraResult.Ok, player.Resume());
            Assert.True(player.Status.Position >= pausedPosition);  // continues, never drops back toward zero
        }

        [Fact]
        public void PollTrackEndBySilence_AfterAutoStop_PlayRestartsPositionFromZero()
        {
            // The auto-stop path must rewind like Stop() does, so Play() afterwards starts
            // the song again with Position from zero instead of continuing silence.
            using var player = new Player(TestCatalog());
            Assert.Equal(ChipestraResult.Ok, player.LoadGame("TEST_GAME"));
            Assert.Equal(ChipestraResult.Ok, player.SelectOutput(OutputKind.Null));
            Assert.Equal(ChipestraResult.Ok, player.SelectTrack("T3"));  // Loops == false
            Assert.True(WaitForSubmittedFrames(player, 1, 1000));

            player.SilenceDurationSeconds = 0.05;
            player.PollTrackEndBySilence();  // sound heard
            player.SilenceThresholdLinear = SilentThreshold;
            Assert.True(WaitUntilReadyViaSilencePoll(player, 2000));
            Assert.Equal(TimeSpan.Zero, player.Status.Position);

            // Unmute so the next Play() does not immediately trip PollTrackEndBySilence again;
            // TestMachine's Mute state is a Player/Mixer-level override that a resent command
            // does not undo.
            player.SilenceThresholdLinear = SoundThreshold;

            Assert.Equal(ChipestraResult.Ok, player.Play());
            Assert.Equal(PlaybackState.Playing, player.Status.State);
            Thread.Sleep(20);
            Assert.True(player.Status.Position < TimeSpan.FromMilliseconds(500));
        }

        // --- Auto-stop counts silence only after the first sound; reads the pre-Mute/Gain/Pan sound ---

        private static void PollFor(Player player, int ms)
        {
            for (int waited = 0; waited < ms; waited += 10)
            {
                player.PollTrackEndBySilence();
                Thread.Sleep(10);
            }
        }

        [Fact]
        public void AutoStop_SilentIntro_DoesNotStopBeforeFirstSound()
        {
            using var player = StartedOneShot();
            player.SilenceDurationSeconds = 0.05;
            player.SilenceThresholdLinear = SilentThreshold;
            PollFor(player, 400);  // 8x the duration, still the intro
            Assert.Equal(PlaybackState.Playing, player.Status.State);
        }

        [Fact]
        public void AutoStop_SilenceAfterSound_StopsAfterDuration()
        {
            using var player = StartedOneShot();
            player.SilenceDurationSeconds = 0.05;
            player.PollTrackEndBySilence();
            player.SilenceThresholdLinear = SilentThreshold;
            Assert.True(WaitUntilReadyViaSilencePoll(player, 2000));
        }

        [Fact]
        public void AutoStop_SoundReturningDuringSilence_RestartsCount()
        {
            using var player = StartedOneShot();
            player.SilenceDurationSeconds = 0.5;
            player.PollTrackEndBySilence();
            player.SilenceThresholdLinear = SilentThreshold;
            PollFor(player, 300);
            player.SilenceThresholdLinear = SoundThreshold;
            player.PollTrackEndBySilence();
            player.SilenceThresholdLinear = SilentThreshold;
            PollFor(player, 300);  // 600ms since the first silence, but only ~300 since the return
            Assert.Equal(PlaybackState.Playing, player.Status.State);
            Assert.True(WaitUntilReadyViaSilencePoll(player, 3000));
        }

        [Fact]
        public void AutoStop_SelectingAnotherTrack_ForgetsEarlierSound()
        {
            using var player = StartedOneShot();
            player.SilenceDurationSeconds = 0.05;
            player.PollTrackEndBySilence();  // sound heard
            Assert.Equal(ChipestraResult.Ok, player.SelectTrack("T3"));  // fresh start
            player.SilenceThresholdLinear = SilentThreshold;
            PollFor(player, 300);
            Assert.Equal(PlaybackState.Playing, player.Status.State);
        }

        [Fact]
        public void AutoStop_StopThenPlay_ForgetsEarlierSound()
        {
            using var player = StartedOneShot();
            player.SilenceDurationSeconds = 0.05;
            player.PollTrackEndBySilence();  // sound heard
            Assert.Equal(ChipestraResult.Ok, player.Stop());
            Assert.Equal(ChipestraResult.Ok, player.Play());
            player.SilenceThresholdLinear = SilentThreshold;
            PollFor(player, 300);
            Assert.Equal(PlaybackState.Playing, player.Status.State);
        }

        [Fact]
        public void AutoStop_PauseResume_KeepsSoundHeard()
        {
            using var player = StartedOneShot();
            player.SilenceDurationSeconds = 0.05;
            player.PollTrackEndBySilence();  // sound heard
            Assert.Equal(ChipestraResult.Ok, player.Pause());
            Assert.Equal(ChipestraResult.Ok, player.Resume());
            player.SilenceThresholdLinear = SilentThreshold;
            Assert.True(WaitUntilReadyViaSilencePoll(player, 2000));
        }

        [Fact]
        public void AutoStop_NeverAnySound_NeverStops()
        {
            using var player = StartedOneShot();
            player.SilenceDurationSeconds = 0.05;
            player.SilenceThresholdLinear = SilentThreshold;
            PollFor(player, 500);
            Assert.Equal(PlaybackState.Playing, player.Status.State);
        }

        [Fact]
        public void AutoStop_MuteGainZeroOrMasterZero_DoesNotStopWhileSounding()
        {
            using var player = StartedOneShot();
            player.SilenceDurationSeconds = 0.05;
            player.PollTrackEndBySilence();  // sound heard

            Assert.Equal(ChipestraResult.Ok, player.SetChannelMuted(0, true));
            PollFor(player, 300);
            Assert.Equal(PlaybackState.Playing, player.Status.State);
            Assert.Equal(ChipestraResult.Ok, player.SetChannelMuted(0, false));

            Assert.Equal(ChipestraResult.Ok, player.SetChannelGain(0, 0f));
            PollFor(player, 300);
            Assert.Equal(PlaybackState.Playing, player.Status.State);
            Assert.Equal(ChipestraResult.Ok, player.SetChannelGain(0, 1f));

            Assert.Equal(ChipestraResult.Ok, player.SetMasterGain(0f));
            PollFor(player, 300);
            Assert.Equal(PlaybackState.Playing, player.Status.State);

            // The Channel Strip meter still shows the post-Mute value.
            Assert.Equal(ChipestraResult.Ok, player.SetMasterGain(1f));
            Assert.Equal(ChipestraResult.Ok, player.SetChannelMuted(0, true));
            Thread.Sleep(50);
            ChipestraLogicalChannelInfo info = player.GetChannelInfo(0)!.Value;
            Assert.Equal(0f, info.PeakLeft);
            Assert.True(info.SourcePeak > 0.1f);
        }

        // The mixer's level readings are only rewritten by the next rendered block, so right after a
        // switch / Stop->Play they still held the PREVIOUS song's sound, which the check took for the new
        // song's first sound - and then counted the new song's silent intro as silence after sound.
        // TEST_MACHINE always sounds, so a poll that lands after the first new block would be legitimate;
        // each attempt therefore only counts when Position was still 0 around the poll (no new block yet).
        private static void RunStaleReadingAttempts(Action<Player> restart)
        {
            bool conclusive = false;
            for (int attempt = 0; attempt < 25 && !conclusive; attempt++)
            {
                using var player = StartedOneShot("T1");  // a sounding song
                player.SilenceDurationSeconds = 0.05;
                player.PollTrackEndBySilence();           // (T1 loops, so this only refreshes state)
                restart(player);
                bool noBlockYet = player.Status.Position == TimeSpan.Zero;
                player.PollTrackEndBySilence();           // the first poll after the restart
                noBlockYet &= player.Status.Position == TimeSpan.Zero;
                if (!noBlockYet)
                {
                    continue;
                }
                conclusive = true;

                player.SilenceThresholdLinear = SilentThreshold;  // from here on the new song's intro is silent
                PollFor(player, 300);
                Assert.Equal(PlaybackState.Playing, player.Status.State);
            }
            Assert.True(conclusive, "no attempt polled before the first new block was rendered");
        }

        [Fact]
        public void AutoStop_SwitchFromASoundingSong_DoesNotTakeTheOldSoundForTheNewSongs()
        {
            RunStaleReadingAttempts(p => Assert.Equal(ChipestraResult.Ok, p.SelectTrack("T3")));
        }

        [Fact]
        public void AutoStop_StopThenPlay_DoesNotTakeTheOldSoundForTheNewSongs()
        {
            RunStaleReadingAttempts(p =>
            {
                // T1 is selected; switch the player to the one-shot first, let it sound, then Stop -> Play.
                Assert.Equal(ChipestraResult.Ok, p.SelectTrack("T3"));
                Assert.True(WaitForSubmittedFrames(p, 1, 1000));
                Assert.Equal(ChipestraResult.Ok, p.Stop());
                Assert.Equal(ChipestraResult.Ok, p.Play());
            });
        }

        [Fact]
        public void AutoStop_LoopingTrack_NeverStopsEvenAfterSound()
        {
            using var player = StartedOneShot("T1");
            player.SilenceDurationSeconds = 0.05;
            player.PollTrackEndBySilence();
            player.SilenceThresholdLinear = SilentThreshold;
            PollFor(player, 300);
            Assert.Equal(PlaybackState.Playing, player.Status.State);
        }

        // --- WSG sound-effect tracks (type="wsg") ---

        [Fact]
        public void SelectTrack_WsgTrackOnAGameWithoutWsgRequests_FailsWithUnsupportedCommand()
        {
            var game = new GameDefinition
            {
                GameId = "NOWSG",
                Title = "No WSG",
                MachineId = "TEST_MACHINE",
                VariantId = "STANDARD",
                RequiredResources = Array.Empty<RomResource>(),
                Tracks = new List<TrackDefinition>
                {
                    new TrackDefinition { Id = "W", DisplayName = "wsg", Command = 5, IsWsg = true, Loops = false },
                },
            };
            using var player = new Player(new Dictionary<string, GameDefinition>(StringComparer.Ordinal) { { "NOWSG", game } });
            Assert.Equal(ChipestraResult.Ok, player.LoadGame("NOWSG"));
            Assert.Equal(ChipestraResult.Ok, player.SelectOutput(OutputKind.Null));

            Assert.Equal(ChipestraResult.UnsupportedCommand, player.SelectTrack("W"));
            Assert.NotEqual(PlaybackState.Playing, player.Status.State);
        }

        private static Player? RealDragonSpirit()
        {
            string? root = Environment.GetEnvironmentVariable("CHIPESTRA_ROM_ROOT");
            if (string.IsNullOrEmpty(root))
            {
                return null;
            }
            var dir = new System.IO.DirectoryInfo(AppContext.BaseDirectory);
            while (!System.IO.File.Exists(System.IO.Path.Combine(dir.FullName, "xml", "dspirit.xml")))
            {
                dir = dir.Parent!;
            }
            GameDefinition g = GameDefinitionXml.Load(System.IO.Path.Combine(dir.FullName, "xml", "dspirit.xml"));
            var p = new Player(new Dictionary<string, GameDefinition>(StringComparer.Ordinal) { { g.GameId, g } });
            Assert.Equal(ChipestraResult.Ok, p.LoadGame(g.GameId, root));
            Assert.Equal(ChipestraResult.Ok, p.SelectOutput(OutputKind.Null));
            return p;
        }

        private static float WsgSourcePeak(Player p)
        {
            float peak = 0;
            int n = p.Status.ChannelCount ?? 0;
            for (int i = 8; i < 16 && i < n; i++)  // CUS30 voices (after the 8 FM channels)
            {
                peak = Math.Max(peak, p.GetChannelInfo(i)?.SourcePeak ?? 0);
            }
            return peak;
        }

        [Fact]
        public void RealRom_WsgTrack_SoundsThenAutoStops_AndStopThenPlayResendsAsWsg()
        {
            using Player? player = RealDragonSpirit();
            if (player == null)
            {
                return;  // needs CHIPESTRA_ROM_ROOT
            }

            Assert.Equal(ChipestraResult.Ok, player.SelectTrack("WSG_00"));  // "[SFX] Credit", about 0.6s
            bool sounded = false;
            var sw = System.Diagnostics.Stopwatch.StartNew();
            while (sw.Elapsed.TotalSeconds < 15 && player.Status.State == PlaybackState.Playing)
            {
                player.PollTrackEndBySilence();
                sounded |= WsgSourcePeak(player) > 0.001f;
                Thread.Sleep(20);
            }
            Assert.True(sounded, "the WSG effect sounded on the CUS30 channels");
            Assert.Equal(PlaybackState.Ready, player.Status.State);  // loops="false": stopped by itself after the effect ended
            Assert.True(sw.Elapsed.TotalSeconds > 4.0, "it stops after the 4s of silence, not before it sounded");

            // Play again: Stop -> Play re-sends the request as Wsg (as Raw 0x00 it would be a BGM STOP and stay silent).
            Assert.Equal(ChipestraResult.Ok, player.Play());
            bool soundedAgain = false;
            sw.Restart();
            while (sw.Elapsed.TotalSeconds < 3)
            {
                soundedAgain |= WsgSourcePeak(player) > 0.001f;
                Thread.Sleep(20);
            }
            Assert.True(soundedAgain, "after the auto-stop, Play re-sends the WSG request and it sounds again");
        }
    }
}
