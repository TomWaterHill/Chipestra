// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using System.Collections.Generic;
using System.Text;
using Chipestra.Core;

namespace Chipestra.App.Settings
{
    // Builds the text of the "XML Load Errors" message box shown once after startup when some
    // game definition XML files could not be loaded. Kept separate from the window code so the
    // count limit and the "and N more" line can be tested without showing anything.
    public static class XmlLoadErrorMessage
    {
        public const string Title = "Chipestra - XML Load Errors";
        public const int MaxShownFailures = 10;

        public static string Build(IReadOnlyList<GameDefinitionLoadFailure> failures, int maxShown = MaxShownFailures)
        {
            ArgumentNullException.ThrowIfNull(failures);
            var text = new StringBuilder();
            text.Append(failures.Count == 1
                ? "1 game definition file could not be loaded:\n\n"
                : $"{failures.Count} game definition files could not be loaded:\n\n");

            int shown = Math.Min(failures.Count, Math.Max(maxShown, 0));
            for (int i = 0; i < shown; i++)
            {
                text.Append($"{failures[i].FileName}\n    {failures[i].Reason}\n");
            }
            if (failures.Count > shown)
            {
                text.Append($"\n...and {failures.Count - shown} more.\n");
            }
            text.Append("\nThe other games are still available.");
            return text.ToString();
        }
    }
}
