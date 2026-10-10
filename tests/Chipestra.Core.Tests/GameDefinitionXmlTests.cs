// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using System.IO;
using System.Linq;
using Chipestra.Core;
using Xunit;

namespace Chipestra.Core.Tests
{
    // GameDefinitionXml is Managed Core's thin wrapper over Native's XML parser via the
    // C ABI. These tests exercise the wrapper's public contract (field mapping,
    // exceptions), not the parsing itself (covered by the native tests). Fixtures are
    // inline XML strings rather than the shipped xml/ files, which can change shape.
    public class GameDefinitionXmlTests
    {
        private const string ValidXml =
            "<chipestra>" +
            "<game id=\"TESTGAME\" title=\"Test Game\" machine=\"SEGA_XBOARD\" variant=\"XBOARD_BASE\" manufacturer=\"Sega\">" +
            "<resources>" +
            "<rom name=\"soundcpu\" file=\"epr-1.bin\" size=\"65536\" crc32=\"d777fc6d\"/>" +
            "<rom name=\"pcm1\" file=\"mpr-1.bin\"/>" +
            "</resources>" +
            "<tracks>" +
            "<track id=\"T1\" command=\"0x91\" title=\"Opening\"/>" +
            "<track id=\"T2\" command=\"146\" title=\"After Burner\"/>" +
            "</tracks>" +
            "</game>" +
            "</chipestra>";

        [Fact]
        public void Parse_ValidXml_ReadsGameFields()
        {
            GameDefinition def = GameDefinitionXml.Parse(ValidXml);

            Assert.Equal("TESTGAME", def.GameId);
            Assert.Equal("Test Game", def.Title);
            Assert.Equal("SEGA_XBOARD", def.MachineId);
            Assert.Equal("XBOARD_BASE", def.VariantId);
            Assert.Equal("Sega", def.Manufacturer);
        }

        [Fact]
        public void Parse_ValidXml_ReadsResources()
        {
            GameDefinition def = GameDefinitionXml.Parse(ValidXml);

            Assert.Equal(2, def.RequiredResources.Count);
            Assert.Equal("soundcpu", def.RequiredResources[0].Name);
            Assert.Equal("epr-1.bin", def.RequiredResources[0].FileName);
            Assert.Equal(65536, def.RequiredResources[0].SizeBytes);
            Assert.Equal("d777fc6d", def.RequiredResources[0].Crc32);

            // size/crc32 are optional per the schema (game_definition.h).
            Assert.Null(def.RequiredResources[1].SizeBytes);
            Assert.Null(def.RequiredResources[1].Crc32);
        }

        [Fact]
        public void Parse_ValidXml_ReadsTracksAndAcceptsHexOrDecimalCommand()
        {
            GameDefinition def = GameDefinitionXml.Parse(ValidXml);

            Assert.Equal(2, def.Tracks.Count);
            Assert.Equal("T1", def.Tracks[0].Id);
            Assert.Equal("Opening", def.Tracks[0].DisplayName);
            Assert.Equal((byte)0x91, def.Tracks[0].Command);

            Assert.Equal("T2", def.Tracks[1].Id);
            Assert.Equal((byte)146, def.Tracks[1].Command);
        }

        [Fact]
        public void Parse_TrackWithoutTypeOrLoops_DefaultsToNotControlAndLoops()
        {
            // ValidXml's two tracks have neither attribute: not an error, and they must
            // round-trip through the C ABI (is_control_command/loops) as the documented defaults.
            GameDefinition def = GameDefinitionXml.Parse(ValidXml);

            Assert.False(def.Tracks[0].IsControlCommand);
            Assert.True(def.Tracks[0].Loops);
            Assert.False(def.Tracks[1].IsControlCommand);
            Assert.True(def.Tracks[1].Loops);
        }

        [Fact]
        public void Parse_TrackTypeControl_SetsIsControlCommand()
        {
            const string xml =
                "<chipestra><game id=\"G\" title=\"G\" machine=\"SEGA_XBOARD\" variant=\"XBOARD_BASE\">" +
                "<tracks>" +
                "<track id=\"T1\" command=\"0x00\" title=\"Stop\" type=\"control\"/>" +
                "<track id=\"T2\" command=\"1\" title=\"Song\"/>" +
                "</tracks>" +
                "</game></chipestra>";

            GameDefinition def = GameDefinitionXml.Parse(xml);

            Assert.True(def.Tracks[0].IsControlCommand);
            Assert.False(def.Tracks[1].IsControlCommand);
        }

        [Fact]
        public void Parse_TrackLoopsFalse_SetsLoopsFalse()
        {
            const string xml =
                "<chipestra><game id=\"G\" title=\"G\" machine=\"SEGA_XBOARD\" variant=\"XBOARD_BASE\">" +
                "<tracks>" +
                "<track id=\"T1\" command=\"1\" title=\"One-shot\" loops=\"false\"/>" +
                "<track id=\"T2\" command=\"2\" title=\"BGM\"/>" +
                "</tracks>" +
                "</game></chipestra>";

            GameDefinition def = GameDefinitionXml.Parse(xml);

            Assert.False(def.Tracks[0].Loops);
            Assert.True(def.Tracks[1].Loops);
        }

        [Fact]
        public void Parse_NonAsciiTitles_DecodeAsUtf8NotMojibake()
        {
            // Titles are UTF-8 on the native side; they must not be decoded with the OS
            // system codepage (mojibake for non-ASCII titles).
            const string xml =
                "<chipestra>" +
                "<game id=\"G\" title=\"演奏停止\" machine=\"SEGA_XBOARD\" variant=\"XBOARD_BASE\">" +
                "<tracks>" +
                "<track id=\"T1\" command=\"0x00\" title=\"演奏停止 (Stop)\"/>" +
                "</tracks>" +
                "</game>" +
                "</chipestra>";

            GameDefinition def = GameDefinitionXml.Parse(xml);

            Assert.Equal("演奏停止", def.Title);
            Assert.Equal("演奏停止 (Stop)", def.Tracks[0].DisplayName);
        }

        [Fact]
        public void Parse_MissingGameElement_Throws()
        {
            Assert.Throws<FormatException>(() => GameDefinitionXml.Parse("<chipestra></chipestra>"));
        }

        [Fact]
        public void Parse_TrackMissingId_Throws()
        {
            const string xml =
                "<chipestra><game id=\"G\" title=\"G\" machine=\"M\" variant=\"V\">" +
                "<tracks><track command=\"1\" title=\"No Id\"/></tracks>" +
                "</game></chipestra>";

            Assert.Throws<FormatException>(() => GameDefinitionXml.Parse(xml));
        }

        [Fact]
        public void Load_ReadsFromDisk()
        {
            string path = Path.Combine(Path.GetTempPath(), Path.GetRandomFileName() + ".xml");
            try
            {
                File.WriteAllText(path, ValidXml);
                GameDefinition def = GameDefinitionXml.Load(path);
                Assert.Equal("TESTGAME", def.GameId);
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Fact]
        public void LoadDirectory_IndexesEveryFileByGameId()
        {
            string dir = Path.Combine(Path.GetTempPath(), Path.GetRandomFileName());
            Directory.CreateDirectory(dir);
            try
            {
                File.WriteAllText(Path.Combine(dir, "a.xml"), ValidXml);
                File.WriteAllText(
                    Path.Combine(dir, "b.xml"),
                    ValidXml.Replace("TESTGAME", "OTHERGAME", StringComparison.Ordinal));

                var result = GameDefinitionCatalog.LoadDirectory(dir);
                var catalog = result.Catalog;

                Assert.Equal(2, catalog.Count);
                Assert.True(catalog.ContainsKey("TESTGAME"));
                Assert.True(catalog.ContainsKey("OTHERGAME"));
            }
            finally
            {
                Directory.Delete(dir, recursive: true);
            }
        }

        private static string NewTempDir()
        {
            string dir = Path.Combine(Path.GetTempPath(), Path.GetRandomFileName());
            Directory.CreateDirectory(dir);
            return dir;
        }

        [Fact]
        public void LoadDirectory_BrokenFilesAreReportedAndTheRestStillLoad()
        {
            string dir = NewTempDir();
            try
            {
                File.WriteAllText(Path.Combine(dir, "good.xml"), ValidXml);
                File.WriteAllText(Path.Combine(dir, "notxml.xml"), "<chipestra><game");
                File.WriteAllText(
                    Path.Combine(dir, "nomachine.xml"),
                    ValidXml.Replace("TESTGAME", "NOMACHINE", StringComparison.Ordinal)
                            .Replace("machine=\"SEGA_XBOARD\" ", "", StringComparison.Ordinal));
                File.WriteAllText(
                    Path.Combine(dir, "badcommand.xml"),
                    ValidXml.Replace("TESTGAME", "BADCOMMAND", StringComparison.Ordinal)
                            .Replace("command=\"0x91\"", "command=\"0x1FF\"", StringComparison.Ordinal));

                var result = GameDefinitionCatalog.LoadDirectory(dir);

                Assert.Single(result.Catalog);
                Assert.True(result.Catalog.ContainsKey("TESTGAME"));
                Assert.Equal(
                    new[] { "badcommand.xml", "nomachine.xml", "notxml.xml" },
                    result.Failures.Select(f => f.FileName).ToArray());
                Assert.All(result.Failures, f => Assert.False(string.IsNullOrWhiteSpace(f.Reason)));
            }
            finally
            {
                Directory.Delete(dir, recursive: true);
            }
        }

        [Fact]
        public void LoadDirectory_DuplicateGameId_KeepsTheFirstFileByNameAndReportsTheLater()
        {
            string dir = NewTempDir();
            try
            {
                // Same game id in both; different titles show which one survived.
                File.WriteAllText(Path.Combine(dir, "b_second.xml"), ValidXml.Replace("Test Game", "Second", StringComparison.Ordinal));
                File.WriteAllText(Path.Combine(dir, "a_first.xml"), ValidXml.Replace("Test Game", "First", StringComparison.Ordinal));

                var result = GameDefinitionCatalog.LoadDirectory(dir);

                Assert.Equal("First", result.Catalog["TESTGAME"].Title);
                GameDefinitionLoadFailure failure = Assert.Single(result.Failures);
                Assert.Equal("b_second.xml", failure.FileName);
                Assert.Contains("a_first.xml", failure.Reason);
            }
            finally
            {
                Directory.Delete(dir, recursive: true);
            }
        }

        [Fact]
        public void LoadDirectory_ReadsFilesInNameOrder()
        {
            string dir = NewTempDir();
            try
            {
                // Created in reverse name order; failures are listed in read order.
                foreach (string name in new[] { "z.xml", "m.xml", "a.xml" })
                {
                    File.WriteAllText(Path.Combine(dir, name), "not xml at all");
                }

                var result = GameDefinitionCatalog.LoadDirectory(dir);

                Assert.Equal(new[] { "a.xml", "m.xml", "z.xml" }, result.Failures.Select(f => f.FileName).ToArray());
            }
            finally
            {
                Directory.Delete(dir, recursive: true);
            }
        }

        [Fact]
        public void LoadDirectory_EmptyOrMissingDirectory_ReturnsEmptyCatalogWithoutThrowing()
        {
            string dir = NewTempDir();
            try
            {
                var empty = GameDefinitionCatalog.LoadDirectory(dir);
                Assert.Empty(empty.Catalog);
                Assert.Empty(empty.Failures);
            }
            finally
            {
                Directory.Delete(dir, recursive: true);
            }

            var missing = GameDefinitionCatalog.LoadDirectory(Path.Combine(dir, "nope"));
            Assert.Empty(missing.Catalog);
            Assert.Single(missing.Failures);
        }

        [Fact]
        public void LoadDirectory_RepositoryXmlFolder_LoadsWithoutAnyError()
        {
            var dir = new DirectoryInfo(AppContext.BaseDirectory);
            while (dir != null && !Directory.Exists(Path.Combine(dir.FullName, "xml")))
            {
                dir = dir.Parent;
            }
            Assert.NotNull(dir);

            var result = GameDefinitionCatalog.LoadDirectory(Path.Combine(dir!.FullName, "xml"));

            Assert.True(
                result.Failures.Count == 0,
                string.Join("; ", result.Failures.Select(f => $"{f.FileName}: {f.Reason}")));
            Assert.NotEmpty(result.Catalog);
        }

        private static string XmlWithTrackType(string typeAttribute) =>
            "<chipestra><game id=\"G\" title=\"G\" machine=\"SEGA_XBOARD\" variant=\"XBOARD_BASE\">" +
            "<resources><rom name=\"soundcpu\" file=\"a.bin\"/></resources><tracks>" +
            "<track id=\"T\" command=\"0x05\" title=\"t\" " + typeAttribute + "/>" +
            "</tracks></game></chipestra>";

        [Fact]
        public void Parse_TrackTypeWsg_SetsIsWsgAndTheWsgCommandKind()
        {
            TrackDefinition wsg = GameDefinitionXml.Parse(XmlWithTrackType("type=\"wsg\"")).Tracks[0];
            Assert.True(wsg.IsWsg);
            Assert.False(wsg.IsControlCommand);
            Assert.Equal(MusicCommandKind.Wsg, wsg.CommandKind);

            TrackDefinition control = GameDefinitionXml.Parse(XmlWithTrackType("type=\"control\"")).Tracks[0];
            Assert.True(control.IsControlCommand);
            Assert.False(control.IsWsg);
            Assert.Equal(MusicCommandKind.Raw, control.CommandKind);

            TrackDefinition plain = GameDefinitionXml.Parse(XmlWithTrackType("")).Tracks[0];
            Assert.False(plain.IsWsg);
            Assert.False(plain.IsControlCommand);
            Assert.Equal(MusicCommandKind.Raw, plain.CommandKind);
        }

        [Theory]
        [InlineData("Control")]
        [InlineData("WSG")]
        [InlineData("psg")]
        [InlineData("")]
        public void Parse_UnknownTrackType_IsAnErrorNamingTheValue(string value)
        {
            FormatException ex = Assert.Throws<FormatException>(
                () => GameDefinitionXml.Parse(XmlWithTrackType("type=\"" + value + "\"")));
            Assert.Contains("unknown type", ex.Message);
        }

        [Fact]
        public void LoadDirectory_UnknownTrackType_OnlyThatFileFails()
        {
            string dir = NewTempDir();
            try
            {
                File.WriteAllText(Path.Combine(dir, "good.xml"), ValidXml);
                File.WriteAllText(Path.Combine(dir, "typo.xml"), XmlWithTrackType("type=\"psg\"").Replace("id=\"G\"", "id=\"TYPO\""));

                var result = GameDefinitionCatalog.LoadDirectory(dir);

                Assert.True(result.Catalog.ContainsKey("TESTGAME"));
                GameDefinitionLoadFailure failure = Assert.Single(result.Failures);
                Assert.Equal("typo.xml", failure.FileName);
                Assert.Contains("unknown type", failure.Reason);
            }
            finally
            {
                Directory.Delete(dir, recursive: true);
            }
        }

        [Fact]
        public void RepositoryXml_WsgTracksExistOnlyInDragonSpiritAndStayWithinTheSoundTestRange()
        {
            var dir = new DirectoryInfo(AppContext.BaseDirectory);
            while (dir != null && !Directory.Exists(Path.Combine(dir.FullName, "xml")))
            {
                dir = dir.Parent;
            }
            Assert.NotNull(dir);

            var result = GameDefinitionCatalog.LoadDirectory(Path.Combine(dir!.FullName, "xml"));
            Assert.Empty(result.Failures);

            int wsgTracks = 0;
            foreach (GameDefinition game in result.Catalog.Values)
            {
                foreach (TrackDefinition track in game.Tracks.Where(t => t.IsWsg))
                {
                    wsgTracks++;
                    Assert.Equal("NAMCO_SYSTEM1", game.MachineId);
                    Assert.InRange(track.Command, (byte)0x00, (byte)0x24);
                    // 0x0E is a looping effect: it is the only WSG track without loops="false".
                    Assert.Equal(track.Command == 0x0E, track.Loops);
                    Assert.False(track.IsControlCommand);
                }
            }
            Assert.Equal(31, wsgTracks);
        }
    }
}
