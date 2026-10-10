// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using System.Globalization;
using System.Windows.Data;

namespace Chipestra.App.Converters
{
    /// <summary>
    /// The level meter's 3-zone coloring (green/yellow/red) is a static, always-fully-rendered background
    /// that a same-colored "mask" element covers from the loud end inward, revealing only the portion up
    /// to the current fill level; only the mask's size changes per update.
    ///
    /// Converts a 0.0-1.0 fill fraction into the mask's size in pixels - the
    /// *unlit* remainder, i.e. <c>(1 - fraction) * totalSize</c>, given the
    /// bar's own total pixel size (matching the axis this is used on) as
    /// <c>ConverterParameter</c>. Bind to <c>Height</c> (mask
    /// <c>VerticalAlignment="Top"</c>) for a vertical bar - per-channel L/R
    /// meters, quiet at the bottom - or to <c>Width</c> (mask
    /// <c>HorizontalAlignment="Right"</c>) for a horizontal one - the master
    /// L/R meter, quiet at the left.
    /// </summary>
    public sealed class FractionToInverseSizeConverter : IValueConverter
    {
        public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        {
            double fraction = value is double d ? Math.Clamp(d, 0.0, 1.0) : 0.0;
            double totalSize = parameter != null && double.TryParse(parameter.ToString(), NumberStyles.Float, CultureInfo.InvariantCulture, out double s)
                ? s
                : 0.0;
            return (1.0 - fraction) * totalSize;
        }

        public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture) =>
            throw new NotSupportedException();
    }
}
