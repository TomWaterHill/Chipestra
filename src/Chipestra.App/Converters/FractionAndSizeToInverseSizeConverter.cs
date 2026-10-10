// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using System.Globalization;
using System.Windows.Data;

namespace Chipestra.App.Converters
{
    /// <summary>
    /// Same math as <see cref="FractionToInverseSizeConverter"/> (the mask's unlit-remainder size,
    /// <c>(1 - fraction) * totalSize</c>), but for a bar whose total size is not a fixed constant: the
    /// meter stretches to fill a flexible Grid row, so the size is read at runtime from an ActualHeight/
    /// ActualWidth binding (an <c>ElementName</c> binding to the meter Grid) instead of a ConverterParameter.
    ///
    /// Bindings, in order: [0] the 0.0-1.0 fill fraction, [1] the meter's own
    /// total size (its ActualHeight for a vertical bar, ActualWidth for a
    /// horizontal one) - see <see cref="FractionToInverseSizeConverter"/>'s
    /// own remarks for which Height/Width and alignment to bind the result to.
    /// </summary>
    public sealed class FractionAndSizeToInverseSizeConverter : IMultiValueConverter
    {
        public object Convert(object[] values, Type targetType, object parameter, CultureInfo culture)
        {
            double fraction = values.Length > 0 && values[0] is double f ? Math.Clamp(f, 0.0, 1.0) : 0.0;
            double totalSize = values.Length > 1 && values[1] is double s ? s : 0.0;
            return (1.0 - fraction) * totalSize;
        }

        public object[] ConvertBack(object value, Type[] targetTypes, object parameter, CultureInfo culture) =>
            throw new NotSupportedException();
    }
}
