// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;

namespace Chipestra.App.ViewModels
{
    /// <summary>
    /// SEARCH box filtering: a flat, hierarchy-ignoring title filter that
    /// <see cref="LibraryViewModel"/> uses to decide which Game nodes are visible while the
    /// SEARCH box has text, in place of the normal Category-&gt;Manufacturer-&gt;System-&gt;Game tree.
    /// </summary>
    public static class LibrarySearchFilter
    {
        /// <summary>
        /// Returns true if <paramref name="title"/> matches <paramref name="searchText"/>
        /// as a case-insensitive substring match. An empty/null/whitespace-only
        /// <paramref name="searchText"/> always matches everything (true).
        /// </summary>
        public static bool Matches(string title, string? searchText)
        {
            if (string.IsNullOrWhiteSpace(searchText))
            {
                return true;
            }

            return title.Contains(searchText, StringComparison.OrdinalIgnoreCase);
        }
    }
}
