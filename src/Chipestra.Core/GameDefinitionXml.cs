// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

namespace Chipestra.Core
{
    /// <summary>
    /// Parses a game definition XML (e.g. xml/afterburn2.xml)
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
                    return ReadGameDefinition(handle);
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

        private static GameDefinition ReadGameDefinition(IntPtr handle)
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
                RequiredResources = resources,
                Tracks = tracks,
            };
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
