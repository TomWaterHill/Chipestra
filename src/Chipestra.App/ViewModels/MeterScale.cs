// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;

namespace Chipestra.App.ViewModels
{
    /// <summary>
    /// Which of the meter's three color zones a dB level falls in. Top-level (not nested in
    /// <see cref="MeterScale"/>) so XAML can reference members via
    /// <c>{x:Static vm:MeterZone.Yellow}</c> in a DataTrigger (the peak-hold marker's color; the
    /// live bar is a static 3-zone background revealed by a mask). Public because it is the type
    /// of a public <see cref="ChannelStripViewModel"/> property (HoldZoneLeft/Right).
    /// </summary>
    public enum MeterZone
    {
        Green,
        Yellow,
        Red,
    }

    /// <summary>
    /// Shared linear-to-dB meter scaling, used by both <see cref="ChannelStripViewModel"/>'s
    /// per-channel meter and <see cref="ChannelMixerViewModel"/>'s master L/R meter so they read
    /// on the same scale.
    /// </summary>
    internal static class MeterScale
    {
        /// <summary>
        /// Meter floor, in dB. <see cref="ToFillFraction"/>'s zero point.
        /// </summary>
        public const double FloorDb = -90.0;

        /// <summary>Zone boundary, in dB - yellow starts here (up to <see cref="RedZoneStartDb"/>).</summary>
        public const double YellowZoneStartDb = -12.0;

        /// <summary>Zone boundary, in dB - red starts here (up to 0dBFS and beyond).</summary>
        public const double RedZoneStartDb = -6.0;

        /// <summary>20*log10(|linear|), floored at <see cref="FloorDb"/> so silence reads as the floor, not -infinity.</summary>
        public static double LinearToDb(float linear)
        {
            double magnitude = Math.Abs(linear);
            return magnitude <= 0.0 ? FloorDb : Math.Max(FloorDb, 20.0 * Math.Log10(magnitude));
        }

        /// <summary>Inverse of <see cref="LinearToDb"/> - used by <see cref="PeakHoldTracker"/>'s decay math.</summary>
        public static double DbToLinear(double db) => Math.Pow(10.0, db / 20.0);

        /// <summary><paramref name="db"/> remapped to 0.0 (<see cref="FloorDb"/> or quieter) - 1.0 (0dB, full scale).</summary>
        public static double ToFillFraction(double db) => Math.Clamp((db - FloorDb) / -FloorDb, 0.0, 1.0);

        /// <summary>
        /// Which color zone <paramref name="db"/> falls in - used for the peak-hold marker's color
        /// (the live bar's coloring comes from a static background instead).
        /// </summary>
        public static MeterZone GetZone(double db)
        {
            if (db >= RedZoneStartDb)
            {
                return MeterZone.Red;
            }
            if (db >= YellowZoneStartDb)
            {
                return MeterZone.Yellow;
            }
            return MeterZone.Green;
        }
    }
}
