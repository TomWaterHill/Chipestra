// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using Chipestra.Core;
using Xunit;

namespace Chipestra.Core.Tests
{
    public class GameDefinitionRegionTests
    {
        private static GameDefinition Game(string? region) => new()
        {
            GameId = "g",
            Title = "G",
            MachineId = "SEGA_MEGADRIVE",
            VariantId = "MEGADRIVE_BASE",
            Region = region,
            RequiredResources = Array.Empty<RomResource>(),
            Tracks = Array.Empty<TrackDefinition>(),
        };

        [Theory]
        [InlineData("japan", "Mega Drive (Japan)")]
        [InlineData("usa", "Genesis (North America)")]
        [InlineData("europe", "Mega Drive (Europe)")]
        [InlineData(null, "Mega Drive (Japan)")]
        public void SystemName_FollowsTheRegion(string? region, string expected)
        {
            Assert.Equal(expected, Game(region).SystemName("Mega Drive"));
        }

        [Fact]
        public void SystemName_LeavesOtherConsolesAlone()
        {
            Assert.Equal("NES", Game("europe").SystemName("NES"));
        }
    }
}
