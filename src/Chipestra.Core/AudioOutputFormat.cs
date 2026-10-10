// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System.Runtime.InteropServices;

namespace Chipestra.Core
{
    /// <summary>Mirrors the native ChipestraSampleFormat C ABI enum (chipestra_abi.h).</summary>
    public enum SampleFormat : uint
    {
        Other = 0,
        Pcm = 1,
        Float = 2,
    }

    /// <summary>
    /// Mirrors the native ChipestraAudioOutputFormat C ABI struct (chipestra_abi.h) field for field: an
    /// audio endpoint's mix format as IAudioClient::GetMixFormat reports it. Blittable.
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct AudioOutputFormat
    {
        /// <summary>What the output-rate setting asked for (0 = auto).</summary>
        public uint RequestedRateHz;
        public uint EndpointRateHz;
        public uint BitsPerSample;
        public uint ValidBits;
        public uint Channels;
        public SampleFormat SampleFormat;
        public uint IsExtensibleRaw;
        public uint SelectionHonoredRaw;

        public readonly bool IsExtensible => IsExtensibleRaw != 0;

        /// <summary>True if the requested rate was 0 (auto) or equals the endpoint's rate.</summary>
        public readonly bool SelectionHonored => SelectionHonoredRaw != 0;

        /// <summary>e.g. "48000 Hz / 32bit float / 2ch" (what the UI shows for the output).</summary>
        public readonly string Describe()
        {
            string kind = SampleFormat switch
            {
                SampleFormat.Float => "float",
                SampleFormat.Pcm => "PCM",
                _ => "other",
            };
            return $"{EndpointRateHz} Hz / {BitsPerSample}bit {kind} / {Channels}ch";
        }
    }
}
