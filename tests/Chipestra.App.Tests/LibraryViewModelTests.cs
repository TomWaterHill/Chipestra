// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using System.Collections.Generic;
using Chipestra.App.ViewModels;
using Chipestra.Core;
using Xunit;

namespace Chipestra.App.Tests
{
    // LibraryViewModel's tree-building and SEARCH-filter switching logic, against
    // TEST_MACHINE (always registered, no ROM or real audio device needed).
    public class LibraryViewModelTests
    {
        private static GameDefinition MakeGame(string id, string title, string machineId = "TEST_MACHINE")
        {
            return new GameDefinition
            {
                GameId = id,
                Title = title,
                MachineId = machineId,
                VariantId = "STANDARD",
                RequiredResources = Array.Empty<RomResource>(),
                Tracks = new List<TrackDefinition>
                {
                    new TrackDefinition { Id = "T1", DisplayName = "Track 1", Command = 1 },
                },
            };
        }

        [Fact]
        public void DisplayNodes_NoSearch_ShowsFullCategoryManufacturerSystemGameHierarchy()
        {
            var catalog = new Dictionary<string, GameDefinition>(StringComparer.Ordinal)
            {
                { "G1", MakeGame("G1", "After Burner II") },
            };
            var library = new LibraryViewModel(catalog, new MachineCatalogLookup());

            LibraryTreeNode category = Assert.Single(library.DisplayNodes);
            Assert.False(category.IsGame);
            LibraryTreeNode manufacturer = Assert.Single(category.Children);
            Assert.False(manufacturer.IsGame);
            LibraryTreeNode system = Assert.Single(manufacturer.Children);
            Assert.False(system.IsGame);
            LibraryTreeNode game = Assert.Single(system.Children);
            Assert.True(game.IsGame);
            Assert.Equal("After Burner II", game.Header);
        }

        [Fact]
        public void SearchText_FiltersToMatchingGamesOnly_IgnoringHierarchy()
        {
            var catalog = new Dictionary<string, GameDefinition>(StringComparer.Ordinal)
            {
                { "G1", MakeGame("G1", "After Burner II") },
                { "G2", MakeGame("G2", "Out Run") },
            };
            var library = new LibraryViewModel(catalog, new MachineCatalogLookup());

            library.SearchText = "burner";

            LibraryTreeNode onlyMatch = Assert.Single(library.DisplayNodes);
            Assert.True(onlyMatch.IsGame);
            Assert.Equal("After Burner II", onlyMatch.Header);
        }

        [Fact]
        public void SearchText_ClearedAfterFiltering_RestoresFullHierarchy()
        {
            var catalog = new Dictionary<string, GameDefinition>(StringComparer.Ordinal)
            {
                { "G1", MakeGame("G1", "After Burner II") },
            };
            var library = new LibraryViewModel(catalog, new MachineCatalogLookup());

            library.SearchText = "burner";
            Assert.Single(library.DisplayNodes);

            library.SearchText = string.Empty;
            Assert.Single(library.DisplayNodes);
            Assert.False(library.DisplayNodes[0].IsGame);
        }

        [Fact]
        public void SelectedNode_OnGameNode_PopulatesSelectedGameAndTracks()
        {
            GameDefinition game = MakeGame("G1", "After Burner II");
            var catalog = new Dictionary<string, GameDefinition>(StringComparer.Ordinal) { { "G1", game } };
            var library = new LibraryViewModel(catalog, new MachineCatalogLookup());

            LibraryTreeNode gameNode = library.DisplayNodes[0].Children[0].Children[0].Children[0];
            library.SelectedNode = gameNode;

            Assert.Same(game, library.SelectedGame);
            Assert.Single(library.Tracks);
            Assert.Equal("T1", library.Tracks[0].Id);
        }

        [Fact]
        public void SelectedNode_OnNonGameNode_ClearsSelectedGameAndTracks()
        {
            var catalog = new Dictionary<string, GameDefinition>(StringComparer.Ordinal)
            {
                { "G1", MakeGame("G1", "After Burner II") },
            };
            var library = new LibraryViewModel(catalog, new MachineCatalogLookup());

            library.SelectedNode = library.DisplayNodes[0].Children[0].Children[0]; // system node, not a game
            Assert.Null(library.SelectedGame);
            Assert.Empty(library.Tracks);
        }
    }
}
