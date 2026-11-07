// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using System.Collections.Generic;
using System.Text.Json;
using System.Text.Json.Serialization;
using Chipestra.Core;

namespace Chipestra.App.Settings
{
    /// <summary>One choice in the Settings dialog's "Mega Drive / Genesis" analog filter ComboBox.</summary>
    public sealed record AnalogFilterOption(AnalogFilterMode Mode, string Label);

    /// <summary>The selectable analog filter modes and the tolerant reading of the saved value.</summary>
    public static class AnalogFilterOptions
    {
        public static IReadOnlyList<AnalogFilterOption> All { get; } = new[]
        {
            new AnalogFilterOption(AnalogFilterMode.Off, "Off"),
            new AnalogFilterOption(AnalogFilterMode.Model1, "Model 1 (Mega Drive / Genesis)"),
            new AnalogFilterOption(AnalogFilterMode.Model2, "Model 2 (Mega Drive 2 / Genesis Model 2)"),
        };

        /// <summary>Returns <paramref name="value"/> if it names a selectable mode, otherwise Off.</summary>
        public static AnalogFilterMode Normalize(int value) =>
            value >= (int)AnalogFilterMode.Off && value <= (int)AnalogFilterMode.Model2 ? (AnalogFilterMode)value : AnalogFilterMode.Off;

        public const string Note = "Low-pass filter of the console's analog output stage, applied to the Mega Drive sound. Takes effect immediately.";
    }

    /// <summary>
    /// Reads the saved filter mode leniently: a number is normalized, anything else (a string, a null, a
    /// nested value) reads as Off instead of making the whole settings file unreadable.
    /// </summary>
    public sealed class AnalogFilterModeJsonConverter : JsonConverter<AnalogFilterMode>
    {
        public override AnalogFilterMode Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
        {
            if (reader.TokenType == JsonTokenType.Number && reader.TryGetInt32(out int value))
            {
                return AnalogFilterOptions.Normalize(value);
            }
            reader.Skip();
            return AnalogFilterMode.Off;
        }

        public override void Write(Utf8JsonWriter writer, AnalogFilterMode value, JsonSerializerOptions options) =>
            writer.WriteNumberValue((int)value);
    }
}
