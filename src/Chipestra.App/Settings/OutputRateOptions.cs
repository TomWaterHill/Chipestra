// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System.Collections.Generic;
using System.Linq;

namespace Chipestra.App.Settings
{
    /// <summary>One choice in the Settings dialog's "Output Sample Rate" ComboBox.</summary>
    public sealed record OutputRateOption(uint Hz, string Label);

    /// <summary>
    /// The selectable output sample rates (0 = auto). Mirrors the values the native
    /// chipestra_select_wasapi_shared_output accepts (chipestra_abi.h); anything else is invalid there, so
    /// <see cref="Normalize"/> maps a stray settings.json value back to auto instead of letting it
    /// reach the C ABI.
    /// </summary>
    public static class OutputRateOptions
    {
        public static IReadOnlyList<OutputRateOption> All { get; } = new[]
        {
            new OutputRateOption(0, "Auto (follow the device)"),
            new OutputRateOption(44100, "44100 Hz"),
            new OutputRateOption(48000, "48000 Hz"),
            new OutputRateOption(88200, "88200 Hz"),
            new OutputRateOption(96000, "96000 Hz"),
            new OutputRateOption(176400, "176400 Hz"),
            new OutputRateOption(192000, "192000 Hz"),
        };

        /// <summary>Returns <paramref name="hz"/> if it is one of the selectable values, otherwise 0 (auto).</summary>
        public static uint Normalize(uint hz) => All.Any(o => o.Hz == hz) ? hz : 0u;

        /// <summary>Shown under the rate ComboBox at all times (shared mode follows the Windows mixer).</summary>
        public const string SharedModeNote =
            "In shared mode, the Windows sound settings take priority. Applies when you switch to another game.";
    }
}
