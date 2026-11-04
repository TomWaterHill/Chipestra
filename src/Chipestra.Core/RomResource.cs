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

        /// <summary>&lt;rom archive="..."&gt;: the archive this resource is looked for in; null takes the game's archive.</summary>
        public string? Archive { get; init; }

        /// <summary>Further file CRC-32 values besides <see cref="Crc32"/> ("a|b" in the XML), hexadecimal separated by "|".</summary>
        public string? Crc32Alternatives { get; init; }

        /// <summary>&lt;rom crc_search="true"&gt;: look for a ZIP entry by CRC-32 when no file has an accepted name.</summary>
        public bool CrcSearch { get; init; }

        /// <summary>&lt;rom search_crc32="..."&gt;: CRC-32 values used only by that search.</summary>
        public string? SearchCrc32 { get; init; }

        /// <summary>&lt;rom search_size="..."&gt;: entry sizes used only by that search, when no CRC-32 matched.</summary>
        public string? SearchSize { get; init; }
    }
}
