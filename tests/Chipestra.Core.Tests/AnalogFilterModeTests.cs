// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using Chipestra.Core;
using Xunit;

namespace Chipestra.Core.Tests
{
    public class AnalogFilterModeTests
    {
        [Fact]
        public void EveryMode_CanBeSelectedOnAFreshSession()
        {
            using var session = new NativeRuntimeSession();
            foreach (AnalogFilterMode mode in Enum.GetValues<AnalogFilterMode>())
            {
                Assert.Equal(ChipestraResult.Ok, session.SetAnalogFilterMode(mode));
            }
        }

        [Fact]
        public void UnknownMode_IsRejected()
        {
            using var session = new NativeRuntimeSession();
            Assert.Equal(ChipestraResult.InvalidArgument, session.SetAnalogFilterMode((AnalogFilterMode)3));
        }

        [Fact]
        public void Values_MatchTheNativeConstants()
        {
            Assert.Equal(0, (int)AnalogFilterMode.Off);
            Assert.Equal(1, (int)AnalogFilterMode.Model1);
            Assert.Equal(2, (int)AnalogFilterMode.Model2);
        }

        [Fact]
        public void AfterDispose_Throws()
        {
            var session = new NativeRuntimeSession();
            session.Dispose();
            Assert.Throws<ObjectDisposedException>(() => session.SetAnalogFilterMode(AnalogFilterMode.Model1));
        }
    }
}
