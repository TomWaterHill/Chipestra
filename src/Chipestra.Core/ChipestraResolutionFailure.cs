// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System.Runtime.InteropServices;

namespace Chipestra.Core
{
    /// <summary>
    /// Mirrors the native ChipestraResolutionFailure C ABI struct (chipestra_abi.h): why
    /// one declared resource failed to resolve. The fixed-size native char buffers
    /// are marshaled directly into strings via ByValTStr. <see cref="Result"/> is
    /// one of <see cref="ChipestraResult.NotFound"/>/<see cref="ChipestraResult.AmbiguousMatch"/>/
    /// <see cref="ChipestraResult.HashMismatch"/>/<see cref="ChipestraResult.Unsupported"/> -
    /// never <see cref="ChipestraResult.Ok"/>, since this only ever describes a failure.
    /// <see cref="Detail"/> is a human-readable diagnostic, not part of any stable
    /// contract - do not match on its text.
    /// </summary>
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    public struct ChipestraResolutionFailure
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string LogicalName;

        public ChipestraResult Result;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
        public string Detail;
    }
}
