// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using System.Runtime.InteropServices;

namespace Chipestra.Core
{
    /// <summary>
    /// Mirrors the native ChipestraResolvedResource C ABI struct (chipestra_abi.h). Used
    /// both as an output (from <c>chipestra_resource_resolution_get_resolved</c>) and as
    /// an input (the array <see cref="NativeRuntimeSession.LoadMachine"/> passes to
    /// <c>chipestra_load_machine</c>) - the same shape works both directions.
    ///
    /// <see cref="Data"/> is a non-owning pointer into memory the
    /// ChipestraResourceResolutionHandle it came from owns (see chipestra_abi.h's own remarks
    /// on ChipestraResolvedResource): it stays valid only until that resolution handle is
    /// freed. Managed Core must free the resolution handle only after the
    /// <c>chipestra_load_machine</c> call built from it has returned.
    /// </summary>
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    public struct ChipestraResolvedResourceNative
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string LogicalName;

        public IntPtr Data;
        public ulong Size;
    }
}
