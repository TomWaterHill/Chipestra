// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using System.IO;
using Chipestra.Core;
using Xunit;

namespace Chipestra.Core.Tests
{
    // Confirms GameDefinitionXml reads the repo's real xml/afterburn2.xml (as
    // Player.FromDefinitionsDirectory would), not just the synthetic fixtures in
    // GameDefinitionXmlTests. No ROM data is read.
    public class GameDefinitionXmlAfterburner2Tests
    {
        // Walk up from the test binary's directory looking for xml/afterburn2.xml;
        // dotnet test does not reliably run with cwd == repo root.
        private static string? FindAfterburner2Xml()
        {
            var dir = new DirectoryInfo(AppContext.BaseDirectory);
            for (int i = 0; i < 8 && dir != null; i++, dir = dir.Parent)
            {
                string candidate = Path.Combine(dir.FullName, "xml", "afterburn2.xml");
                if (File.Exists(candidate))
                {
                    return candidate;
                }
            }
            return null;
        }

        [Fact]
        public void Load_Afterburn2Xml_ParsesSixBgmTracks()
        {
            string? path = FindAfterburner2Xml();
            if (path == null)
            {
                return;  // SKIP: not found from this test run's directory tree.
            }

            GameDefinition def = GameDefinitionXml.Load(path);

            Assert.Equal("AFTERBURNER2", def.GameId);
            Assert.Equal("SEGA_XBOARD", def.MachineId);
            Assert.Equal("XBOARD_BASE", def.VariantId);
            Assert.Equal(4, def.RequiredResources.Count);
            // The track list in afterburn2.xml: 6 BGM, 15 Voice/SFX, and 1 STOP control track.
            Assert.Equal(22, def.Tracks.Count);

            // The six BGM command bytes must each be present as an ordinary (non-control)
            // track with a non-empty Id and title. Checked by content, not by position.
            byte[] expectedCommands = { 0x91, 0x92, 0x93, 0x94, 0x95, 0x96 };
            foreach (byte command in expectedCommands)
            {
                TrackDefinition track = Assert.Single(def.Tracks, t => t.Command == command);
                Assert.False(track.IsControlCommand);
                Assert.False(string.IsNullOrWhiteSpace(track.Id));
                Assert.False(string.IsNullOrWhiteSpace(track.DisplayName));
            }

            // Exactly one STOP: command 0x00, type="control".
            TrackDefinition stop = Assert.Single(def.Tracks, t => t.Command == 0x00);
            Assert.True(stop.IsControlCommand);
        }

        [Fact]
        public void Load_Afterburn2Xml_HasArchiveScope()
        {
            string? path = FindAfterburner2Xml();
            if (path == null)
            {
                return;  // SKIP: not found from this test run's directory tree.
            }

            GameDefinition def = GameDefinitionXml.Load(path);

            // archive="aburner2" scopes ROM resolution when a ROM Root holds more than one
            // After Burner II set.
            Assert.Equal("aburner2", def.Archive);
        }
    }
}
