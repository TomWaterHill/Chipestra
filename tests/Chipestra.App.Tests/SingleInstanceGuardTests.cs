// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using System.Threading;
using System.Threading.Tasks;
using Chipestra.App.Settings;
using Xunit;

namespace Chipestra.App.Tests
{
    public class SingleInstanceGuardTests
    {
        private static string NewName() => @"Local\Chipestra.Tests." + Guid.NewGuid().ToString("N");

        // A named mutex is re-entrant for the thread that owns it, so "another instance" is simulated by
        // trying from a different thread (in the real app it is a different process). The thread is a
        // dedicated one: a Task can be run inline on the thread that waits for it, which would be the same
        // thread again and would wrongly succeed.
        private static SingleInstanceGuard? TryAcquireElsewhere(string name)
        {
            SingleInstanceGuard? result = null;
            var thread = new Thread(() => result = SingleInstanceGuard.TryAcquire(name));
            thread.Start();
            thread.Join();
            return result;
        }

        [Fact]
        public void FirstAcquire_Succeeds()
        {
            string name = NewName();
            using SingleInstanceGuard? first = SingleInstanceGuard.TryAcquire(name);
            Assert.NotNull(first);
        }

        [Fact]
        public void SecondAcquireWhileHeld_ReturnsNull()
        {
            string name = NewName();
            using SingleInstanceGuard? first = SingleInstanceGuard.TryAcquire(name);
            Assert.NotNull(first);
            Assert.Null(TryAcquireElsewhere(name));
        }

        [Fact]
        public void AfterDispose_AnotherInstanceCanAcquire()
        {
            string name = NewName();
            SingleInstanceGuard? first = SingleInstanceGuard.TryAcquire(name);
            Assert.NotNull(first);
            first!.Dispose();
            using SingleInstanceGuard? second = TryAcquireElsewhere(name);
            Assert.NotNull(second);
        }

        [Fact]
        public void MutexAbandonedByACrashedOwner_CountsAsFree()
        {
            string name = NewName();
            var owner = new Thread(() =>
            {
                var m = new Mutex(false, name);
                m.WaitOne();  // exits while owning it, without releasing: abandoned
            });
            owner.Start();
            owner.Join();

            using SingleInstanceGuard? next = SingleInstanceGuard.TryAcquire(name);
            Assert.NotNull(next);
        }

        [Fact]
        public void DefaultName_IsInTheSessionLocalNamespace()
        {
            Assert.StartsWith(@"Local\", SingleInstanceGuard.DefaultMutexName);
        }

        [Fact]
        public void DisposeTwice_DoesNotThrow()
        {
            SingleInstanceGuard? g = SingleInstanceGuard.TryAcquire(NewName());
            Assert.NotNull(g);
            g!.Dispose();
            g.Dispose();
        }
    }
}
