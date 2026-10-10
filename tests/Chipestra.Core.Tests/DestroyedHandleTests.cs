// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using Chipestra.Core;
using Xunit;

namespace Chipestra.Core.Tests
{
    // Tests that deliberately use a native handle AFTER chipestra_destroy_instance. They must not run in
    // parallel with any other test:
    //
    // The C ABI handle is the instance's raw pointer, and the live-instance registry
    // (chipestra_api.cpp's g_liveInstances) is keyed by that pointer. Once an instance is destroyed its
    // address can be handed to a NEW instance - for example one created at that moment by another test
    // running in parallel. A stale handle used after that point is indistinguishable from the new
    // instance's handle: a second chipestra_destroy_instance would remove the other test's live instance
    // from the registry (its next calls then fail with InvalidState), and a stale unload/reset would act on
    // it. Real callers never reuse a destroyed handle (NativeRuntimeSession.Dispose destroys once), so this
    // only matters for these tests; running them alone removes the interference.
    [CollectionDefinition(DestroyedHandleCollection.Name, DisableParallelization = true)]
    public sealed class DestroyedHandleCollection
    {
        public const string Name = "Native destroyed-handle tests (run alone)";
    }

    [Collection(DestroyedHandleCollection.Name)]
    public class DestroyedHandleTests
    {
        [Fact]
        public void DestroyInstance_SameHandleTwice_DoesNotThrow()
        {
            ChipestraResult created = NativeMethods.chipestra_create_instance(out IntPtr handle);
            Assert.Equal(ChipestraResult.Ok, created);

            NativeMethods.chipestra_destroy_instance(handle);
            var exception = Record.Exception(() => NativeMethods.chipestra_destroy_instance(handle));
            Assert.Null(exception);
        }

        [Fact]
        public void UnloadMachine_AfterDestroyedHandle_ReturnsInvalidState()
        {
            NativeMethods.chipestra_create_instance(out IntPtr handle);
            NativeMethods.chipestra_destroy_instance(handle);

            ChipestraResult result = NativeMethods.chipestra_unload_machine(handle);
            Assert.Equal(ChipestraResult.InvalidState, result);
        }
    }
}
