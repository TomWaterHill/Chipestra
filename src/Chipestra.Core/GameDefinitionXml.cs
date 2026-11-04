// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Xml;
using System.Xml.Linq;

namespace Chipestra.Core
{
    /// <summary>
    /// Parses a game definition XML (e.g. afterburn2.xml)
    /// into a <see cref="GameDefinition"/>, via Native's own XML parser over the C ABI's
    /// GameDefinition group - <c>chipestra_parse_game_definition</c> +
    /// <c>chipestra_game_definition_get_info</c>/<c>_get_resource</c>/<c>_get_track</c>.
    /// This is a thin wrapper, so it agrees with <c>chipestra_resolve_resources</c> and
    /// Native's own tools on schema and validation.
    ///
    /// Schema:
    /// <code>
    /// &lt;chipestra&gt;
    ///   &lt;game id="..." title="..." machine="..." variant="..." manufacturer="..."&gt;
    ///     &lt;resources&gt;
    ///       &lt;rom name="..." file="..." size="..." crc32="..." sha1="..."/&gt;
    ///     &lt;/resources&gt;
    ///     &lt;tracks&gt;
    ///       &lt;track id="..." command="..." title="..."/&gt;
    ///     &lt;/tracks&gt;
    ///   &lt;/game&gt;
    /// &lt;/chipestra&gt;
    /// </code>
    /// <c>command</c> accepts either a decimal integer or a "0x"-prefixed
    /// hexadecimal one. Native stores it as an opaque string and never interprets it (what a
    /// command byte means is a convention of the target board's sound driver), so this class
    /// parses it into <see cref="TrackDefinition.Command"/>.
    /// </summary>
    public static class GameDefinitionXml
    {
        // Bounded buffer for chipestra_parse_game_definition's out_detail; the text is only
        // surfaced inside a thrown FormatException's message, never matched on.
        private const int DetailBufferSize = 256;

        public static GameDefinition Load(string filePath)
        {
            return Parse(File.ReadAllText(filePath));
        }

        /// <summary>
        /// Like <see cref="Load"/>, but a game whose <c>&lt;driver region="usa|europe"&gt;</c> lists several consoles
        /// becomes one game for each (see <see cref="ParseAll"/>).
        /// </summary>
        public static IReadOnlyList<GameDefinition> LoadAll(string filePath)
        {
            return ParseAll(File.ReadAllText(filePath));
        }

        /// <summary>
        /// A game that can be played on more than one console lists them in the <c>region</c> of its driver separated by
        /// '|' (<c>usa|europe</c>), and may give <c>warmup_frames</c> and <c>release_cycle</c> for each in the same order
        /// (<c>784|782</c>; one value serves all). This returns one game for each region: the same tracks and
        /// resources, the id <c>{id}@{region}</c>, the title <c>{title} (60Hz)</c> or <c>(50Hz)</c> (NTSC or PAL), and a
        /// driver with the values of its region alone. A game with one region (or none) is returned as it is.
        /// </summary>
        public static IReadOnlyList<GameDefinition> ParseAll(string xml)
        {
            GameDefinition game = Parse(xml);
            if (game.DriverXml == null) return new[] { game };
            XElement driver = XElement.Parse(game.DriverXml);
            string[] regions = (driver.Attribute("region")?.Value ?? string.Empty).Split('|');
            if (regions.Length < 2) return new[] { game };

            var games = new List<GameDefinition>();
            for (int i = 0; i < regions.Length; i++)
            {
                var copy = new XElement(driver);
                foreach (string name in new[] { "region", "warmup_frames", "release_cycle" })
                {
                    XAttribute? attribute = copy.Attribute(name);
                    if (attribute == null) continue;
                    string[] values = attribute.Value.Split('|');
                    if (values.Length != 1 && values.Length != regions.Length)
                        throw new FormatException($"<driver {name}>: expected one value or one for each of the {regions.Length} regions");
                    attribute.Value = values.Length == 1 ? values[0] : values[i];
                }
                string region = regions[i];
                string hertz = region == "europe" ? "50Hz" : "60Hz";
                games.Add(new GameDefinition
                {
                    GameId = $"{game.GameId}@{region}",
                    Title = $"{game.Title} ({hertz})",
                    MachineId = game.MachineId,
                    VariantId = game.VariantId,
                    Manufacturer = game.Manufacturer,
                    Archive = game.Archive,
                    DriverXml = copy.ToString(SaveOptions.DisableFormatting),
                    Region = region,
                    RequiredResources = game.RequiredResources,
                    Tracks = game.Tracks,
                });
            }
            return games;
        }

        public static GameDefinition Parse(string xml)
        {
            uint xmlLength = (uint)Encoding.UTF8.GetByteCount(xml);
            IntPtr detailBuffer = Marshal.AllocHGlobal(DetailBufferSize);
            try
            {
                ChipestraResult parseResult = NativeMethods.chipestra_parse_game_definition(
                    xml, xmlLength, out IntPtr handle, detailBuffer, DetailBufferSize);
                if (parseResult != ChipestraResult.Ok)
                {
                    string detail = Marshal.PtrToStringUTF8(detailBuffer) ?? string.Empty;
                    throw new FormatException(
                        string.IsNullOrEmpty(detail)
                            ? "game definition xml: failed to parse"
                            : $"game definition xml: {detail}");
                }

                try
                {
                    return ReadGameDefinition(handle, ExtractDriverXml(xml));
                }
                finally
                {
                    NativeMethods.chipestra_free_game_definition(handle);
                }
            }
            finally
            {
                Marshal.FreeHGlobal(detailBuffer);
            }
        }

        private static GameDefinition ReadGameDefinition(IntPtr handle, string? driverXml)
        {
            ChipestraResult infoResult = NativeMethods.chipestra_game_definition_get_info(handle, out ChipestraGameInfo info);
            if (infoResult != ChipestraResult.Ok)
            {
                throw new InvalidOperationException($"chipestra_game_definition_get_info failed: {infoResult}");
            }

            var resources = new List<RomResource>((int)info.ResourceCount);
            for (uint i = 0; i < info.ResourceCount; i++)
            {
                ChipestraResult resourceResult = NativeMethods.chipestra_game_definition_get_resource(handle, i, out ChipestraRequiredResourceInfo resourceInfo);
                if (resourceResult != ChipestraResult.Ok)
                {
                    throw new InvalidOperationException($"chipestra_game_definition_get_resource({i}) failed: {resourceResult}");
                }
                resources.Add(new RomResource
                {
                    Name = resourceInfo.LogicalName,
                    FileName = resourceInfo.AcceptedName,
                    SizeBytes = resourceInfo.HasSizeBytes != 0 ? (long)resourceInfo.SizeBytes : null,
                    Crc32 = resourceInfo.HasCrc32 != 0 ? resourceInfo.Crc32.ToString("x8", CultureInfo.InvariantCulture) : null,
                    Sha1 = string.IsNullOrEmpty(resourceInfo.Sha1) ? null : resourceInfo.Sha1,
                    Archive = string.IsNullOrEmpty(resourceInfo.Archive) ? null : resourceInfo.Archive,
                    Crc32Alternatives = string.IsNullOrEmpty(resourceInfo.Crc32Alternatives) ? null : resourceInfo.Crc32Alternatives,
                    CrcSearch = resourceInfo.CrcSearch != 0,
                    SearchCrc32 = string.IsNullOrEmpty(resourceInfo.SearchCrc32) ? null : resourceInfo.SearchCrc32,
                    SearchSize = string.IsNullOrEmpty(resourceInfo.SearchSize) ? null : resourceInfo.SearchSize,
                });
            }

            var tracks = new List<TrackDefinition>((int)info.TrackCount);
            for (uint i = 0; i < info.TrackCount; i++)
            {
                ChipestraResult trackResult = NativeMethods.chipestra_game_definition_get_track(handle, i, out ChipestraTrackInfo trackInfo);
                if (trackResult != ChipestraResult.Ok)
                {
                    throw new InvalidOperationException($"chipestra_game_definition_get_track({i}) failed: {trackResult}");
                }
                tracks.Add(new TrackDefinition
                {
                    Id = trackInfo.Id,
                    DisplayName = DecodeUtf8ZBuffer(trackInfo.Title),
                    Command = ParseCommandByte(trackInfo.Command),
                    IsControlCommand = trackInfo.IsControlCommand != 0,
                    Loops = trackInfo.Loops != 0,
                    IsWsg = trackInfo.IsWsg != 0,
                    IsEffect = trackInfo.IsEffect != 0,
                    Auxiliary = trackInfo.Auxiliary,
                });
            }

            return new GameDefinition
            {
                GameId = info.GameId,
                Title = DecodeUtf8ZBuffer(info.Title),
                MachineId = info.MachineId,
                VariantId = info.VariantId,
                Manufacturer = string.IsNullOrEmpty(info.Manufacturer) ? null : info.Manufacturer,
                Archive = string.IsNullOrEmpty(info.Archive) ? null : info.Archive,
                DriverXml = driverXml,
                Region = ExtractRegion(driverXml),
                RequiredResources = resources,
                Tracks = tracks,
            };
        }

        // The text of the first <game>'s <driver> element, or null if there is none. Native has already accepted
        // the document (it rejects a DOCTYPE), and the reader prohibits DTDs anyway.
        private static string? ExtractDriverXml(string xml)
        {
            var settings = new XmlReaderSettings { DtdProcessing = DtdProcessing.Prohibit, XmlResolver = null };
            using var reader = XmlReader.Create(new StringReader(xml), settings);
            XElement? driver = XDocument.Load(reader).Root?.Element("game")?.Element("driver");
            return driver?.ToString(SaveOptions.DisableFormatting);
        }

        private static string? ExtractRegion(string? driverXml)
        {
            if (driverXml == null) return null;
            string? region = XElement.Parse(driverXml).Attribute("region")?.Value;
            return string.IsNullOrEmpty(region) ? null : region;
        }

        private static byte ParseCommandByte(string text)
        {
            if (text.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
            {
                return Convert.ToByte(text[2..], 16);
            }
            return byte.Parse(text, CultureInfo.InvariantCulture);
        }

        // Decodes a fixed-size, null-terminated native char buffer as UTF-8 (see
        // ChipestraGameInfo.Title for why these fields are raw byte[] rather than ByValTStr).
        private static string DecodeUtf8ZBuffer(byte[] buffer)
        {
            int length = Array.IndexOf(buffer, (byte)0);
            if (length < 0)
            {
                length = buffer.Length;
            }
            return Encoding.UTF8.GetString(buffer, 0, length);
        }
    }
}
