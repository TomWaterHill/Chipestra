// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using Chipestra.Core;
using Xunit;

namespace Chipestra.Core.Tests
{
    public class ChipestraRuntimeTests
    {
        [Fact]
        public void GetApiVersion_ReturnsExpectedVersion()
        {
            // Must match kApiVersion in chipestra_abi.h; bump both on a breaking ABI change.
            uint expected = 4u;
            uint actual = ChipestraRuntime.GetApiVersion();
            Assert.Equal(expected, actual);
            Assert.Equal(ChipestraRuntime.ExpectedApiVersion, expected);
        }

        [Fact]
        public void EnsureCompatibleNativeVersion_DoesNotThrow_WhenDllMatches()
        {
            ChipestraRuntime.EnsureCompatibleNativeVersion();
        }

        [Fact]
        public void NativeVersionMismatchException_MessageNamesBothVersions()
        {
            var mismatch = new NativeVersionMismatchException(3, 2);
            Assert.Contains("version 2", mismatch.Message);
            Assert.Contains("version 3", mismatch.Message);
            Assert.Equal(3u, mismatch.Expected);
            Assert.Equal(2u, mismatch.Actual);

            var missing = new NativeVersionMismatchException(3, null);
            Assert.Null(missing.Actual);
            Assert.Contains("does not report", missing.Message);
        }

        [Fact]
        public void SelectWasapiSharedOutput_UnsupportedRate_ReturnsInvalidArgument()
        {
            using var session = new NativeRuntimeSession();
            Assert.Equal(ChipestraResult.InvalidArgument, session.SelectWasapiSharedOutput(12345));
        }

        [Fact]
        public void GetAudioOutputFormat_WithNoOutputSelected_ReturnsInvalidState()
        {
            using var session = new NativeRuntimeSession();
            Assert.Equal(ChipestraResult.InvalidState, session.GetAudioOutputFormat(out _));
        }

        [Fact]
        public void AudioOutputFormat_Describe_FormatsRateDepthKindChannels()
        {
            var format = new AudioOutputFormat
            {
                EndpointRateHz = 48000,
                BitsPerSample = 32,
                Channels = 2,
                SampleFormat = SampleFormat.Float,
            };
            Assert.Equal("48000 Hz / 32bit float / 2ch", format.Describe());
        }
    }
}
