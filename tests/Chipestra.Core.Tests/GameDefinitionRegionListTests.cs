// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using System.IO;
using System.Linq;
using Chipestra.Core;
using Xunit;

namespace Chipestra.Core.Tests
{
    public class GameDefinitionRegionListTests
    {
        private static string Xml(string region, string warmup) => $@"<chipestra>
  <game id=""tt"" title=""Time Trax"" machine=""SEGA_MEGADRIVE"" variant=""MEGADRIVE_BASE"" manufacturer=""Malibu"">
    <resources><rom name=""rom"" file=""a.bin""/></resources>
    <driver music=""0:16"" release_cycle=""100"" warmup_frames=""{warmup}"" region=""{region}""/>
    <tracks><track id=""1"" command=""0x01"" title=""Song""/></tracks>
  </game>
</chipestra>";

        [Fact]
        public void ParseAll_AGameWithTwoRegionsBecomesTwoGamesWithTheSameTracks()
        {
            var games = GameDefinitionXml.ParseAll(Xml("usa|europe", "784|782"));
            Assert.Equal(2, games.Count);
            Assert.Equal(new[] { "tt@usa", "tt@europe" }, games.Select(g => g.GameId));
            Assert.Equal(new[] { "Time Trax (60Hz)", "Time Trax (50Hz)" }, games.Select(g => g.Title));
            Assert.Contains("region=\"usa\"", games[0].DriverXml);
            Assert.Contains("warmup_frames=\"784\"", games[0].DriverXml);
            Assert.Contains("region=\"europe\"", games[1].DriverXml);
            Assert.Contains("warmup_frames=\"782\"", games[1].DriverXml);
            Assert.Same(games[0].Tracks, games[1].Tracks);
            Assert.Equal("Genesis (North America)", games[0].SystemName("Mega Drive"));
            Assert.Equal("Mega Drive (Europe)", games[1].SystemName("Mega Drive"));
        }

        [Fact]
        public void ParseAll_OneValueServesAllRegions()
        {
            var games = GameDefinitionXml.ParseAll(Xml("usa|europe", "5"));
            Assert.All(games, g => Assert.Contains("warmup_frames=\"5\"", g.DriverXml));
        }

        [Fact]
        public void ParseAll_AListThatDoesNotFitIsAnError()
        {
            Assert.Throws<FormatException>(() => GameDefinitionXml.ParseAll(Xml("usa|europe", "1|2|3")));
        }

        [Fact]
        public void ParseAll_ASingleRegionIsLeftAlone()
        {
            var games = GameDefinitionXml.ParseAll(Xml("japan", "104"));
            Assert.Single(games);
            Assert.Equal("tt", games[0].GameId);
            Assert.Equal("Time Trax", games[0].Title);
        }

        [Fact]
        public void LoadDirectory_ListsTheRegionsOfTimeTraxAsTwoGames()
        {
            string? root = AppContext.BaseDirectory;
            string? xml = null;
            for (int i = 0; i < 10 && root != null; i++, root = Path.GetDirectoryName(root))
                if (Directory.Exists(Path.Combine(root, "xml")) && Directory.Exists(Path.Combine(root, "src"))) { xml = Path.Combine(root, "xml"); break; }
            if (xml == null) return;
            var result = GameDefinitionCatalog.LoadDirectory(xml);
            Assert.Contains("timetrax_proto@usa", result.Catalog.Keys);
            Assert.Contains("timetrax_proto@europe", result.Catalog.Keys);
            Assert.DoesNotContain("timetrax_proto", result.Catalog.Keys);
        }
    }
}
