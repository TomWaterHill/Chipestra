// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using Chipestra.Core;
using Xunit;

namespace Chipestra.Core.Tests
{
    public class NativeRuntimeSessionTests
    {
        [Fact]
        public void CreateAndDispose_Succeeds()
        {
            var session = new NativeRuntimeSession();
            var exception = Record.Exception(() => session.Dispose());
            Assert.Null(exception);
        }

        [Fact]
        public void Dispose_CalledTwice_IsSafe()
        {
            var session = new NativeRuntimeSession();
            session.Dispose();
            var exception = Record.Exception(() => session.Dispose());
            Assert.Null(exception);
        }

        [Fact]
        public void MultipleInstances_AreIndependent()
        {
            var session1 = new NativeRuntimeSession();
            var session2 = new NativeRuntimeSession();

            session1.Dispose();
            var exception = Record.Exception(() => session2.Dispose());
            Assert.Null(exception);
        }
    }
}
