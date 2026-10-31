// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using System.IO;
using System.Linq;

namespace Chipestra.Core.Tests
{
    /// <summary>Finds the game definition files of the repository's xml folder, wherever they sit below it.</summary>
    internal static class RepositoryXml
    {
        /// <summary>The repository's xml folder (the nearest parent of the test binary that has one), or null.</summary>
        public static string? Folder()
        {
            var dir = new DirectoryInfo(AppContext.BaseDirectory);
            for (; dir != null; dir = dir.Parent)
            {
                string candidate = Path.Combine(dir.FullName, "xml");
                if (Directory.Exists(candidate))
                {
                    return candidate;
                }
            }
            return null;
        }

        /// <summary>The path of the file with this name below the xml folder, or null if there is none.</summary>
        public static string? Find(string fileName)
        {
            string? folder = Folder();
            if (folder == null)
            {
                return null;
            }
            return Directory.EnumerateFiles(folder, fileName, SearchOption.AllDirectories)
                .OrderBy(f => f, StringComparer.Ordinal)
                .FirstOrDefault();
        }

        /// <summary>Like <see cref="Find"/> but a missing file fails the test with a clear message.</summary>
        public static string Require(string fileName) =>
            Find(fileName) ?? throw new FileNotFoundException("xml/**/" + fileName + " not found from the test binary's directory");
    }
}
