// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using Chipestra.App.ViewModels;
using Xunit;

namespace Chipestra.App.Tests
{
    // SEARCH box filtering (LibrarySearchFilter.Matches): a flat, hierarchy-ignoring title filter.
    public class LibrarySearchFilterTests
    {
        [Theory]
        [InlineData("anytitle", null, true)]
        [InlineData("anytitle", "", true)]
        [InlineData("anytitle", "   ", true)]
        public void Matches_ReturnsTrue_WhenSearchTextIsNullEmptyOrWhitespace(
            string title,
            string? searchText,
            bool expected)
        {
            bool result = LibrarySearchFilter.Matches(title, searchText);
            Assert.Equal(expected, result);
        }

        [Theory]
        [InlineData("After Burner II", "Burner", true)]
        [InlineData("After Burner II", "burner", true)]
        [InlineData("After Burner II", "After", true)]
        [InlineData("After Burner II", "II", true)]
        public void Matches_ReturnsTrue_WhenTitleContainsSearchText(
            string title,
            string searchText,
            bool expected)
        {
            bool result = LibrarySearchFilter.Matches(title, searchText);
            Assert.Equal(expected, result);
        }

        [Fact]
        public void Matches_ReturnsFalse_WhenTitleDoesNotContainSearchText()
        {
            bool result = LibrarySearchFilter.Matches("After Burner II", "Zelda");
            Assert.False(result);
        }
    }
}
