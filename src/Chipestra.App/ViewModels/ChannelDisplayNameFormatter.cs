// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System.Collections.Generic;
using System.Diagnostics;
using System.Text.RegularExpressions;

namespace Chipestra.App.ViewModels
{
    /// <summary>
    /// Reformats each active channel's raw Native DeviceName/ChannelName into the two-row
    /// Channel Strip display: a device-name row (the chip model, with a "-N" suffix only when
    /// that model appears more than once in the Machine) and a ch row ("category Nch", where the
    /// category word follows each chip manufacturer's naming convention and N restarts at 1 for
    /// every physical chip instance).
    ///
    /// Pure presentation-layer transformation. Instance numbers are parsed from markers in the
    /// raw strings (e.g. "YM1"/"YM2", "AY1".."AY5", "#1"/"#2") rather than from registration
    /// order, so reordering a Machine's channel registration cannot renumber the display.
    ///
    /// Input matching none of the known patterns falls back to the raw strings unchanged
    /// (multi-instance ambiguity is then resolved by first-encounter order) and fires
    /// <see cref="Debug.Assert"/> as a developer-only signal (compiled out of Release builds).
    /// </summary>
    internal static class ChannelDisplayNameFormatter
    {
        private static readonly Regex TrailingDigits = new(@"(\d+)\s*$", RegexOptions.Compiled);
        private static readonly Regex AyPattern = new(@"^AY(\d+)\s+(\d+)$", RegexOptions.Compiled);
        private static readonly Regex DariusYm2203Pattern = new(@"^YM(\d+)\s+(FM|PSG)(\d+)$", RegexOptions.Compiled);
        private static readonly Regex PlainFmSsgPattern = new(@"^(FM|SSG)(\d+)$", RegexOptions.Compiled);
        private static readonly Regex Ym2610Pattern = new(@"^(ADPCM-A|ADPCM-B|FM|SSG)\s*(\d*)$", RegexOptions.Compiled);
        private static readonly Regex OkiInstancePattern = new(@"#(\d+)", RegexOptions.Compiled);

        /// <summary>One parsed entry: the real chip model, its instance token within
        /// this Machine (null if the model is known to never need numbering, e.g. it
        /// always has exactly one instance on every board that uses it), the ch-row
        /// category word, and the ch-row channel number.</summary>
        private readonly struct Parsed
        {
            public Parsed(string model, string? instanceToken, string category, int channelNumber)
            {
                Model = model;
                InstanceToken = instanceToken;
                Category = category;
                ChannelNumber = channelNumber;
            }

            public string Model { get; }
            public string? InstanceToken { get; }
            public string Category { get; }
            public int ChannelNumber { get; }
        }

        /// <summary>
        /// Formats every entry in <paramref name="raw"/> (in order) into (deviceDisplay,
        /// channelDisplay) pairs, e.g. ("YM2203-1", "FM 1ch"). Call once per Rebind()
        /// with every ACTIVE channel's raw strings - the per-model instance count (and
        /// therefore whether "-N" is shown at all) depends on every channel in the
        /// Machine, not just one.
        /// </summary>
        public static IReadOnlyList<(string DeviceDisplay, string ChannelDisplay)> Format(
            IReadOnlyList<(string DeviceName, string ChannelName)> raw)
        {
            var parsed = new Parsed[raw.Count];
            var fallbackInstanceByRawDeviceName = new Dictionary<string, string>();
            for (int i = 0; i < raw.Count; i++)
            {
                parsed[i] = ParseOne(raw[i].DeviceName, raw[i].ChannelName, fallbackInstanceByRawDeviceName);
            }

        // Distinct instance tokens seen per model; only their count decides whether "-N" is shown.
        // The number shown is always the parsed token itself, never a re-indexed position.
            var distinctInstancesByModel = new Dictionary<string, HashSet<string>>();
            foreach (Parsed p in parsed)
            {
                if (p.InstanceToken == null)
                {
                    continue;
                }
                if (!distinctInstancesByModel.TryGetValue(p.Model, out HashSet<string>? set))
                {
                    set = new HashSet<string>();
                    distinctInstancesByModel[p.Model] = set;
                }
                set.Add(p.InstanceToken);
            }

            var result = new (string, string)[raw.Count];
            for (int i = 0; i < raw.Count; i++)
            {
                Parsed p = parsed[i];
                bool needsSuffix = p.InstanceToken != null
                    && distinctInstancesByModel.TryGetValue(p.Model, out HashSet<string>? set)
                    && set.Count > 1;
                string deviceDisplay = needsSuffix ? $"{p.Model}-{p.InstanceToken}" : p.Model;
                string channelDisplay = $"{p.Category} {p.ChannelNumber}ch";
                result[i] = (deviceDisplay, channelDisplay);
            }
            return result;
        }

        private static Parsed ParseOne(string deviceName, string channelName,
                                        Dictionary<string, string> fallbackInstanceByRawDeviceName)
        {
            Match m;

            switch (deviceName)
            {
                case "DAC1":
                    return new Parsed("YM3012", "1", "DAC", 1);
                case "DAC2":
                    return new Parsed("YM3012", "2", "DAC", 1);
                case "CUS30":
                    return new Parsed("CUS30", null, "WSG", TrailingDigitOrOne(channelName));
                case "VLM5030":
                    return new Parsed("VLM5030", null, "VLM", 1);
                case "K005289":
                    return new Parsed("K005289", null, "SCC", TrailingDigitOrOne(channelName));
                case "MSM5205":
                    return new Parsed("MSM5205", null, "ADPCM", 1);
                case "i8039":
                    return new Parsed("i8039", null, "DAC", 1);
                case "SegaPCM":
                    return new Parsed("SegaPCM", null, "PCM", TrailingDigitOrOne(channelName));
                case "YM2610":
                    m = Ym2610Pattern.Match(channelName);
                    if (m.Success)
                    {
                        string category = m.Groups[1].Value;
                        int ch = m.Groups[2].Success && m.Groups[2].Value.Length > 0
                            ? int.Parse(m.Groups[2].Value)
                            : 1;
                        return new Parsed("YM2610", null, category, ch);
                    }
                    break;
                case "YM2151":
                    return new Parsed("YM2151", null, "FM", TrailingDigitOrOne(channelName));
                case "AY8910":
                    m = AyPattern.Match(channelName);
                    if (m.Success)
                    {
                        return new Parsed("AY8910", m.Groups[1].Value, "PSG", int.Parse(m.Groups[2].Value));
                    }
                    break;
                case "YM2203":
                    m = DariusYm2203Pattern.Match(channelName);
                    if (m.Success)
                    {
                        string category = m.Groups[2].Value == "PSG" ? "SSG" : "FM";
                        return new Parsed("YM2203", m.Groups[1].Value, category, int.Parse(m.Groups[3].Value));
                    }
                    m = PlainFmSsgPattern.Match(channelName);
                    if (m.Success)
                    {
                        return new Parsed("YM2203", null, m.Groups[1].Value, int.Parse(m.Groups[2].Value));
                    }
                    break;
                default:
                    if (deviceName.StartsWith("OKIM6295", System.StringComparison.Ordinal))
                    {
                        Match inst = OkiInstancePattern.Match(deviceName);
                        if (inst.Success)
                        {
                            return new Parsed("OKIM6295", inst.Groups[1].Value, "ADPCM", TrailingDigitOrOne(channelName));
                        }
                    }
                    break;
            }

            // Fallback for an unrecognized pattern: never crash; Debug.Assert flags it for developers.
            Debug.Assert(false,
                $"ChannelDisplayNameFormatter: unrecognized DeviceName '{deviceName}' / ChannelName '{channelName}' " +
                "- falling back to raw strings with registration-order instance numbering. " +
                "Add a dedicated parsing rule for this chip's naming convention.");

            if (!fallbackInstanceByRawDeviceName.TryGetValue(deviceName, out string? fallbackInstance))
            {
                fallbackInstance = (fallbackInstanceByRawDeviceName.Count + 1).ToString();
                fallbackInstanceByRawDeviceName[deviceName] = fallbackInstance;
            }
            Match trailing = TrailingDigits.Match(channelName);
            int fallbackCh = trailing.Success ? int.Parse(trailing.Groups[1].Value) : 1;
            string fallbackCategory = trailing.Success ? channelName[..trailing.Index].TrimEnd() : channelName;
            if (fallbackCategory.Length == 0)
            {
                fallbackCategory = channelName;
            }
            return new Parsed(deviceName, fallbackInstance, fallbackCategory, fallbackCh);
        }

        private static int TrailingDigitOrOne(string channelName)
        {
            Match m = TrailingDigits.Match(channelName);
            return m.Success ? int.Parse(m.Groups[1].Value) : 1;
        }
    }
}
