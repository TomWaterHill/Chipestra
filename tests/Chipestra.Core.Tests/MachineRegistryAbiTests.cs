// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using Chipestra.Core;
using Xunit;

namespace Chipestra.Core.Tests
{
    // Registry group ABI: process-wide, not tied to any particular
    // NativeRuntimeSession, so these call the static GetMachineCount()/
    // GetMachineInfo() directly rather than going through a session.
    public class MachineRegistryAbiTests
    {
        [Fact]
        public void GetMachineCount_ReportsAtLeastTheDefaultTestMachine()
        {
            Assert.True(NativeRuntimeSession.GetMachineCount() >= 1);
        }

        [Fact]
        public void GetMachineInfo_ForTestMachine_ReturnsExpectedMetadata()
        {
            ChipestraResult result = NativeRuntimeSession.GetMachineInfo(0, out MachineInfo info);

            Assert.Equal(ChipestraResult.Ok, result);
            Assert.Equal("TEST_MACHINE", info.MachineId);
            Assert.Equal("ARCADE", info.Category);
            Assert.True(info.VariantCount >= 1);
        }

        [Fact]
        public void GetMachineInfo_OutOfRangeIndex_ReturnsNotFound()
        {
            uint outOfRangeIndex = NativeRuntimeSession.GetMachineCount();

            ChipestraResult result = NativeRuntimeSession.GetMachineInfo(outOfRangeIndex, out _);

            Assert.Equal(ChipestraResult.NotFound, result);
        }

        [Fact]
        public void LoadMachine_KnownMachineUnknownVariant_ReturnsNotFound()
        {
            using var session = new NativeRuntimeSession();

            ChipestraResult result = session.LoadMachine("TEST_MACHINE", "NOT_A_REAL_VARIANT");

            Assert.Equal(ChipestraResult.NotFound, result);
        }
    }
}
