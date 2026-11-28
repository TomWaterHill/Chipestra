// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System.Text.Json;
using Chipestra.App.Settings;
using Chipestra.App.ViewModels;
using Xunit;

namespace Chipestra.App.Tests
{
    public class LevelMatchingSettingsTests
    {
        [Fact]
        public void Default_IsOff()
        {
            Assert.False(new AppSettings().LevelMatching);
        }

        [Theory]
        [InlineData(true)]
        [InlineData(false)]
        public void SaveAndLoad_RoundTrips(bool enabled)
        {
            string json = JsonSerializer.Serialize(new AppSettings { RomRoot = "X", LevelMatching = enabled });
            AppSettings? loaded = JsonSerializer.Deserialize<AppSettings>(json);
            Assert.NotNull(loaded);
            Assert.Equal(enabled, loaded!.LevelMatching);
            Assert.Equal("X", loaded.RomRoot);
        }

        [Fact]
        public void JsonWithoutTheField_LoadsAsOff()
        {
            AppSettings? loaded = JsonSerializer.Deserialize<AppSettings>("{\"RomRoot\":\"X\"}");
            Assert.NotNull(loaded);
            Assert.False(loaded!.LevelMatching);
        }

        [Fact]
        public void SettingsViewModel_RoundTripsTheChoice_AndKeepsTheOtherSettings()
        {
            var vm = new SettingsViewModel(new AppSettings { LevelMatching = true, OutputRateHz = 48000 });
            Assert.True(vm.LevelMatching);
            vm.LevelMatching = false;
            AppSettings result = vm.ToAppSettings();
            Assert.False(result.LevelMatching);
            Assert.Equal(48000u, result.OutputRateHz);
        }
    }
}
