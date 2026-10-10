// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using System.Globalization;
using System.Windows;
using System.Windows.Data;

namespace Chipestra.App.Converters
{
    /// <summary>
    /// Positions a thin peak-hold marker inside a fixed-height vertical meter bar. Converts a 0.0-1.0
    /// fill fraction (bound to <c>HoldFillFractionLeft</c>/<c>Right</c>) into a bottom
    /// <see cref="Thickness"/> for a <c>VerticalAlignment="Bottom"</c> element, given the bar's own
    /// pixel height as <c>ConverterParameter</c>.
    /// </summary>
    public sealed class FractionToBottomMarginConverter : IValueConverter
    {
        public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        {
            double fraction = value is double d ? Math.Clamp(d, 0.0, 1.0) : 0.0;
            double height = parameter != null && double.TryParse(parameter.ToString(), NumberStyles.Float, CultureInfo.InvariantCulture, out double h)
                ? h
                : 0.0;
            return new Thickness(0, 0, 0, fraction * height);
        }

        public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture) =>
            throw new NotSupportedException();
    }
}
