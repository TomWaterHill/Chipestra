// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
namespace Chipestra.Core
{
    /// <summary>
    /// Mirrors the native MusicCommandKind enum (chipestra_abi.h), as the raw
    /// byte value <see cref="NativeMethods.chipestra_send_command"/> takes - the native
    /// type itself is a C++-only <c>enum class</c> and never crosses the C ABI.
    /// </summary>
    public enum MusicCommandKind : byte
    {
        /// <summary>Select/start a track; Value carries an abstract track number.</summary>
        TrackSelect = 0,

        /// <summary>Stop playback.</summary>
        Stop = 1,

        /// <summary>
        /// Value (and optionally Auxiliary) are passed through as-is - the target
        /// board's own raw command protocol. Every board wired up so far (SEGA
        /// X-Board) uses this: a <see cref="TrackDefinition.Command"/> read from a
        /// game definition XML is already the real hardware command byte.
        /// </summary>
        Raw = 2,

        /// <summary>
        /// Value is a WSG sound-effect number (the game's own test-screen number, from a
        /// <c>&lt;track type="wsg"&gt;</c>). Only a Machine that accepts it plays it; others answer
        /// <see cref="ChipestraResult.UnsupportedCommand"/>.
        /// </summary>
        Wsg = 3,
    }
}
