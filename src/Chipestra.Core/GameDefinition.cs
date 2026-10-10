// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System.Collections.Generic;

namespace Chipestra.Core
{
    /// <summary>
    /// One Game's full definition (&lt;game&gt;): identity, which
    /// Machine/Variant it targets, the resources it needs, and its tracks. Parsed
    /// from a game definition XML by <see cref="GameDefinitionXml"/>.
    /// </summary>
    public sealed class GameDefinition
    {
        public required string GameId { get; init; }

        public required string Title { get; init; }

        /// <summary>MachineId this game targets (looked up via <see cref="NativeRuntimeSession.GetMachineInfo"/>/MachineRegistry).</summary>
        public required string MachineId { get; init; }

        /// <summary>VariantId, one of that Machine's registered Variants.</summary>
        public required string VariantId { get; init; }

        public string? Manufacturer { get; init; }

        /// <summary>&lt;game archive="..."&gt;: optional ROM-resolution archive
        /// scope hint (a ZIP stem or ROM Root subfolder name), or null if omitted.</summary>
        public string? Archive { get; init; }

        public required IReadOnlyList<RomResource> RequiredResources { get; init; }

        public required IReadOnlyList<TrackDefinition> Tracks { get; init; }
    }
}
