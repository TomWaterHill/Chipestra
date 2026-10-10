// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
namespace Chipestra.Core
{
    /// <summary>
    /// Mirrors the Native Runtime instance's lifecycle state (not itself exposed across the C ABI,
    /// so <see cref="Player"/> tracks it locally). It only advances on a <see cref="ChipestraResult.Ok"/>
    /// response from the corresponding <see cref="NativeRuntimeSession"/> call; Native remains the
    /// single source of truth for which transitions are legal.
    /// </summary>
    public enum PlaybackState
    {
        Created,
        MachineLoaded,
        Ready,
        Playing,
        Paused,
    }
}
