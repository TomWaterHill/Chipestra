// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
namespace Chipestra.Core
{
    /// <summary>
    /// One playable track within a <see cref="GameDefinition"/> (&lt;track&gt;).
    /// <see cref="Command"/> is the raw byte value the target board's own
    /// sound driver expects (Chipestra's Native Runtime never assigns meaning to
    /// it) - <see cref="Player.SelectTrack"/> sends it through unchanged as a
    /// <see cref="MusicCommandKind.Raw"/> command, matching how every board wired up
    /// so far (SEGA X-Board) actually uses its command latch.
    /// </summary>
    public sealed class TrackDefinition
    {
        /// <summary>
        /// Stable identifier callers select this track by (a game-definition-XML
        /// &lt;track id="..."&gt; attribute) - distinct from <see cref="Command"/> so
        /// callers never need to know the raw command byte.
        /// </summary>
        public required string Id { get; init; }

        public required string DisplayName { get; init; }

        public required byte Command { get; init; }

        /// <summary>
        /// True if this track is a control command (e.g. STOP), not a
        /// real song - a &lt;track type="control"&gt; XML attribute (optional,
        /// defaults to false).
        /// <see cref="Player.SelectTrack"/> still sends <see cref="Command"/> to
        /// the Machine (real hardware side effects, e.g. actually silencing
        /// whatever is playing, are kept) but deliberately stops short of
        /// Playing, so Play Time never advances for a track with no music.
        /// </summary>
        public bool IsControlCommand { get; init; } = false;

        /// <summary>
        /// True if this track is understood to loop indefinitely, like a
        /// typical arcade BGM - a &lt;track loops="false"&gt; XML attribute
        /// (optional, defaults to true). Only a track explicitly marked
        /// <c>false</c> is eligible for <see cref="Player.PollTrackEndBySilence"/>'s
        /// silence-based auto-stop - this default is a deliberate safety choice
        /// so a genuinely looping track's legitimate quiet passages can never be
        /// misdetected as "the track ended".
        /// </summary>
        public bool Loops { get; init; } = true;

        /// <summary>
        /// True if this track is a WSG sound-effect request - a <c>&lt;track type="wsg"&gt;</c> XML attribute
        /// (optional, defaults to false). <see cref="Command"/> is then the game's own test-screen number and
        /// <see cref="Player"/> sends it as <see cref="MusicCommandKind.Wsg"/> instead of Raw.
        /// </summary>
        public bool IsWsg { get; init; } = false;

        /// <summary>The kind of command <see cref="Player"/> sends for this track.</summary>
        public MusicCommandKind CommandKind => IsWsg ? MusicCommandKind.Wsg : MusicCommandKind.Raw;
    }
}
