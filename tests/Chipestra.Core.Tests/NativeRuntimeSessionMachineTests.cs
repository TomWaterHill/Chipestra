// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using Chipestra.Core;
using Xunit;

namespace Chipestra.Core.Tests
{
    public class NativeRuntimeSessionMachineTests
    {
        [Fact]
        public void LoadResetUnload_Succeeds()
        {
            using var session = new NativeRuntimeSession();
            var loadResult = session.LoadMachine("TEST_MACHINE", "STANDARD");
            Assert.Equal(ChipestraResult.Ok, loadResult);

            var resetResult = session.Reset();
            Assert.Equal(ChipestraResult.Ok, resetResult);

            var unloadResult = session.UnloadMachine();
            Assert.Equal(ChipestraResult.Ok, unloadResult);
        }

        [Fact]
        public void LoadMachine_UnknownId_ReturnsNotFound()
        {
            using var session = new NativeRuntimeSession();
            var result = session.LoadMachine("NO_SUCH_MACHINE", "STANDARD");
            Assert.Equal(ChipestraResult.NotFound, result);
        }

        [Fact]
        public void Reset_BeforeLoadMachine_ReturnsInvalidState()
        {
            using var session = new NativeRuntimeSession();
            var result = session.Reset();
            Assert.Equal(ChipestraResult.InvalidState, result);
        }

        [Fact]
        public void UnloadMachine_BeforeLoadMachine_ReturnsInvalidState()
        {
            using var session = new NativeRuntimeSession();
            var result = session.UnloadMachine();
            Assert.Equal(ChipestraResult.InvalidState, result);
        }

        [Fact]
        public void LoadMachine_CalledTwice_SecondCallReturnsInvalidState()
        {
            using var session = new NativeRuntimeSession();
            var firstResult = session.LoadMachine("TEST_MACHINE", "STANDARD");
            Assert.Equal(ChipestraResult.Ok, firstResult);

            var secondResult = session.LoadMachine("TEST_MACHINE", "STANDARD");
            Assert.Equal(ChipestraResult.InvalidState, secondResult);
        }

        [Fact]
        public void RepeatedLoadResetUnloadCycles_AreDeterministic()
        {
            using var session = new NativeRuntimeSession();

            for (int i = 0; i < 3; i++)
            {
                var loadResult = session.LoadMachine("TEST_MACHINE", "STANDARD");
                Assert.Equal(ChipestraResult.Ok, loadResult);

                var resetResult = session.Reset();
                Assert.Equal(ChipestraResult.Ok, resetResult);

                var unloadResult = session.UnloadMachine();
                Assert.Equal(ChipestraResult.Ok, unloadResult);
            }
        }
    }
}
