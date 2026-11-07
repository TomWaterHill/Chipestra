// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
namespace Chipestra.Core
{
    /// <summary>
    /// The console analog-stage low-pass filter applied to the channels a Machine marks as filtered
    /// (the Mega Drive's FM and PSG). The numeric values are the native ChipestraAnalogFilterMode values.
    /// </summary>
    public enum AnalogFilterMode
    {
        Off = 0,
        Model1 = 1,
        Model2 = 2,
    }
}
