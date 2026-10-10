// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
namespace Chipestra.Core
{
    /// <summary>Which <see cref="NativeRuntimeSession"/> audio output <see cref="Player"/> has selected, if any.</summary>
    public enum OutputKind
    {
        None,
        Null,
        WasapiShared,
    }
}
