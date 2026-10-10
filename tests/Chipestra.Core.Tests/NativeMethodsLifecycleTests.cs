// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using Chipestra.Core;
using Xunit;

namespace Chipestra.Core.Tests
{
    // Exercises chipestra_create_instance/chipestra_destroy_instance directly through
    // NativeMethods (internal, exposed to this assembly via
    // InternalsVisibleTo) to cover the null-argument
    // safety path that NativeRuntimeSession's API surface can't reach. (The tests that reuse a destroyed
    // handle live in DestroyedHandleTests, which runs alone - see DestroyedHandleCollection.)
    public class NativeMethodsLifecycleTests
    {
        [Fact]
        public void CreateInstance_NullOutHandle_ReturnsInvalidArgument()
        {
            ChipestraResult result = NativeMethods.chipestra_create_instance_raw(IntPtr.Zero);
            Assert.Equal(ChipestraResult.InvalidArgument, result);
        }

        [Fact]
        public void DestroyInstance_NullHandle_DoesNotThrow()
        {
            var exception = Record.Exception(() => NativeMethods.chipestra_destroy_instance(IntPtr.Zero));
            Assert.Null(exception);
        }
    }
}
