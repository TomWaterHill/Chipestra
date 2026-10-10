// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System.Runtime.InteropServices;

namespace Chipestra.Core
{
    /// <summary>
    /// Mirrors the native ChipestraMachineInfo C ABI struct (chipestra_abi.h). The fixed-size
    /// native char buffers are marshaled directly into strings via ByValTStr.
    /// </summary>
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    public struct MachineInfo
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string MachineId;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)]
        public string DisplayName;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 16)]
        public string Category;

        public uint VariantCount;

        /// <summary>
        /// MachineDefinition::Manufacturer - the field Library grouping (Category-&gt;Manufacturer-&gt;
        /// System-&gt;Game) should use, not <see cref="GameDefinition.Manufacturer"/> (informational only).
        /// </summary>
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string Manufacturer;
    }
}
