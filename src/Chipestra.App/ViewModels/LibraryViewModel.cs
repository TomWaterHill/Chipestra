// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using Chipestra.Core;

namespace Chipestra.App.ViewModels
{
    /// <summary>
    /// Library screen state: builds the hierarchical Category-&gt;Manufacturer-&gt;System-&gt;Game
    /// tree from a game definition catalog once at construction, and switches
    /// <see cref="DisplayNodes"/> between that tree and a flat, <see cref="LibrarySearchFilter"/>-filtered
    /// list of matching games while <see cref="SearchText"/> has text.
    /// </summary>
    public sealed class LibraryViewModel : ViewModelBase
    {
        private readonly List<LibraryTreeNode> _hierarchyRoots;
        private readonly List<(string Title, LibraryTreeNode Node)> _allGameNodes;

        public IReadOnlyDictionary<string, GameDefinition> Catalog { get; }

        /// <summary>
        /// What <c>LibraryTreeView</c> should currently show - either
        /// <see cref="_hierarchyRoots"/> (SearchText empty) or a flat list of
        /// matching Game nodes (SearchText non-empty). Mutated in place (Clear +
        /// re-Add) rather than replaced, so a plain <c>ItemsSource="{Binding
        /// DisplayNodes}"</c> binding picks up changes via
        /// <see cref="ObservableCollection{T}"/>'s own change notifications.
        /// </summary>
        public ObservableCollection<LibraryTreeNode> DisplayNodes { get; } = new();

        private string _searchText = string.Empty;

        /// <summary>SEARCH box text - hierarchy-ignoring title filter, see <see cref="LibrarySearchFilter"/>.</summary>
        public string SearchText
        {
            get => _searchText;
            set
            {
                if (SetField(ref _searchText, value))
                {
                    RefreshDisplayNodes();
                }
            }
        }

        private LibraryTreeNode? _selectedNode;

        /// <summary>
        /// Set by MainWindow's TreeView.SelectedItemChanged handler - WPF's
        /// TreeView.SelectedItem is a read-only DependencyProperty (unlike
        /// ListBox.SelectedItem), so it can't be bound TwoWay directly.
        /// </summary>
        public LibraryTreeNode? SelectedNode
        {
            get => _selectedNode;
            set
            {
                if (SetField(ref _selectedNode, value))
                {
                    OnSelectedNodeChanged();
                }
            }
        }

        private GameDefinition? _selectedGame;

        /// <summary>The Game the current <see cref="SelectedNode"/> belongs to, or null if it isn't (or isn't) a Game node.</summary>
        public GameDefinition? SelectedGame
        {
            get => _selectedGame;
            private set => SetField(ref _selectedGame, value);
        }

        /// <summary>The selected Game's tracks (its &lt;tracks&gt;), for TrackListView's ItemsSource.</summary>
        public ObservableCollection<TrackDefinition> Tracks { get; } = new();

        private TrackDefinition? _selectedTrack;

        /// <summary>Bound TwoWay to TrackListView.SelectedItem (ListView.SelectedItem does support binding, unlike TreeView's).</summary>
        public TrackDefinition? SelectedTrack
        {
            get => _selectedTrack;
            set => SetField(ref _selectedTrack, value);
        }

        public LibraryViewModel(IReadOnlyDictionary<string, GameDefinition> catalog, MachineCatalogLookup machines)
        {
            Catalog = catalog;
            (_hierarchyRoots, _allGameNodes) = BuildTree(catalog, machines);
            RefreshDisplayNodes();
        }

        private static (List<LibraryTreeNode> Roots, List<(string Title, LibraryTreeNode Node)> GameNodes) BuildTree(
            IReadOnlyDictionary<string, GameDefinition> catalog, MachineCatalogLookup machines)
        {
            // Category -> Manufacturer -> System, each level built up as we walk
            // the catalog once; Category/Manufacturer sorted for a stable,
            // predictable tree order (games within a System keep catalog order,
            // already Title-sorted below).
            var categories = new SortedDictionary<string, Dictionary<string, Dictionary<string, LibraryTreeNode>>>(StringComparer.Ordinal);
            var gameNodes = new List<(string, LibraryTreeNode)>();

            foreach (GameDefinition game in catalog.Values.OrderBy(g => g.Title, StringComparer.OrdinalIgnoreCase))
            {
                MachineInfo? machine = machines.Find(game.MachineId);
            // MachineInfo is authoritative for grouping, not GameDefinition.Manufacturer
            // (informational only). Falls back to something reasonable if the machine isn't registered.
                string category = !string.IsNullOrEmpty(machine?.Category) ? machine.Value.Category : "ARCADE";
                string manufacturer = !string.IsNullOrEmpty(machine?.Manufacturer) ? machine.Value.Manufacturer : "Unknown";
                string system = !string.IsNullOrEmpty(machine?.DisplayName) ? machine.Value.DisplayName : game.MachineId;

                if (!categories.TryGetValue(category, out Dictionary<string, Dictionary<string, LibraryTreeNode>>? manufacturers))
                {
                    manufacturers = new Dictionary<string, Dictionary<string, LibraryTreeNode>>(StringComparer.Ordinal);
                    categories[category] = manufacturers;
                }
                if (!manufacturers.TryGetValue(manufacturer, out Dictionary<string, LibraryTreeNode>? systems))
                {
                    systems = new Dictionary<string, LibraryTreeNode>(StringComparer.Ordinal);
                    manufacturers[manufacturer] = systems;
                }
                if (!systems.TryGetValue(system, out LibraryTreeNode? systemNode))
                {
                    systemNode = new LibraryTreeNode { Header = system };
                    systems[system] = systemNode;
                }

                var gameNode = new LibraryTreeNode { Header = game.Title, Game = game };
                systemNode.Children.Add(gameNode);
                gameNodes.Add((game.Title, gameNode));
            }

            var roots = new List<LibraryTreeNode>();
            foreach ((string categoryName, Dictionary<string, Dictionary<string, LibraryTreeNode>> manufacturers) in categories)
            {
                var categoryNode = new LibraryTreeNode { Header = categoryName };
                foreach ((string manufacturerName, Dictionary<string, LibraryTreeNode> systems) in
                         manufacturers.OrderBy(kv => kv.Key, StringComparer.OrdinalIgnoreCase))
                {
                    var manufacturerNode = new LibraryTreeNode { Header = manufacturerName };
                    foreach ((string _, LibraryTreeNode systemNode) in systems.OrderBy(kv => kv.Key, StringComparer.OrdinalIgnoreCase))
                    {
                        manufacturerNode.Children.Add(systemNode);
                    }
                    categoryNode.Children.Add(manufacturerNode);
                }
                roots.Add(categoryNode);
            }

            return (roots, gameNodes);
        }

        private void RefreshDisplayNodes()
        {
            DisplayNodes.Clear();
            if (string.IsNullOrWhiteSpace(SearchText))
            {
                foreach (LibraryTreeNode root in _hierarchyRoots)
                {
                    DisplayNodes.Add(root);
                }
                return;
            }

            foreach ((string title, LibraryTreeNode node) in _allGameNodes)
            {
                if (LibrarySearchFilter.Matches(title, SearchText))
                {
                    DisplayNodes.Add(node);
                }
            }
        }

        private void OnSelectedNodeChanged()
        {
            Tracks.Clear();
            if (_selectedNode?.Game is GameDefinition game)
            {
                SelectedGame = game;
                foreach (TrackDefinition track in game.Tracks)
                {
                    Tracks.Add(track);
                }
            }
            else
            {
                SelectedGame = null;
            }
            SelectedTrack = null;
        }
    }
}
