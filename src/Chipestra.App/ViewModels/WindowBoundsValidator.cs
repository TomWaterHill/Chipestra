// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
namespace Chipestra.App.ViewModels
{
    /// <summary>
    /// Pure validation for whether a saved window position is still safe to restore, so it is
    /// unit-testable without a live WPF Window/SystemParameters. A position saved on a since-removed
    /// monitor (or a stale/corrupt settings.json value) must not put the window where the user
    /// can't reach it; falling back to WPF's default placement is safer.
    /// </summary>
    public static class WindowBoundsValidator
    {
        // Only a corner of the window needs to stay inside the virtual desktop; the saved
        // Width/Height isn't known here, and a partially offscreen but reachable window is fine.
        private const double MinVisibleMargin = 50;

        public static bool ShouldRestorePosition(double left, double top, double virtualScreenWidth, double virtualScreenHeight)
        {
            return left >= 0 && top >= 0
                && left + MinVisibleMargin <= virtualScreenWidth
                && top + MinVisibleMargin <= virtualScreenHeight;
        }

        // Same stale/corrupt-geometry concern as ShouldRestorePosition, applied to the
        // Library/track-list splitter's column width: a saved width could otherwise leave the
        // track-list column too narrow, or negative, once restored into a narrower window.
        private const double MinLibraryColumnWidth = 100;
        private const double MinTrackListColumnWidth = 200;

        public static bool ShouldRestoreLibraryColumnWidth(double width, double windowWidth)
        {
            return width >= MinLibraryColumnWidth
                && width <= windowWidth - MinTrackListColumnWidth;
        }
    }
}
