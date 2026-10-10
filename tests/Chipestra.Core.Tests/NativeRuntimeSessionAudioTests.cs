// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System.Threading;
using Chipestra.Core;
using Xunit;

namespace Chipestra.Core.Tests
{
    public class NativeRuntimeSessionAudioTests
    {
        private static NativeRuntimeSession CreateReadySession()
        {
            var session = new NativeRuntimeSession();
            Assert.Equal(ChipestraResult.Ok, session.LoadMachine("TEST_MACHINE", "STANDARD"));
            Assert.Equal(ChipestraResult.Ok, session.SelectNullOutput());
            Assert.Equal(ChipestraResult.Ok, session.StartAudio());
            return session;
        }

        // PlaybackPump renders asynchronously on its own thread, so progress has to be
        // observed by polling diagnostics with a bounded wait rather than a single call.
        private static bool WaitForSubmittedFrames(NativeRuntimeSession session, ulong minFrames, int timeoutMs)
        {
            const int pollIntervalMs = 5;
            for (int waited = 0; waited <= timeoutMs; waited += pollIntervalMs)
            {
                if (session.GetAudioOutputDiagnostics(out AudioOutputDiagnostics diagnostics) == ChipestraResult.Ok &&
                    diagnostics.SubmittedFrameCount >= minFrames)
                {
                    return true;
                }
                Thread.Sleep(pollIntervalMs);
            }
            return false;
        }

        [Fact]
        public void StartPlayback_Succeeds_AndRenders()
        {
            using NativeRuntimeSession session = CreateReadySession();

            Assert.Equal(ChipestraResult.Ok, session.StartPlayback());
            Assert.True(WaitForSubmittedFrames(session, 1, 1000));

            Assert.Equal(ChipestraResult.Ok, session.StopPlayback());
        }

        [Fact]
        public void StartPlayback_BeforeReady_ReturnsInvalidState()
        {
            using var session = new NativeRuntimeSession();
            Assert.Equal(ChipestraResult.Ok, session.LoadMachine("TEST_MACHINE", "STANDARD"));

            ChipestraResult result = session.StartPlayback();

            Assert.Equal(ChipestraResult.InvalidState, result);
        }

        [Fact]
        public void StartAudio_BeforeSelectNullOutput_ReturnsInvalidState()
        {
            using var session = new NativeRuntimeSession();
            Assert.Equal(ChipestraResult.Ok, session.LoadMachine("TEST_MACHINE", "STANDARD"));

            ChipestraResult result = session.StartAudio();

            Assert.Equal(ChipestraResult.InvalidState, result);
        }

        [Fact]
        public void PausePlayback_StopsSubmittingFrames_ThenResumeContinues()
        {
            using NativeRuntimeSession session = CreateReadySession();
            Assert.Equal(ChipestraResult.Ok, session.StartPlayback());
            Assert.True(WaitForSubmittedFrames(session, 1, 1000));

            Assert.Equal(ChipestraResult.Ok, session.PausePlayback());
            // PausePlayback() only signals the worker; a render already in
            // flight at that exact moment still completes (at most one
            // block's worth). Let that settle before taking the baseline, so
            // the two snapshots below are both unambiguously "after pause".
            Thread.Sleep(50);
            session.GetAudioOutputDiagnostics(out AudioOutputDiagnostics pausedDiagnostics);
            Thread.Sleep(50);
            session.GetAudioOutputDiagnostics(out AudioOutputDiagnostics stillPausedDiagnostics);
            Assert.Equal(pausedDiagnostics.SubmittedFrameCount, stillPausedDiagnostics.SubmittedFrameCount);

            Assert.Equal(ChipestraResult.Ok, session.ResumePlayback());
            Assert.True(WaitForSubmittedFrames(session, pausedDiagnostics.SubmittedFrameCount + 1, 1000));

            Assert.Equal(ChipestraResult.Ok, session.StopPlayback());
        }

        [Fact]
        public void StopAudio_BeforeStoppingPlayback_ReturnsInvalidState()
        {
            using NativeRuntimeSession session = CreateReadySession();
            Assert.Equal(ChipestraResult.Ok, session.StartPlayback());
            Assert.True(WaitForSubmittedFrames(session, 1, 1000));

            ChipestraResult result = session.StopAudio();

            Assert.Equal(ChipestraResult.InvalidState, result);

            session.StopPlayback();
        }

        [Fact]
        public void RepeatedStartStopPlaybackCycles_AllSucceed()
        {
            using var session = new NativeRuntimeSession();
            Assert.Equal(ChipestraResult.Ok, session.LoadMachine("TEST_MACHINE", "STANDARD"));
            Assert.Equal(ChipestraResult.Ok, session.SelectNullOutput());
            Assert.Equal(ChipestraResult.Ok, session.StartAudio());

            for (int i = 0; i < 3; i++)
            {
                Assert.Equal(ChipestraResult.Ok, session.StartPlayback());
                Assert.True(WaitForSubmittedFrames(session, 1, 1000));
                Assert.Equal(ChipestraResult.Ok, session.StopPlayback());
            }

            Assert.Equal(ChipestraResult.Ok, session.StopAudio());
        }
    }
}
