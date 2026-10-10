// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using Chipestra.Core;

namespace Chipestra.App.Settings
{
    /// <summary>
    /// Persisted app-level settings: the ROM Root path and preferred audio output, set from the
    /// Settings dialog. See <see cref="AppSettingsStore"/> for the persistence mechanism/location.
    /// </summary>
    public sealed class AppSettings
    {
        /// <summary>
        /// ROM Root directory - a user-local filesystem path, never hardcoded. Null/empty
        /// means "not configured yet" - <see cref="ViewModels.MainViewModel"/>
        /// refuses to attempt <see cref="Player.LoadGame"/> until this is set,
        /// rather than silently loading a Machine with no ROM data.
        /// </summary>
        public string? RomRoot { get; set; }

        /// <summary>Which output <see cref="Player.SelectOutput"/> is called with after a successful LoadGame.</summary>
        public OutputKind OutputKind { get; set; } = OutputKind.WasapiShared;

        /// <summary>
        /// Requested output sample rate in Hz, 0 = auto (follow the device; also what a settings.json
        /// without this field gets). Advisory in shared mode - see <see cref="OutputRateOptions"/>.
        /// </summary>
        public uint OutputRateHz { get; set; }

        /// <summary>
        /// Last-used main window bounds, saved on close and restored on the next launch (see
        /// <see cref="MainWindow"/>). All four are null together on first run/a fresh settings.json;
        /// the caller then falls back to MainWindow.xaml's own Width/Height defaults.
        /// </summary>
        public double? WindowWidth { get; set; }
        public double? WindowHeight { get; set; }
        public double? WindowLeft { get; set; }
        public double? WindowTop { get; set; }

        /// <summary>
        /// Last-used width of the Library tree column (left of the Library/track-list GridSplitter),
        /// saved on close and restored on the next launch. Null means "not saved yet" -
        /// MainWindow.xaml's own ColumnDefinition Width is the fallback.
        /// </summary>
        public double? LibraryColumnWidth { get; set; }
    }
}
