// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System.Collections.ObjectModel;
using Chipestra.Core;

namespace Chipestra.App.ViewModels
{
    /// <summary>
    /// One node in the Library's hierarchical tree (Category-&gt;Manufacturer-&gt;System-&gt;Game).
    /// Category/Manufacturer/System nodes are pure grouping (<see cref="Game"/> is null); a Game
    /// node is always a leaf (<see cref="Children"/> stays empty).
    /// </summary>
    public sealed class LibraryTreeNode
    {
        public required string Header { get; init; }

        /// <summary>Non-null only on a leaf Game node.</summary>
        public GameDefinition? Game { get; init; }

        public ObservableCollection<LibraryTreeNode> Children { get; } = new();

        public bool IsGame => Game != null;

        // WPF's default TreeViewItem AutomationPeer falls back to DataContext.ToString() for its
        // accessible Name, so without this override every node would expose the C# type name.
        // MainWindow.xaml also binds AutomationProperties.Name to Header for the same reason.
        public override string ToString() => Header;
    }
}
