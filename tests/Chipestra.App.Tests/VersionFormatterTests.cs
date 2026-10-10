// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using Chipestra.App.Settings;
using Xunit;

namespace Chipestra.App.Tests
{
    public class VersionFormatterTests
    {
        [Theory]
        [InlineData(1, 0, 0, "Ver 1.000.00")]
        [InlineData(1, 12, 3, "Ver 1.012.03")]
        [InlineData(2, 999, 99, "Ver 2.999.99")]
        [InlineData(0, 1, 0, "Ver 0.001.00")]
        public void Format_PadsMinorTo3AndPatchTo2(int major, int minor, int patch, string expected)
        {
            Assert.Equal(expected, VersionFormatter.Format(new System.Version(major, minor, patch)));
        }

        [Fact]
        public void Format_IgnoresTheFourthPart()
        {
            Assert.Equal("Ver 1.000.00", VersionFormatter.Format(new System.Version(1, 0, 0, 7)));
        }

        [Fact]
        public void CurrentDisplay_MatchesTheAppAssemblyVersion_AsVer1_000_00()
        {
            // version.props currently holds 1.0.0; update this expectation when it is bumped.
            Assert.Equal("Ver 1.000.00", VersionFormatter.CurrentDisplay());
        }
    }
}
