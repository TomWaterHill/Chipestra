// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System.Runtime.InteropServices;

namespace Chipestra.Core
{
    /// <summary>
    /// Mirrors the native ChipestraAudioOutputDiagnostics C ABI struct (chipestra_abi.h) field
    /// for field. Blittable: marshaled directly, no custom marshaling needed.
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct AudioOutputDiagnostics
    {
        public ulong SubmittedFrameCount;
        public ulong WorkerWakeCount;
        public ulong QueueDrainCount;
        public ulong QueueOverflowCount;
        public ulong QueuedFrameCount;
        public ulong BufferFrameCapacity;
        public ulong LastPaddingFrames;
    }
}
