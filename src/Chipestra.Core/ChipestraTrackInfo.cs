// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System.Runtime.InteropServices;

namespace Chipestra.Core
{
    /// <summary>
    /// Mirrors the native ChipestraTrackInfo C ABI struct (chipestra_abi.h): one &lt;track&gt;
    /// entry. <see cref="Id"/>/<see cref="Command"/> are ASCII-only identifier-style
    /// fields by this project's own convention and marshal directly into strings via
    /// ByValTStr - <see cref="Command"/> stays an opaque string here too (e.g. "0x92")
    /// - Native never interprets it; parsing it into the raw byte
    /// <c>chipestra_send_command</c> expects is a Managed Core concern. <see cref="Title"/>
    /// is free text and can be non-ASCII (e.g. a Japanese track title), so it is a raw byte buffer
    /// decoded explicitly as UTF-8 by <see cref="GameDefinitionXml"/> (see ChipestraGameInfo.Title).
    /// </summary>
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    public struct ChipestraTrackInfo
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string Id;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)]
        public string Command;

        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 128)]
        public byte[] Title;

        /// <summary>Mirrors ChipestraTrackInfo::is_control_command (0/1) - see <see cref="TrackDefinition.IsControlCommand"/>.</summary>
        public byte IsControlCommand;

        /// <summary>Mirrors ChipestraTrackInfo::loops (0/1) - see <see cref="TrackDefinition.Loops"/>.</summary>
        public byte Loops;

        /// <summary>Mirrors ChipestraTrackInfo::is_wsg (0/1) - see <see cref="TrackDefinition.IsWsg"/>.</summary>
        public byte IsWsg;
    }
}
