// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
namespace Chipestra.Core
{
    /// <summary>
    /// Public entry point to the Native Core. UI and other managed code must
    /// call through here rather than through <see cref="NativeMethods"/> directly.
    /// </summary>
    public static class ChipestraRuntime
    {
        /// <summary>The C ABI version this Core is built against (chipestra_abi.h / chipestra_api.cpp kApiVersion).</summary>
        public const uint ExpectedApiVersion = 4;

        public static uint GetApiVersion() => NativeMethods.chipestra_get_api_version();

        /// <summary>
        /// Throws <see cref="NativeVersionMismatchException"/> unless the loaded chipestra_native.dll
        /// reports exactly <see cref="ExpectedApiVersion"/>. Called from the NativeRuntimeSession
        /// constructor, i.e. before any other native call.
        /// </summary>
        public static void EnsureCompatibleNativeVersion()
        {
            uint actual;
            try
            {
                actual = NativeMethods.chipestra_get_api_version();
            }
            catch (System.EntryPointNotFoundException)
            {
                throw new NativeVersionMismatchException(ExpectedApiVersion, null);
            }

            if (actual != ExpectedApiVersion)
            {
                throw new NativeVersionMismatchException(ExpectedApiVersion, actual);
            }
        }
    }
}
