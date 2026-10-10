// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System.Runtime.InteropServices;

namespace Chipestra.Core
{
    /// <summary>
    /// Mirrors the native ChipestraLogicalChannelInfo C ABI struct (chipestra_abi.h). The
    /// fixed-size native char buffers are marshaled directly into strings via
    /// ByValTStr, same convention as <see cref="MachineInfo"/>.
    /// </summary>
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    public struct ChipestraLogicalChannelInfo
    {
        public uint ChannelId;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string DeviceName;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string ChannelName;

        public uint NativeSampleRate;
        public byte Muted;
        public float Pan;
        public float Gain;
        public float Peak;
        public float Rms;

        // True per-channel L/R meter values; Peak/Rms above stay mono. Order matches the native struct.
        public float PeakLeft;
        public float PeakRight;
        public float RmsLeft;
        public float RmsRight;

        // Peak before the user's Mute/Gain/Pan (still scaled by the board's RoutingGain). The
        // auto-stop-by-silence check reads this so muting never looks like the song ending.
        public float SourcePeak;
    }
}
