// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System.Linq;
using System.Text.Json;
using Chipestra.App.Settings;
using Chipestra.App.ViewModels;
using Chipestra.Core;
using Xunit;

namespace Chipestra.App.Tests
{
    public class OutputRateSettingsTests
    {
        [Fact]
        public void AppSettings_DefaultsToAuto()
        {
            Assert.Equal(0u, new AppSettings().OutputRateHz);
        }

        [Fact]
        public void AppSettings_JsonWithoutTheField_LoadsAsAuto()
        {
            // A settings.json that has no output-rate fields.
            AppSettings? loaded = JsonSerializer.Deserialize<AppSettings>("{\"RomRoot\":\"X\",\"OutputKind\":1}");
            Assert.NotNull(loaded);
            Assert.Equal(0u, loaded!.OutputRateHz);
        }

        [Theory]
        [InlineData(0u, 0u)]
        [InlineData(44100u, 44100u)]
        [InlineData(48000u, 48000u)]
        [InlineData(192000u, 192000u)]
        [InlineData(12345u, 0u)]
        [InlineData(32000u, 0u)]
        public void Normalize_KeepsOnlySelectableRates(uint input, uint expected)
        {
            Assert.Equal(expected, OutputRateOptions.Normalize(input));
        }

        [Fact]
        public void Options_StartWithAuto_AndMatchTheNativeAcceptedRates()
        {
            Assert.Equal(0u, OutputRateOptions.All[0].Hz);
            Assert.Equal(new uint[] { 0, 44100, 48000, 88200, 96000, 176400, 192000 }, OutputRateOptions.All.Select(o => o.Hz).ToArray());
        }

        [Fact]
        public void SettingsViewModel_RoundTripsRate_AndSanitizesStrayValues()
        {
            var vm = new SettingsViewModel(new AppSettings { OutputRateHz = 48000 });
            Assert.Equal(48000u, vm.OutputRateHz);
            vm.OutputRateHz = 96000;
            Assert.Equal(96000u, vm.ToAppSettings().OutputRateHz);

            var stray = new SettingsViewModel(new AppSettings { OutputRateHz = 12345 });
            Assert.Equal(0u, stray.OutputRateHz);
            Assert.Contains("Windows sound settings", stray.OutputRateNote);
        }

        private static AudioOutputFormat Format(uint requested, uint endpoint, uint honored) => new()
        {
            RequestedRateHz = requested,
            EndpointRateHz = endpoint,
            BitsPerSample = 32,
            ValidBits = 32,
            Channels = 2,
            SampleFormat = SampleFormat.Float,
            SelectionHonoredRaw = honored,
        };

        [Fact]
        public void DescribeOutput_HonoredSelection_ShowsOnlyTheActualFormat()
        {
            Assert.Equal("48000 Hz / 32bit float / 2ch", OutputMessages.DescribeOutput(Format(0, 48000, 1)));
        }

        [Fact]
        public void DescribeOutput_IgnoredSelection_ExplainsWindowsWins()
        {
            string text = OutputMessages.DescribeOutput(Format(44100, 48000, 0));
            Assert.Contains("48000 Hz", text);
            Assert.Contains("(44100 Hz not applied)", text);
        }

        [Fact]
        public void DescribeOutputFailure_UnsupportedFormat_NamesRateDepthAndSampleFormat()
        {
            var rejected = new AudioOutputFormat
            {
                EndpointRateHz = 44100,
                BitsPerSample = 24,
                Channels = 2,
                SampleFormat = SampleFormat.Pcm,
            };
            string text = OutputMessages.DescribeOutputFailure(ChipestraResult.UnsupportedFormat, rejected);
            Assert.Contains("44100 Hz", text);
            Assert.Contains("24bit PCM", text);
            Assert.Contains("32bit float or 16bit PCM", text);
        }

        [Fact]
        public void DescribeOutputFailure_OtherFailure_FallsBackToTheResultName()
        {
            string text = OutputMessages.DescribeOutputFailure(ChipestraResult.Internal, null);
            Assert.Contains("Internal", text);
            Assert.DoesNotContain("mix format", text);
        }
    }
}
