// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using System.Threading;

namespace Chipestra.App.Settings
{
    // Allows one Chipestra per user session. The named mutex lives in the Local\ namespace (per logon
    // session) and is held until the guard is disposed at exit. Keep the guard in a field for the whole
    // lifetime of the app, or the garbage collector may release the mutex.
    public sealed class SingleInstanceGuard : IDisposable
    {
        public const string DefaultMutexName = @"Local\Chipestra.SingleInstance";

        private Mutex? _mutex;

        private SingleInstanceGuard(Mutex mutex) => _mutex = mutex;

        // Returns the guard if this process is the first instance, null if another one already holds the
        // mutex. A mutex abandoned by a crashed previous instance counts as free (we then own it).
        public static SingleInstanceGuard? TryAcquire(string mutexName = DefaultMutexName)
        {
            var mutex = new Mutex(initiallyOwned: false, mutexName);
            bool acquired;
            try
            {
                acquired = mutex.WaitOne(0);
            }
            catch (AbandonedMutexException)
            {
                acquired = true;
            }

            if (!acquired)
            {
                mutex.Dispose();
                return null;
            }
            return new SingleInstanceGuard(mutex);
        }

        public void Dispose()
        {
            Mutex? mutex = Interlocked.Exchange(ref _mutex, null);
            if (mutex == null)
            {
                return;
            }
            try
            {
                mutex.ReleaseMutex();
            }
            catch (ApplicationException)
            {
                // Not owned by this thread (disposed from another thread): the OS releases it at process exit.
            }
            mutex.Dispose();
        }
    }
}
