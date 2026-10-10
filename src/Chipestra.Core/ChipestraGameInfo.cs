// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System.Runtime.InteropServices;

namespace Chipestra.Core
{
    /// <summary>
    /// Mirrors the native ChipestraGameInfo C ABI struct (chipestra_abi.h). Identifier-style fields are
    /// marshaled directly via ByValTStr, like <see cref="MachineInfo"/>. <see cref="Title"/> is free text
    /// that may be non-ASCII, so it is a raw byte buffer decoded explicitly as UTF-8 by
    /// <see cref="GameDefinitionXml"/> (ByValTStr would decode with the system ANSI code page).
    /// <see cref="ResourceCount"/>/<see cref="TrackCount"/> are the valid index ranges for
    /// <c>chipestra_game_definition_get_resource</c>/<c>chipestra_game_definition_get_track</c>.
    /// </summary>
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    public struct ChipestraGameInfo
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string GameId;

        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 128)]
        public byte[] Title;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string MachineId;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string VariantId;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string Manufacturer;

        public uint ResourceCount;
        public uint TrackCount;

        /// <summary>&lt;game archive="..."&gt;; "" if the XML omitted it.</summary>
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string Archive;
    }
}
