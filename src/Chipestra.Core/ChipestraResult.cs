// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
namespace Chipestra.Core
{
    /// <summary>Mirrors the native ChipestraResult C ABI enum (chipestra_abi.h).</summary>
    public enum ChipestraResult
    {
        Ok = 0,
        InvalidArgument = 1,
        InvalidState = 2,
        Internal = 3,
        NotFound = 4,
        // Resource resolution failures (ResourceResolutionFailure.Result). Append-only: do not
        // reorder or remove existing values.
        AmbiguousMatch = 5,
        HashMismatch = 6,
        Unsupported = 7,
        // The audio endpoint's mix format is not one WasapiSharedOutput can write;
        // Player.LastRejectedOutputFormat has the details.
        UnsupportedFormat = 8,
        // The loaded Machine does not accept this kind of command (e.g. a WSG sound-effect track on a game
        // whose sound driver has no such request).
        UnsupportedCommand = 9,
    }
}
