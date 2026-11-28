// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System.Collections.Generic;
using Chipestra.App.Settings;
using Chipestra.Core;

namespace Chipestra.App.ViewModels
{
    /// <summary>
    /// Settings dialog state. Holds a working copy of <see cref="AppSettings"/> the dialog
    /// edits; the "Browse..." folder picker and OK/Cancel close behavior live in SettingsWindow's
    /// code-behind (this ViewModel deliberately doesn't reference dialog/window types).
    /// </summary>
    public sealed class SettingsViewModel : ViewModelBase
    {
        private string? _romRoot;

        public string? RomRoot
        {
            get => _romRoot;
            set => SetField(ref _romRoot, value);
        }

        private OutputKind _outputKind;

        public OutputKind OutputKind
        {
            get => _outputKind;
            set => SetField(ref _outputKind, value);
        }

        /// <summary>For a ComboBox - <see cref="OutputKind.None"/> is never a user-selectable choice (it means "nothing selected yet").</summary>
        public IReadOnlyList<OutputKind> AvailableOutputKinds { get; } = new[] { OutputKind.WasapiShared, OutputKind.Null };

        private uint _outputRateHz;

        public uint OutputRateHz
        {
            get => _outputRateHz;
            set => SetField(ref _outputRateHz, value);
        }

        /// <summary>For the output-rate ComboBox (value = <see cref="OutputRateHz"/>).</summary>
        public IReadOnlyList<OutputRateOption> AvailableOutputRates => OutputRateOptions.All;

        public string OutputRateNote => OutputRateOptions.SharedModeNote;

        private bool _levelMatching;

        /// <summary>The "Level matching" check box: each game is brought to a common loudness.</summary>
        public bool LevelMatching
        {
            get => _levelMatching;
            set => SetField(ref _levelMatching, value);
        }

        public string LevelMatchingNote => "Brings the songs of every game to about the same loudness (-26 LUFS), with a light limiter against clipping. Off leaves the sound as the original board makes it.";

        private AnalogFilterMode _megaDriveAnalogFilter;

        public AnalogFilterMode MegaDriveAnalogFilter
        {
            get => _megaDriveAnalogFilter;
            set => SetField(ref _megaDriveAnalogFilter, value);
        }

        /// <summary>For the Mega Drive / Genesis ComboBox (value = <see cref="MegaDriveAnalogFilter"/>).</summary>
        public IReadOnlyList<AnalogFilterOption> AvailableAnalogFilters => AnalogFilterOptions.All;

        public string AnalogFilterNote => AnalogFilterOptions.Note;

        public SettingsViewModel(AppSettings current)
        {
            _outputRateHz = OutputRateOptions.Normalize(current.OutputRateHz);
            _romRoot = current.RomRoot;
            _levelMatching = current.LevelMatching;
            _megaDriveAnalogFilter = AnalogFilterOptions.Normalize((int)current.MegaDriveAnalogFilter);
            _outputKind = current.OutputKind == OutputKind.None ? OutputKind.WasapiShared : current.OutputKind;
        }

        public AppSettings ToAppSettings()
        {
            return new AppSettings { RomRoot = RomRoot, OutputKind = OutputKind, OutputRateHz = OutputRateOptions.Normalize(OutputRateHz), LevelMatching = LevelMatching, MegaDriveAnalogFilter = MegaDriveAnalogFilter };
        }
    }
}
