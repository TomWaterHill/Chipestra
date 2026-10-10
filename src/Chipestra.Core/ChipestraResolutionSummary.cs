// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System.Runtime.InteropServices;

namespace Chipestra.Core
{
    /// <summary>
    /// Mirrors the native ChipestraResolutionSummary C ABI struct (chipestra_abi.h):
    /// blittable, no custom marshaling needed, same as
    /// <see cref="AudioOutputDiagnostics"/>. <see cref="Ok"/> is nonzero iff every
    /// Required resource resolved successfully; <see cref="ResolvedCount"/>/
    /// <see cref="FailureCount"/> are the valid index ranges for
    /// <c>chipestra_resource_resolution_get_resolved</c>/<c>chipestra_resource_resolution_get_failure</c>.
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct ChipestraResolutionSummary
    {
        public byte Ok;
        public uint ResolvedCount;
        public uint FailureCount;
    }
}
