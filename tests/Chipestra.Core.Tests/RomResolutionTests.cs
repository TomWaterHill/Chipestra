// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using System.Collections.Generic;
using System.IO;
using Chipestra.Core;
using Xunit;

namespace Chipestra.Core.Tests
{
    // Player.LoadGame's ROM resolution wiring, using the native ResourceResolver via the
    // C ABI's chipestra_resolve_resources. Runs against TEST_MACHINE (a synthetic no-ROM
    // Machine whose factory ignores the resources it is given), so these prove the
    // Managed <-> Native plumbing without any real ROM; only synthetic placeholder bytes
    // are written to temp directories.
    public class RomResolutionTests
    {
        private static string MakeTempRomRoot(string testName)
        {
            string dir = Path.Combine(Path.GetTempPath(), "chipestra_romresolution_tests", testName);
            if (Directory.Exists(dir))
            {
                Directory.Delete(dir, recursive: true);
            }
            Directory.CreateDirectory(dir);
            return dir;
        }

        private static IReadOnlyDictionary<string, GameDefinition> CatalogWithResources(
            IReadOnlyList<RomResource> resources, string? archive = null)
        {
            var game = new GameDefinition
            {
                GameId = "TEST_GAME",
                Title = "Test Game",
                MachineId = "TEST_MACHINE",
                VariantId = "STANDARD",
                Archive = archive,
                RequiredResources = resources,
                Tracks = Array.Empty<TrackDefinition>(),
            };
            return new Dictionary<string, GameDefinition>(StringComparer.Ordinal) { { "TEST_GAME", game } };
        }

        [Fact]
        public void LoadGame_NoRomRoot_SkipsResolutionAndLoadsWithNoResources()
        {
            var catalog = CatalogWithResources(new[] { new RomResource { Name = "r1", FileName = "present.bin" } });
            using var player = new Player(catalog);

            Assert.Equal(ChipestraResult.Ok, player.LoadGame("TEST_GAME"));
            Assert.Null(player.RomResolution);
            Assert.Equal(PlaybackState.MachineLoaded, player.Status.State);
        }

        [Fact]
        public void LoadGame_AllResourcesPresent_ResolvesAndLoadsSuccessfully()
        {
            string root = MakeTempRomRoot("all_present");
            try
            {
                File.WriteAllBytes(Path.Combine(root, "present.bin"), new byte[] { 0xAB, 0xCD });
                var catalog = CatalogWithResources(new[] { new RomResource { Name = "r1", FileName = "present.bin" } });
                using var player = new Player(catalog);

                Assert.Equal(ChipestraResult.Ok, player.LoadGame("TEST_GAME", root));

                Assert.NotNull(player.RomResolution);
                Assert.True(player.RomResolution!.AllResolved);
                Assert.Equal(RomResolutionStatus.Resolved, player.RomResolution.Entries[0].Status);
                Assert.Equal(PlaybackState.MachineLoaded, player.Status.State);
            }
            finally
            {
                Directory.Delete(root, recursive: true);
            }
        }

        [Fact]
        public void LoadGame_NoResourcesDeclared_AllResolvedIsTriviallyTrue()
        {
            string root = MakeTempRomRoot("no_resources");
            try
            {
                var catalog = CatalogWithResources(Array.Empty<RomResource>());
                using var player = new Player(catalog);

                Assert.Equal(ChipestraResult.Ok, player.LoadGame("TEST_GAME", root));

                Assert.NotNull(player.RomResolution);
                Assert.Empty(player.RomResolution!.Entries);
                Assert.True(player.RomResolution.AllResolved);
            }
            finally
            {
                Directory.Delete(root, recursive: true);
            }
        }

        [Fact]
        public void LoadGame_MissingResource_FailsAndDoesNotLoadTheMachine()
        {
            string root = MakeTempRomRoot("missing");
            try
            {
                var catalog = CatalogWithResources(new[] { new RomResource { Name = "r1", FileName = "missing.bin" } });
                using var player = new Player(catalog);

                ChipestraResult result = player.LoadGame("TEST_GAME", root);

                Assert.Equal(ChipestraResult.NotFound, result);
                Assert.NotNull(player.RomResolution);
                Assert.False(player.RomResolution!.AllResolved);
                Assert.Equal(RomResolutionStatus.NotFound, player.RomResolution.Entries[0].Status);
                // A resolution failure must not silently load a Machine with no ROM data.
                Assert.Equal(PlaybackState.Created, player.Status.State);
            }
            finally
            {
                Directory.Delete(root, recursive: true);
            }
        }

        [Fact]
        public void LoadGame_AmbiguousResource_ReportsAmbiguousMatch()
        {
            string root = MakeTempRomRoot("ambiguous");
            try
            {
                Directory.CreateDirectory(Path.Combine(root, "d1"));
                Directory.CreateDirectory(Path.Combine(root, "d2"));
                File.WriteAllBytes(Path.Combine(root, "d1", "shared.bin"), new byte[] { 0x01, 0x02 });
                File.WriteAllBytes(Path.Combine(root, "d2", "shared.bin"), new byte[] { 0x03, 0x04, 0x05 });
                var catalog = CatalogWithResources(new[] { new RomResource { Name = "r1", FileName = "shared.bin" } });
                using var player = new Player(catalog);

                ChipestraResult result = player.LoadGame("TEST_GAME", root);

                Assert.Equal(ChipestraResult.AmbiguousMatch, result);
                Assert.Equal(RomResolutionStatus.AmbiguousMatch, player.RomResolution!.Entries[0].Status);
                Assert.Equal(PlaybackState.Created, player.Status.State);
            }
            finally
            {
                Directory.Delete(root, recursive: true);
            }
        }

        // Proves GameDefinition.Archive survives the full Managed round trip
        // (GameDefinitionXml -> Player.LoadGame -> NativeRuntimeSession.LoadMachineWithResources
        // -> chipestra_resolve_resources) that the GUI uses, with two differently-named folders.
        [Fact]
        public void LoadGame_ArchiveScope_ResolvesDespiteSameNamedFileInADifferentlyNamedFolder()
        {
            string root = MakeTempRomRoot("archive_scope_wiring");
            try
            {
                Directory.CreateDirectory(Path.Combine(root, "setA"));
                Directory.CreateDirectory(Path.Combine(root, "setB"));
                File.WriteAllBytes(Path.Combine(root, "setA", "shared.bin"), new byte[] { 0x01, 0x02 });
                File.WriteAllBytes(Path.Combine(root, "setB", "shared.bin"), new byte[] { 0x03, 0x04, 0x05 });
                var catalog = CatalogWithResources(
                    new[] { new RomResource { Name = "r1", FileName = "shared.bin" } }, archive: "setB");
                using var player = new Player(catalog);

                ChipestraResult result = player.LoadGame("TEST_GAME", root);

                // Without the archive scope this would be AmbiguousMatch (see
                // LoadGame_AmbiguousResource_ReportsAmbiguousMatch); Archive = "setB" must
                // have reached Native and narrowed the candidates to exactly one.
                Assert.Equal(ChipestraResult.Ok, result);
                Assert.True(player.RomResolution!.AllResolved);
                Assert.Equal(PlaybackState.MachineLoaded, player.Status.State);
            }
            finally
            {
                Directory.Delete(root, recursive: true);
            }
        }

        // A ROM Root can hold two ZIPs with the same name at different paths. Archive
        // scoping matches by name only, so it cannot tell them apart, and AmbiguousMatch is
        // the correct outcome (never auto-select). Checked through the full Managed round trip.
        [Fact]
        public void LoadGame_ArchiveScope_SameArchiveNameAtDifferentPaths_StillReportsAmbiguousMatch()
        {
            string root = MakeTempRomRoot("archive_scope_duplicate_name");
            try
            {
                // Two loose files whose immediate parent directory is named "dedicated" in both
                // cases but which live at two different paths under the ROM Root; scoping by
                // name cannot tell them apart, so AmbiguousMatch is the correct outcome.
                Directory.CreateDirectory(Path.Combine(root, "groupA", "dedicated"));
                Directory.CreateDirectory(Path.Combine(root, "groupB", "dedicated"));
                File.WriteAllBytes(Path.Combine(root, "groupA", "dedicated", "sameset.bin"), new byte[] { 0x01, 0x02 });
                File.WriteAllBytes(Path.Combine(root, "groupB", "dedicated", "sameset.bin"), new byte[] { 0x03, 0x04, 0x05 });
                var catalog = CatalogWithResources(
                    new[] { new RomResource { Name = "r1", FileName = "sameset.bin" } }, archive: "dedicated");
                using var player = new Player(catalog);

                ChipestraResult result = player.LoadGame("TEST_GAME", root);

                Assert.Equal(ChipestraResult.AmbiguousMatch, result);
                Assert.Equal(RomResolutionStatus.AmbiguousMatch, player.RomResolution!.Entries[0].Status);
                Assert.Equal(PlaybackState.Created, player.Status.State);
            }
            finally
            {
                Directory.Delete(root, recursive: true);
            }
        }
    }
}
