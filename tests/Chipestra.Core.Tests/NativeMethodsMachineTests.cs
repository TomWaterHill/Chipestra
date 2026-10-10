// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using Chipestra.Core;
using Xunit;

namespace Chipestra.Core.Tests
{
    // Edge cases that NativeRuntimeSession's API surface can't reach: null
    // arguments at the raw P/Invoke boundary, and calling into a session
    // after Dispose().
    public class NativeMethodsMachineTests
    {
        [Fact]
        public void LoadMachine_NullHandle_ReturnsInvalidArgument()
        {
            ChipestraResult result = NativeMethods.chipestra_load_machine(IntPtr.Zero, "TEST_MACHINE", "STANDARD", null, 0);
            Assert.Equal(ChipestraResult.InvalidArgument, result);
        }

        [Fact]
        public void LoadMachine_NullMachineId_ReturnsInvalidArgument()
        {
            NativeMethods.chipestra_create_instance(out IntPtr handle);
            try
            {
                ChipestraResult result = NativeMethods.chipestra_load_machine(handle, null, "STANDARD", null, 0);
                Assert.Equal(ChipestraResult.InvalidArgument, result);
            }
            finally
            {
                NativeMethods.chipestra_destroy_instance(handle);
            }
        }

        [Fact]
        public void LoadMachine_NullVariantId_ReturnsInvalidArgument()
        {
            NativeMethods.chipestra_create_instance(out IntPtr handle);
            try
            {
                ChipestraResult result = NativeMethods.chipestra_load_machine(handle, "TEST_MACHINE", null, null, 0);
                Assert.Equal(ChipestraResult.InvalidArgument, result);
            }
            finally
            {
                NativeMethods.chipestra_destroy_instance(handle);
            }
        }

        [Fact]
        public void UnloadMachine_NullHandle_ReturnsInvalidArgument()
        {
            ChipestraResult result = NativeMethods.chipestra_unload_machine(IntPtr.Zero);
            Assert.Equal(ChipestraResult.InvalidArgument, result);
        }

        [Fact]
        public void ResetMachine_NullHandle_ReturnsInvalidArgument()
        {
            ChipestraResult result = NativeMethods.chipestra_reset_machine(IntPtr.Zero);
            Assert.Equal(ChipestraResult.InvalidArgument, result);
        }

        [Fact]
        public void LoadMachine_AfterDispose_ThrowsObjectDisposedException()
        {
            var session = new NativeRuntimeSession();
            session.Dispose();
            Assert.Throws<ObjectDisposedException>(() => session.LoadMachine("TEST_MACHINE", "STANDARD"));
        }
    }
}
