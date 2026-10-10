// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using System.Reflection;

namespace Chipestra.App.Settings
{
    // The one place the internal numeric version (Major.Minor.Patch, e.g. 1.0.0, set in the repo-root
    // version.props) becomes the displayed form "Ver a.bbb.cc" (Minor zero-padded to 3 digits, Patch to 2).
    public static class VersionFormatter
    {
        public static string Format(Version version) =>
            $"Ver {version.Major}.{version.Minor:D3}.{Math.Max(version.Build, 0):D2}";

        public static string CurrentDisplay()
        {
            Version? version = typeof(VersionFormatter).Assembly.GetName().Version;
            return version != null ? Format(version) : "Ver ?";
        }
    }
}
