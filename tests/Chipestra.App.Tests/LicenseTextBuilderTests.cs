// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using System.IO;
using Chipestra.App.Settings;
using Xunit;

namespace Chipestra.App.Tests
{
    public class LicenseTextBuilderTests : IDisposable
    {
        private readonly string _dir = Path.Combine(Path.GetTempPath(), "chipestra_license_test_" + Guid.NewGuid().ToString("N"));

        public LicenseTextBuilderTests()
        {
            Directory.CreateDirectory(Path.Combine(_dir, "licenses"));
        }

        public void Dispose()
        {
            try { Directory.Delete(_dir, recursive: true); } catch (IOException) { }
        }

        private void Write(string relativePath, string content) =>
            File.WriteAllText(Path.Combine(_dir, relativePath), content);

        [Fact]
        public void BuildLicenseText_OrdersNoticesThenLicensesThenLicenseFile()
        {
            Write("THIRD-PARTY-NOTICES.md", "NOTICES-BODY");
            Write("licenses/MIT.txt", "MIT-BODY");
            Write("licenses/LGPL-2.1.txt", "LGPL-BODY");
            Write("licenses/BSD-3-Clause.txt", "BSD-BODY");
            Write("licenses/FFTPACK.txt", "FFTPACK-BODY");
            Write("LICENSE", "ROOT-LICENSE-BODY");

            string text = LicenseTextBuilder.BuildLicenseText(_dir);

            int notices = text.IndexOf("NOTICES-BODY", StringComparison.Ordinal);
            int fftpack = text.IndexOf("FFTPACK-BODY", StringComparison.Ordinal);
            int bsd = text.IndexOf("BSD-BODY", StringComparison.Ordinal);
            int lgpl = text.IndexOf("LGPL-BODY", StringComparison.Ordinal);
            int mit = text.IndexOf("MIT-BODY", StringComparison.Ordinal);
            int gpl = text.IndexOf("ROOT-LICENSE-BODY", StringComparison.Ordinal);

            Assert.True(notices >= 0 && notices < fftpack, "notices come first");
            Assert.True(fftpack < bsd && bsd < lgpl && lgpl < mit, "FFTPACK, BSD-3-Clause, LGPL-2.1, MIT in this order");
            Assert.True(mit < gpl, "LICENSE (GPL) comes last");
        }

        [Fact]
        public void BuildLicenseText_GivesEachFullTextItsOwnHeading()
        {
            Write("THIRD-PARTY-NOTICES.md", "n");
            Write("licenses/MIT.txt", "m");
            Write("LICENSE", "g");

            string text = LicenseTextBuilder.BuildLicenseText(_dir);

            Assert.Contains("=== THIRD-PARTY-NOTICES.md ===", text);
            Assert.Contains("=== licenses/MIT.txt ===", text);
            Assert.Contains("=== LICENSE (GNU General Public License, version 2) ===", text);
        }

        [Fact]
        public void BuildLicenseText_PutsUnlistedLicenseFilesAfterTheListedOnes_InNameOrder()
        {
            Write("THIRD-PARTY-NOTICES.md", "n");
            Write("licenses/MIT.txt", "MIT-BODY");
            Write("licenses/Zlib.txt", "ZLIB-BODY");
            Write("licenses/Apache-2.0.txt", "APACHE-BODY");
            Write("LICENSE", "g");

            string text = LicenseTextBuilder.BuildLicenseText(_dir);

            Assert.True(text.IndexOf("MIT-BODY", StringComparison.Ordinal) < text.IndexOf("APACHE-BODY", StringComparison.Ordinal));
            Assert.True(text.IndexOf("APACHE-BODY", StringComparison.Ordinal) < text.IndexOf("ZLIB-BODY", StringComparison.Ordinal));
        }

        [Fact]
        public void BuildLicenseText_ReportsMissingFilesInsteadOfThrowing()
        {
            string text = LicenseTextBuilder.BuildLicenseText(_dir);

            Assert.Contains("[file not found: THIRD-PARTY-NOTICES.md]", text);
            Assert.Contains("[file not found: LICENSE]", text);
        }

        [Fact]
        public void BuildLicenseText_DoesNotReadTheOldThirdpartyLicensesFolder()
        {
            Directory.CreateDirectory(Path.Combine(_dir, "thirdparty", "licenses"));
            Write("thirdparty/licenses/old.md", "OLD-WORK-NOTES");
            Write("THIRD-PARTY-NOTICES.md", "n");
            Write("LICENSE", "g");

            Assert.DoesNotContain("OLD-WORK-NOTES", LicenseTextBuilder.BuildLicenseText(_dir));
        }

        [Fact]
        public void BuildLicenseText_FromTheRepositoryRoot_ContainsTheShippedTexts()
        {
            string? root = FindRepositoryRoot();
            if (root == null) return; // not running from a source checkout

            string text = LicenseTextBuilder.BuildLicenseText(root);

            Assert.DoesNotContain("[file not found", text);
            Assert.Contains("GNU GENERAL PUBLIC LICENSE", text);
            Assert.Contains("GNU Lesser General Public License", text);
            Assert.Contains("Julien Pommier", text);
        }

        private static string? FindRepositoryRoot()
        {
            for (var dir = new DirectoryInfo(AppContext.BaseDirectory); dir != null; dir = dir.Parent)
            {
                if (File.Exists(Path.Combine(dir.FullName, "version.props")) &&
                    File.Exists(Path.Combine(dir.FullName, "THIRD-PARTY-NOTICES.md")))
                {
                    return dir.FullName;
                }
            }
            return null;
        }
    }
}
