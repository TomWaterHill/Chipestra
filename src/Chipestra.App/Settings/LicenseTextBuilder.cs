// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using System.IO;
using System.Linq;
using System.Text;

namespace Chipestra.App.Settings
{
    // Builds the text shown by the License window from the files shipped next to the EXE:
    //   1. THIRD-PARTY-NOTICES.md  (Chipestra's own notice and the table of third-party code)
    //   2. licenses/*.txt          (full license texts; FFTPACK, BSD-3-Clause, LGPL-2.1, MIT, in this order,
    //                               any further .txt files after them in file-name order)
    //   3. LICENSE                 (the GNU General Public License, version 2, full text)
    public static class LicenseTextBuilder
    {
        public const string NoticesFileName = "THIRD-PARTY-NOTICES.md";
        public const string LicensesFolderName = "licenses";
        public const string LicenseFileName = "LICENSE";

        // Order of the full texts in licenses/ (files not listed here follow, sorted by name).
        private static readonly string[] LicenseFileOrder =
        {
            "FFTPACK.txt", "BSD-3-Clause.txt", "LGPL-2.1.txt", "MIT.txt",
        };

        /// <param name="baseDirectory">The folder holding THIRD-PARTY-NOTICES.md, licenses/ and LICENSE
        /// (the application folder).</param>
        public static string BuildLicenseText(string baseDirectory)
        {
            var result = new StringBuilder();

            AppendSection(result, NoticesFileName, Path.Combine(baseDirectory, NoticesFileName));

            string licensesDirectory = Path.Combine(baseDirectory, LicensesFolderName);
            string[] files = Directory.Exists(licensesDirectory)
                ? Directory.GetFiles(licensesDirectory, "*.txt")
                : Array.Empty<string>();
            foreach (string file in OrderLicenseFiles(files))
            {
                AppendSection(result, LicensesFolderName + "/" + Path.GetFileName(file), file);
            }

            AppendSection(result, LicenseFileName + " (GNU General Public License, version 2)",
                Path.Combine(baseDirectory, LicenseFileName));

            return result.ToString().TrimEnd('\r', '\n');
        }

        private static string[] OrderLicenseFiles(string[] files)
        {
            int Rank(string file)
            {
                int index = Array.FindIndex(LicenseFileOrder,
                    name => name.Equals(Path.GetFileName(file), StringComparison.OrdinalIgnoreCase));
                return index >= 0 ? index : LicenseFileOrder.Length;
            }

            return files
                .OrderBy(Rank)
                .ThenBy(Path.GetFileName, StringComparer.OrdinalIgnoreCase)
                .ToArray();
        }

        private static void AppendSection(StringBuilder result, string heading, string path)
        {
            if (result.Length > 0)
            {
                result.AppendLine();
                result.AppendLine();
            }

            result.AppendLine($"=== {heading} ===");
            result.AppendLine();
            result.AppendLine(File.Exists(path) ? File.ReadAllText(path) : $"[file not found: {Path.GetFileName(path)}]");
        }
    }
}
