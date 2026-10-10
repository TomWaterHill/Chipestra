// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using System.Globalization;
using System.Windows;
using System.Windows.Data;

namespace Chipestra.App.Converters
{
    /// <summary>
    /// Same math as <see cref="FractionToBottomMarginConverter"/> (a bottom <see cref="Thickness"/> of
    /// <c>fraction * totalHeight</c>, positioning the peak-hold marker line), but the meter bar stretches to
    /// fill a flexible Grid row, so its total height is read at runtime from an ActualHeight binding
    /// instead of a static ConverterParameter.
    ///
    /// Bindings, in order: [0] the 0.0-1.0 hold-fraction, [1] the meter's own
    /// total height (its ActualHeight).
    /// </summary>
    public sealed class FractionAndSizeToBottomMarginConverter : IMultiValueConverter
    {
        public object Convert(object[] values, Type targetType, object parameter, CultureInfo culture)
        {
            double fraction = values.Length > 0 && values[0] is double f ? Math.Clamp(f, 0.0, 1.0) : 0.0;
            double totalHeight = values.Length > 1 && values[1] is double h ? h : 0.0;
            return new Thickness(0, 0, 0, fraction * totalHeight);
        }

        public object[] ConvertBack(object value, Type[] targetTypes, object parameter, CultureInfo culture) =>
            throw new NotSupportedException();
    }
}
