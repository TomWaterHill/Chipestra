// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System.Runtime.InteropServices;

namespace Chipestra.Core
{
    /// <summary>
    /// Mirrors the native ChipestraResolvedSource C ABI struct (chipestra_abi.h): where one resolved
    /// resource came from. <see cref="FoundByCrc"/> is nonzero if no file had an accepted name and the
    /// ZIP entry was found by its CRC-32 (or size); <see cref="Path"/> is the file used, or
    /// "&lt;zip path&gt;!&lt;entry name&gt;".
    /// </summary>
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    public struct ChipestraResolvedSource
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string LogicalName;

        public byte FoundByCrc;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 520)]
        public string Path;
    }
}
