// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System.Runtime.InteropServices;

namespace Chipestra.Core
{
    /// <summary>
    /// Mirrors the native ChipestraRequiredResourceInfo C ABI struct (chipestra_abi.h): one
    /// &lt;rom&gt; entry as declared in a game definition XML, not yet resolved against
    /// any ROM Root. The fixed-size native char buffers are marshaled directly into
    /// strings via ByValTStr. <see cref="SizeBytes"/>/<see cref="Crc32"/> are only
    /// meaningful when the corresponding <see cref="HasSizeBytes"/>/
    /// <see cref="HasCrc32"/> flag is nonzero; <see cref="Sha1"/> is empty ("") if
    /// the XML omitted it.
    /// </summary>
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    public struct ChipestraRequiredResourceInfo
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string LogicalName;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)]
        public string AcceptedName;

        public ulong SizeBytes;
        public byte HasSizeBytes;
        public uint Crc32;
        public byte HasCrc32;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string Sha1;

        public byte Required;

        /// <summary>Optional archive scope; "" means unscoped.</summary>
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string Archive;
    }
}
