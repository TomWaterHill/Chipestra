// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using System.Globalization;
using System.Windows;
using System.Windows.Data;
using Chipestra.App.Converters;
using Xunit;

namespace Chipestra.App.Tests
{
    public class FractionAndSizeConvertersTests
    {
        [Theory]
        [InlineData(0.0, 100.0, 100.0)]
        [InlineData(0.5, 100.0, 50.0)]
        [InlineData(1.0, 100.0, 0.0)]
        [InlineData(0.25, 200.0, 150.0)]
        public void FractionAndSizeToInverseSizeConverter_Convert_ReturnsCorrectValue(double fraction, double totalSize, double expected)
        {
            var converter = new FractionAndSizeToInverseSizeConverter();
            var values = new object[] { fraction, totalSize };

            var result = converter.Convert(values, typeof(double), null, CultureInfo.CurrentCulture);

            Assert.Equal(expected, result);
        }

        [Fact]
        public void FractionAndSizeToInverseSizeConverter_Convert_NullOrNonDoubleInputs_ReturnsCorrectFallbackValues()
        {
            var converter = new FractionAndSizeToInverseSizeConverter();

            // First value is not a double, second is valid: fraction=0.0, totalSize=100.0
            var result1 = converter.Convert(new object[] { "not_a_double", 100.0 }, typeof(double), null, CultureInfo.CurrentCulture);
            Assert.Equal(100.0, result1);

            // First value is valid, second is not a double: fraction=0.5, totalSize=0.0
            var result2 = converter.Convert(new object[] { 0.5, "not_a_double" }, typeof(double), null, CultureInfo.CurrentCulture);
            Assert.Equal(0.0, result2);

            // Both values are not doubles: fraction=0.0, totalSize=0.0
            var result3 = converter.Convert(new object[] { "not_a_double", "also_not_a_double" }, typeof(double), null, CultureInfo.CurrentCulture);
            Assert.Equal(0.0, result3);

            // Empty array: fraction=0.0, totalSize=0.0
            var result4 = converter.Convert(new object[0], typeof(double), null, CultureInfo.CurrentCulture);
            Assert.Equal(0.0, result4);
        }

        [Fact]
        public void FractionAndSizeToInverseSizeConverter_ConvertBack_ThrowsNotSupportedException()
        {
            var converter = new FractionAndSizeToInverseSizeConverter();
            Assert.Throws<NotSupportedException>(() => converter.ConvertBack(null, null, null, CultureInfo.CurrentCulture));
        }

        [Theory]
        [InlineData(0.0, 100.0, 0.0)]
        [InlineData(0.5, 100.0, 50.0)]
        [InlineData(1.0, 100.0, 100.0)]
        [InlineData(0.25, 200.0, 50.0)]
        public void FractionAndSizeToBottomMarginConverter_Convert_ReturnsCorrectValue(double fraction, double totalHeight, double expectedBottom)
        {
            var converter = new FractionAndSizeToBottomMarginConverter();
            var values = new object[] { fraction, totalHeight };

            var result = converter.Convert(values, typeof(Thickness), null, CultureInfo.CurrentCulture);

            var thickness = Assert.IsType<Thickness>(result);
            Assert.Equal(0.0, thickness.Left);
            Assert.Equal(0.0, thickness.Top);
            Assert.Equal(0.0, thickness.Right);
            Assert.Equal(expectedBottom, thickness.Bottom);
        }

        [Fact]
        public void FractionAndSizeToBottomMarginConverter_Convert_NullOrNonDoubleInputs_ReturnsCorrectFallbackValues()
        {
            var converter = new FractionAndSizeToBottomMarginConverter();

            // First value is not a double, second is valid: fraction=0.0, totalHeight=100.0 -> Bottom=0.0*100.0=0.0
            var result1 = converter.Convert(new object[] { "not_a_double", 100.0 }, typeof(Thickness), null, CultureInfo.CurrentCulture);
            var thickness1 = Assert.IsType<Thickness>(result1);
            Assert.Equal(0.0, thickness1.Bottom);

            // First value is valid, second is not a double: fraction=0.5, totalHeight=0.0 -> Bottom=0.5*0.0=0.0
            var result2 = converter.Convert(new object[] { 0.5, "not_a_double" }, typeof(Thickness), null, CultureInfo.CurrentCulture);
            var thickness2 = Assert.IsType<Thickness>(result2);
            Assert.Equal(0.0, thickness2.Bottom);

            // Both values are not doubles: fraction=0.0, totalHeight=0.0 -> Bottom=0.0
            var result3 = converter.Convert(new object[] { "not_a_double", "also_not_a_double" }, typeof(Thickness), null, CultureInfo.CurrentCulture);
            var thickness3 = Assert.IsType<Thickness>(result3);
            Assert.Equal(0.0, thickness3.Bottom);

            // Empty array: fraction=0.0, totalHeight=0.0 -> Bottom=0.0
            var result4 = converter.Convert(new object[0], typeof(Thickness), null, CultureInfo.CurrentCulture);
            var thickness4 = Assert.IsType<Thickness>(result4);
            Assert.Equal(0.0, thickness4.Bottom);
        }

        [Fact]
        public void FractionAndSizeToBottomMarginConverter_ConvertBack_ThrowsNotSupportedException()
        {
            var converter = new FractionAndSizeToBottomMarginConverter();
            Assert.Throws<NotSupportedException>(() => converter.ConvertBack(null, null, null, CultureInfo.CurrentCulture));
        }
    }
}
