// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace Chipestra.Core
{
    /// <summary>One game definition XML file that could not be loaded, and why.</summary>
    public sealed record GameDefinitionLoadFailure(string FileName, string Reason);

    /// <summary>
    /// The outcome of <see cref="GameDefinitionCatalog.LoadDirectory"/>: the games
    /// that loaded, and the files that did not (in the order they were read).
    /// </summary>
    public sealed class GameDefinitionLoadResult
    {
        public GameDefinitionLoadResult(
            IReadOnlyDictionary<string, GameDefinition> catalog,
            IReadOnlyList<GameDefinitionLoadFailure> failures)
        {
            Catalog = catalog;
            Failures = failures;
        }

        public IReadOnlyDictionary<string, GameDefinition> Catalog { get; }
        public IReadOnlyList<GameDefinitionLoadFailure> Failures { get; }
    }

    /// <summary>
    /// Loads every game definition XML file in a directory and in all folders below it into a
    /// GameId-keyed catalog, one file at a time: a file that cannot be read or
    /// parsed is recorded in <see cref="GameDefinitionLoadResult.Failures"/> and
    /// the rest still load (one broken XML must not take the whole app down).
    /// Files are read in the order of their path relative to the directory (written with '/', compared
    /// ordinally), so the result does not depend on the file system's enumeration order, and a file is
    /// named by that relative path in the failure list. Where a file sits has no meaning: the Library
    /// groups games by what the XML says. If a GameId appears in more than one file, the file that comes
    /// first is kept and each later one is reported as a failure that names both paths.
    /// </summary>
    public static class GameDefinitionCatalog
    {
        public static GameDefinitionLoadResult LoadDirectory(string definitionsDirectory)
        {
            var catalog = new Dictionary<string, GameDefinition>(StringComparer.Ordinal);
            var sourceOfGame = new Dictionary<string, string>(StringComparer.Ordinal);
            var failures = new List<GameDefinitionLoadFailure>();

            (string File, string Relative)[] files;
            try
            {
                files = Directory.EnumerateFiles(definitionsDirectory, "*.xml", SearchOption.AllDirectories)
                    .Select(f => (File: f, Relative: Path.GetRelativePath(definitionsDirectory, f).Replace(Path.DirectorySeparatorChar, '/')))
                    .OrderBy(f => f.Relative, StringComparer.Ordinal)
                    .ToArray();
            }
            catch (Exception ex) when (ex is IOException || ex is UnauthorizedAccessException || ex is ArgumentException)
            {
                failures.Add(new GameDefinitionLoadFailure(definitionsDirectory, Describe(ex)));
                return new GameDefinitionLoadResult(catalog, failures);
            }

            foreach ((string file, string name) in files)
            {
                try
                {
                    IReadOnlyList<GameDefinition> definitions = GameDefinitionXml.LoadAll(file);
                    foreach (GameDefinition definition in definitions)
                    {
                        if (sourceOfGame.TryGetValue(definition.GameId, out string? firstFile))
                        {
                            failures.Add(new GameDefinitionLoadFailure(
                                name, $"game id \"{definition.GameId}\" is already defined by {firstFile} ({name} is ignored)"));
                            continue;
                        }
                        catalog[definition.GameId] = definition;
                        sourceOfGame[definition.GameId] = name;
                    }
                }
                catch (Exception ex)
                {
                    failures.Add(new GameDefinitionLoadFailure(name, Describe(ex)));
                }
            }
            return new GameDefinitionLoadResult(catalog, failures);
        }

        private static string Describe(Exception ex)
        {
            string message = ex.Message.ReplaceLineEndings(" ").Trim();
            return $"{ex.GetType().Name}: {message}";
        }
    }
}
