// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using Chipestra.Core;
using Xunit;

namespace Chipestra.Core.Tests
{
    public class LevelMatchingTests
    {
        private const string Xml =
            "<chipestra><game id=\"t\" title=\"T\" machine=\"SEGA_XBOARD\" variant=\"XBOARD_BASE\" {0}>" +
            "<resources/><tracks><track id=\"1\" title=\"S\" command=\"1\"/></tracks></game></chipestra>";

        [Fact]
        public void GameDefinition_ReadsTheGainOfTheXml()
        {
            Assert.Equal(-7.5f, GameDefinitionXml.Parse(string.Format(Xml, "level_gain_db=\"-7.5\"")).LevelGainDb);
            Assert.Equal(0.0f, GameDefinitionXml.Parse(string.Format(Xml, "")).LevelGainDb);
        }

        [Fact]
        public void GameDefinition_RefusesAnUnusableGain()
        {
            Assert.ThrowsAny<Exception>(() => GameDefinitionXml.Parse(string.Format(Xml, "level_gain_db=\"loud\"")));
            Assert.ThrowsAny<Exception>(() => GameDefinitionXml.Parse(string.Format(Xml, "level_gain_db=\"45\"")));
        }

        [Fact]
        public void Session_AcceptsAGainAndTheOffState_AndRefusesAnOutOfRangeOne()
        {
            using var session = new NativeRuntimeSession();
            Assert.Equal(ChipestraResult.Ok, session.SetLevelMatching(true, -12.0f));
            Assert.Equal(ChipestraResult.Ok, session.SetLevelMatching(false, 0.0f));
            Assert.Equal(ChipestraResult.InvalidArgument, session.SetLevelMatching(true, 31.0f));
            Assert.Equal(ChipestraResult.InvalidArgument, session.SetLevelMatching(true, float.NaN));
        }

        [Fact]
        public void AfterDispose_Throws()
        {
            var session = new NativeRuntimeSession();
            session.Dispose();
            Assert.Throws<ObjectDisposedException>(() => session.SetLevelMatching(true, 0.0f));
        }
    }
}
