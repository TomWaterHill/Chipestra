// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System.Collections.Generic;
using System.Linq;

namespace Chipestra.Core
{
    /// <summary>
    /// Outcome of resolving one <see cref="RomResource"/> against a ROM root
    /// directory, via Native's ResourceResolver over the C ABI
    /// (<c>chipestra_resolve_resources</c>). Mirrors <c>ChipestraResult</c>'s
    /// resolution-specific values - never <see cref="Ok"/>/
    /// <see cref="InvalidArgument"/>/etc. from the general <see cref="ChipestraResult"/>
    /// set, since Resolved/NotFound/AmbiguousMatch/HashMismatch/Unsupported is
    /// exactly what a resolution outcome can be.
    /// </summary>
    public enum RomResolutionStatus
    {
        Resolved,
        NotFound,
        AmbiguousMatch,
        HashMismatch,
        Unsupported,
    }

    /// <summary>One resource's resolution outcome.</summary>
    public sealed class RomResolutionEntry
    {
        public required RomResource Resource { get; init; }

        public required RomResolutionStatus Status { get; init; }

        /// <summary>
        /// Human-readable diagnostic for a non-<see cref="RomResolutionStatus.Resolved"/>
        /// status (Native's ResourceResolutionFailure::Detail, via
        /// <c>chipestra_resource_resolution_get_failure</c>) - not part of any stable
        /// contract, do not match on its text. Null when <see cref="Status"/> is
        /// Resolved.
        /// </summary>
        public string? Detail { get; init; }
    }

    /// <summary>
    /// A <see cref="GameDefinition"/>'s resources resolved against one ROM root
    /// directory. Built by <see cref="NativeRuntimeSession.LoadMachineWithResources"/> as part
    /// of <see cref="Player.LoadGame"/>, never auto-selecting among multiple
    /// candidates for the same resource.
    /// </summary>
    public sealed class RomResolutionResult
    {
        public required IReadOnlyList<RomResolutionEntry> Entries { get; init; }

        public bool AllResolved => Entries.All(e => e.Status == RomResolutionStatus.Resolved);
    }
}
