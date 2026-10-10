// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;

namespace Chipestra.App.ViewModels
{
    public static class PanKnobMath
    {
        /// <summary>
        /// Converts a pan value from -1.0 (full left) to +1.0 (full right) into an angle in degrees from -100 to +100.
        /// </summary>
        /// <param name="pan">The pan value, clamped to the range [-1.0, 1.0]</param>
        /// <returns>The corresponding angle in degrees</returns>
        public static double ToAngleDegrees(double pan)
        {
            pan = Math.Clamp(pan, -1.0, 1.0);
            return pan * 100.0;
        }
    }
}
