// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Xunit;

namespace Chipestra.Core.Tests
{
    // The game definition files of the repository's xml folder sit in folders named after what the file says:
    //   Arcade/<maker of the board>/<board>/<file>.xml
    //   Console/<maker of the console>/<console>/<publisher>/<file>.xml
    // A folder name is the name with everything but letters and digits removed (a leading "The " is dropped from
    // the board or console name), compared without regard to case. The folder does not change what the Library
    // shows (that comes from the XML); this only keeps the repository tidy. Files a user puts in the xml
    // folder of an installed copy are not checked.
    public class RepositoryXmlFolderLayoutTests
    {
        internal static string FolderName(string name)
        {
            return new string(name.Where(char.IsLetterOrDigit).ToArray());
        }

        private static string BoardFolderName(MachineInfo machine)
        {
            string name = machine.DisplayName;
            if (name.StartsWith("The ", StringComparison.Ordinal))
            {
                name = name.Substring(4);
            }
            return FolderName(name);
        }

        private static Dictionary<string, MachineInfo> Machines()
        {
            var machines = new Dictionary<string, MachineInfo>(StringComparer.Ordinal);
            uint count = NativeRuntimeSession.GetMachineCount();
            for (uint i = 0; i < count; i++)
            {
                if (NativeRuntimeSession.GetMachineInfo(i, out MachineInfo info) == ChipestraResult.Ok)
                {
                    machines[info.MachineId] = info;
                }
            }
            return machines;
        }

        [Fact]
        public void FolderName_RemovesEverythingButLettersAndDigits()
        {
            Assert.Equal("DataEast", FolderName("Data East"));
            Assert.Equal("68000Based", FolderName("68000 Based"));
            Assert.Equal("XBoard", FolderName("X-Board"));
            Assert.Equal("OutRun", FolderName("Out Run"));
            Assert.Equal("F2System", FolderName("F2 System"));
        }

        [Fact]
        public void RepositoryXml_EveryFileSitsInTheFoldersItsContentNames()
        {
            string? xmlFolder = RepositoryXml.Folder();
            Assert.NotNull(xmlFolder);
            Dictionary<string, MachineInfo> machines = Machines();

            var problems = new List<string>();
            int checkedFiles = 0;
            foreach (string file in Directory.EnumerateFiles(xmlFolder!, "*.xml", SearchOption.AllDirectories).OrderBy(f => f, StringComparer.Ordinal))
            {
                checkedFiles++;
                string relative = Path.GetRelativePath(xmlFolder!, file).Replace(Path.DirectorySeparatorChar, '/');
                string[] parts = relative.Split('/');
                GameDefinition game = GameDefinitionXml.Load(file);
                if (!machines.TryGetValue(game.MachineId, out MachineInfo machine))
                {
                    problems.Add($"{relative}: machine \"{game.MachineId}\" is not registered");
                    continue;
                }

                bool console = string.Equals(machine.Category, "CONSOLE", StringComparison.OrdinalIgnoreCase);
                string[] expected = console
                    ? new[]
                    {
                        "Console", FolderName(machine.Manufacturer), BoardFolderName(machine),
                        FolderName(game.Manufacturer ?? string.Empty), Path.GetFileName(file),
                    }
                    : new[] { "Arcade", FolderName(machine.Manufacturer), BoardFolderName(machine), Path.GetFileName(file) };

                bool same = parts.Length == expected.Length
                    && parts.Zip(expected, (a, b) => string.Equals(a, b, StringComparison.OrdinalIgnoreCase)).All(x => x);
                if (!same)
                {
                    problems.Add($"{relative}: expected {string.Join("/", expected)}");
                }
            }

            Assert.True(checkedFiles > 0, "the xml folder has no files");
            Assert.True(problems.Count == 0, string.Join("; ", problems));
        }
    }
}
