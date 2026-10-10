// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System.Runtime.InteropServices;

namespace Chipestra.Core
{
    /// <summary>
    /// Mirrors the native ChipestraMasterState C ABI struct (chipestra_abi.h) field for
    /// field. Blittable: marshaled directly, no custom marshaling needed.
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct ChipestraMasterState
    {
        public float Gain;
        public byte Muted;
        public float PeakLeft;
        public float PeakRight;
        public float RmsLeft;
        public float RmsRight;
    }
}
