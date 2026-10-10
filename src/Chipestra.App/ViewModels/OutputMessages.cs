// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using Chipestra.Core;

namespace Chipestra.App.ViewModels
{
    /// <summary>User-facing text about the audio output (INFORMATION row, load-error message).</summary>
    public static class OutputMessages
    {
        /// <summary>
        /// "48000 Hz / 32bit float / 2ch", plus - only when the requested rate was not the endpoint's -
        /// a short "(N Hz not applied)" note.
        /// </summary>
        public static string DescribeOutput(AudioOutputFormat format)
        {
            string text = format.Describe();
            if (!format.SelectionHonored)
            {
                text += $" ({format.RequestedRateHz} Hz not applied)";
            }

            return text;
        }

        /// <summary>The audio-output-start failure message; names the rejected format when one is known.</summary>
        public static string DescribeOutputFailure(ChipestraResult result, AudioOutputFormat? rejected)
        {
            if (result == ChipestraResult.UnsupportedFormat && rejected.HasValue)
            {
                AudioOutputFormat f = rejected.Value;
                string kind = f.SampleFormat switch
                {
                    SampleFormat.Float => "float",
                    SampleFormat.Pcm => "PCM",
                    _ => "other format",
                };
                return $"Failed to start audio output: the device's mix format ({f.EndpointRateHz} Hz, {f.BitsPerSample}bit {kind}, " +
                       $"{f.Channels}ch) is not supported. Chipestra needs 32bit float or 16bit PCM - check the device's format in the Windows sound settings.";
            }

            return $"Failed to start audio output: {result}. Check that a playback device is available and enabled in the Windows sound settings.";
        }
    }
}
