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
    public class AnalogFilterSettingsTests
    {
        [Fact]
        public void Default_IsOff()
        {
            Assert.Equal(AnalogFilterMode.Off, new AppSettings().MegaDriveAnalogFilter);
        }

        [Theory]
        [InlineData(AnalogFilterMode.Off)]
        [InlineData(AnalogFilterMode.Model1)]
        [InlineData(AnalogFilterMode.Model2)]
        public void SaveAndLoad_RoundTripsEveryMode(AnalogFilterMode mode)
        {
            string json = JsonSerializer.Serialize(new AppSettings { RomRoot = "X", MegaDriveAnalogFilter = mode });
            AppSettings? loaded = JsonSerializer.Deserialize<AppSettings>(json);
            Assert.NotNull(loaded);
            Assert.Equal(mode, loaded!.MegaDriveAnalogFilter);
            Assert.Equal("X", loaded.RomRoot);
        }

        [Fact]
        public void JsonWithoutTheField_LoadsAsOff()
        {
            AppSettings? loaded = JsonSerializer.Deserialize<AppSettings>("{\"RomRoot\":\"X\",\"OutputKind\":1}");
            Assert.NotNull(loaded);
            Assert.Equal(AnalogFilterMode.Off, loaded!.MegaDriveAnalogFilter);
        }

        [Theory]
        [InlineData("7")]
        [InlineData("-1")]
        [InlineData("\"Model1\"")]
        [InlineData("null")]
        [InlineData("{\"a\":1}")]
        [InlineData("1.5")]
        public void JsonWithAnUnusableValue_LoadsAsOff_AndKeepsTheOtherFields(string value)
        {
            AppSettings? loaded = JsonSerializer.Deserialize<AppSettings>("{\"RomRoot\":\"X\",\"MegaDriveAnalogFilter\":" + value + "}");
            Assert.NotNull(loaded);
            Assert.Equal(AnalogFilterMode.Off, loaded!.MegaDriveAnalogFilter);
            Assert.Equal("X", loaded.RomRoot);
        }

        [Fact]
        public void Options_AreOffModel1Model2_InThatOrder_WithTheDocumentedLabels()
        {
            Assert.Equal(new[] { AnalogFilterMode.Off, AnalogFilterMode.Model1, AnalogFilterMode.Model2 },
                         AnalogFilterOptions.All.Select(o => o.Mode).ToArray());
            Assert.Equal("Off", AnalogFilterOptions.All[0].Label);
            Assert.Equal("Model 1 (Mega Drive / Genesis)", AnalogFilterOptions.All[1].Label);
            Assert.Equal("Model 2 (Mega Drive 2 / Genesis Model 2)", AnalogFilterOptions.All[2].Label);
        }

        [Fact]
        public void SettingsViewModel_RoundTripsTheMode()
        {
            var vm = new SettingsViewModel(new AppSettings { MegaDriveAnalogFilter = AnalogFilterMode.Model2 });
            Assert.Equal(AnalogFilterMode.Model2, vm.MegaDriveAnalogFilter);
            vm.MegaDriveAnalogFilter = AnalogFilterMode.Model1;
            Assert.Equal(AnalogFilterMode.Model1, vm.ToAppSettings().MegaDriveAnalogFilter);
        }

        [Fact]
        public void SettingsViewModel_SanitizesAStrayValue()
        {
            var vm = new SettingsViewModel(new AppSettings { MegaDriveAnalogFilter = (AnalogFilterMode)9 });
            Assert.Equal(AnalogFilterMode.Off, vm.MegaDriveAnalogFilter);
        }
    }
}
