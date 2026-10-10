// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System.Collections.Generic;
using Chipestra.App.Settings;
using Chipestra.Core;
using Xunit;

namespace Chipestra.App.Tests
{
    public class XmlLoadErrorMessageTests
    {
        private static List<GameDefinitionLoadFailure> Failures(int n)
        {
            var list = new List<GameDefinitionLoadFailure>();
            for (int i = 1; i <= n; i++)
            {
                list.Add(new GameDefinitionLoadFailure($"f{i}.xml", $"reason {i}"));
            }
            return list;
        }

        [Fact]
        public void Build_ListsEveryFileWithItsReasonAndSaysOtherGamesWork()
        {
            string text = XmlLoadErrorMessage.Build(Failures(2));

            Assert.Contains("2 game definition files could not be loaded", text);
            Assert.Contains("f1.xml", text);
            Assert.Contains("reason 1", text);
            Assert.Contains("f2.xml", text);
            Assert.Contains("reason 2", text);
            Assert.Contains("other games are still available", text);
            Assert.DoesNotContain("more", text);
        }

        [Fact]
        public void Build_SingleFailure_UsesSingularWording()
        {
            Assert.Contains("1 game definition file could not be loaded", XmlLoadErrorMessage.Build(Failures(1)));
        }

        [Fact]
        public void Build_MoreThanTheLimit_ShowsOnlyTheLimitAndCountsTheRest()
        {
            string text = XmlLoadErrorMessage.Build(Failures(13), maxShown: 10);

            Assert.Contains("13 game definition files could not be loaded", text);
            Assert.Contains("f10.xml", text);
            Assert.DoesNotContain("f11.xml", text);
            Assert.Contains("...and 3 more.", text);
        }

        [Fact]
        public void Build_ExactlyAtTheLimit_HasNoMoreLine()
        {
            string text = XmlLoadErrorMessage.Build(Failures(XmlLoadErrorMessage.MaxShownFailures));

            Assert.DoesNotContain("more.", text);
        }

        [Fact]
        public void Title_IsTheXmlLoadErrorsTitle()
        {
            Assert.Equal("Chipestra - XML Load Errors", XmlLoadErrorMessage.Title);
        }
    }
}
