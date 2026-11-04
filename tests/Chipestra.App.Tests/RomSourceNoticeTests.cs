// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System.Collections.Generic;
using Chipestra.App.ViewModels;
using Chipestra.Core;
using Xunit;

namespace Chipestra.App.Tests
{
    public class RomSourceNoticeTests
    {
        private static RomResolutionEntry Entry(string name, string? path, bool foundByCrc) => new()
        {
            Resource = new RomResource { Name = name, FileName = name + ".bin" },
            Status = RomResolutionStatus.Resolved,
            SourcePath = path,
            FoundByCrcSearch = foundByCrc,
        };

        [Fact]
        public void BuildRomSourceNotice_NoResolution_IsNull()
        {
            Assert.Null(MainViewModel.BuildRomSourceNotice(null));
        }

        [Fact]
        public void BuildRomSourceNotice_AllFoundByName_IsNull()
        {
            var resolution = new RomResolutionResult { Entries = new List<RomResolutionEntry> { Entry("a", "C:/roms/a.zip!a.bin", false) } };
            Assert.Null(MainViewModel.BuildRomSourceNotice(resolution));
        }

        [Fact]
        public void BuildRomSourceNotice_FoundByCrc_NamesTheFilesUsed()
        {
            var resolution = new RomResolutionResult
            {
                Entries = new List<RomResolutionEntry>
                {
                    Entry("a", "C:/roms/a.zip!a.bin", false),
                    Entry("b", "C:/roms/My Dump.zip!Game (Japan, USA).md", true),
                },
            };
            string? notice = MainViewModel.BuildRomSourceNotice(resolution);
            Assert.NotNull(notice);
            Assert.Contains("CRC-32", notice);
            Assert.Contains("My Dump.zip!Game (Japan, USA).md", notice);
            Assert.DoesNotContain("a.zip!a.bin", notice);
        }
    }
}
