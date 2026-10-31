// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System.Collections.Generic;
using Chipestra.App.ViewModels;
using Xunit;

namespace Chipestra.App.Tests
{
    public class ChannelDisplayNameFormatterTests
    {
        [Fact]
        public void Format_Rp2a03Channels_ShowTheChipOnceWithPulseNumbers()
        {
            var raw = new List<(string, string)>
            {
                ("RP2A03", "Pulse 1"),
                ("RP2A03", "Pulse 2"),
                ("RP2A03", "Triangle"),
                ("RP2A03", "Noise"),
                ("RP2A03", "DMC"),
            };

            var shown = ChannelDisplayNameFormatter.Format(raw);

            Assert.Equal(5, shown.Count);
            Assert.All(shown, s => Assert.Equal("RP2A03", s.DeviceDisplay));
            Assert.Equal("Pulse 1ch", shown[0].ChannelDisplay);
            Assert.Equal("Pulse 2ch", shown[1].ChannelDisplay);
            Assert.Equal("Triangle 1ch", shown[2].ChannelDisplay);
            Assert.Equal("Noise 1ch", shown[3].ChannelDisplay);
            Assert.Equal("DMC 1ch", shown[4].ChannelDisplay);
        }
    }
}
