// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;

namespace Chipestra.App.ViewModels
{
    public static class PlayTimeFormatter
    {
        /// <summary>
        /// Formats a TimeSpan as HH:MM:SS.sss with zero-padded components.
        /// </summary>
        public static string Format(TimeSpan position)
        {
            var hours = position.Hours + position.Days * 24;
            var minutes = position.Minutes;
            var seconds = position.Seconds;
            var milliseconds = position.Milliseconds;

            return $"{hours:D2}:{minutes:D2}:{seconds:D2}.{milliseconds:D3}";
        }
    }
}
