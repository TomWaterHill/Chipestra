// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;

namespace Chipestra.Core
{
    /// <summary>
    /// The loaded chipestra_native.dll's C ABI version is not the one this Core was built against
    /// (an old/new DLL next to a mismatched app). Thrown before any other native call so a
    /// stale DLL cannot silently misbehave (e.g. an argument the DLL does not know about).
    /// </summary>
    public sealed class NativeVersionMismatchException : Exception
    {
        public NativeVersionMismatchException(uint expected, uint? actual)
            : base(actual.HasValue
                ? $"chipestra_native.dll has C ABI version {actual.Value}, but this application needs version {expected}. " +
                  "The application and chipestra_native.dll are from different builds - reinstall or rebuild so they match."
                : $"chipestra_native.dll does not report a C ABI version (this application needs version {expected}). " +
                  "The DLL is too old or not a Chipestra build - reinstall or rebuild so the application and chipestra_native.dll match.")
        {
            Expected = expected;
            Actual = actual;
        }

        public uint Expected { get; }

        /// <summary>The DLL's reported version, or null if it has no chipestra_get_api_version export.</summary>
        public uint? Actual { get; }
    }
}
