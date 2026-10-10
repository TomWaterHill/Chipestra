// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
namespace Chipestra.Core
{
    /// <summary>
    /// One ROM/data file a <see cref="GameDefinition"/>'s Machine needs, as declared
    /// in the game definition XML's &lt;resources&gt;&lt;rom&gt; elements.
    /// Mirrors the native RomResourceDefinition (game_definition.h) field for field -
    /// built by <see cref="GameDefinitionXml"/> from Native's own XML parser.
    /// </summary>
    public sealed class RomResource
    {
        /// <summary>&lt;rom name="..."&gt; - the logical resource name (e.g. "soundcpu").</summary>
        public required string Name { get; init; }

        /// <summary>&lt;rom file="..."&gt; - the expected on-disk file name.</summary>
        public required string FileName { get; init; }

        /// <summary>&lt;rom size="..."&gt;, in bytes, if declared.</summary>
        public long? SizeBytes { get; init; }

        /// <summary>&lt;rom crc32="..."&gt; as a hex string, if declared.</summary>
        public string? Crc32 { get; init; }

        /// <summary>&lt;rom sha1="..."&gt; as a hex string, if declared.</summary>
        public string? Sha1 { get; init; }
    }
}
