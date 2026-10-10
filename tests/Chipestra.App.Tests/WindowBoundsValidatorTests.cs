// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using Chipestra.App.ViewModels;
using Xunit;

namespace Chipestra.App.Tests
{
    // Pure position-safety check used before restoring a saved window position. There is
    // deliberately no AppSettingsStore file I/O test: its path is the real
    // %LOCALAPPDATA%\Chipestra\settings.json, which a test would overwrite.
    public class WindowBoundsValidatorTests
    {
        [Theory]
        [InlineData(0, 0, 1920, 1080, true)]        // top-left corner: always safe
        [InlineData(1870, 1030, 1920, 1080, true)]   // fits with exactly the visibility margin to spare
        [InlineData(1900, 1060, 1920, 1080, false)]  // would leave less than the margin visible
        [InlineData(-10, 100, 1920, 1080, false)]    // negative Left: off the left edge
        [InlineData(100, -10, 1920, 1080, false)]    // negative Top: off the top edge
        [InlineData(3000, 100, 1920, 1080, false)]   // a since-removed second monitor to the right
        public void ShouldRestorePosition_MatchesExpectedSafety(
            double left, double top, double virtualScreenWidth, double virtualScreenHeight, bool expected)
        {
            Assert.Equal(expected, WindowBoundsValidator.ShouldRestorePosition(left, top, virtualScreenWidth, virtualScreenHeight));
        }

        [Theory]
        [InlineData(200, 1060, true)]    // the XAML default itself, in a typical window: always safe
        [InlineData(100, 1060, true)]    // exactly the minimum library width
        [InlineData(99, 1060, false)]    // just under the minimum library width
        [InlineData(860, 1060, true)]    // leaves exactly the track-list minimum (1060-860=200)
        [InlineData(861, 1060, false)]   // would leave less than the track-list minimum
        [InlineData(200, 250, false)]    // a since-narrowed window: same saved width no longer fits
        public void ShouldRestoreLibraryColumnWidth_MatchesExpectedSafety(double width, double windowWidth, bool expected)
        {
            Assert.Equal(expected, WindowBoundsValidator.ShouldRestoreLibraryColumnWidth(width, windowWidth));
        }
    }
}
