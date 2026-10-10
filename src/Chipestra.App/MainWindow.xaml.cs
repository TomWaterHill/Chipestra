// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using System.Windows;
using System.Windows.Input;
using Chipestra.App.Settings;
using Chipestra.App.ViewModels;

namespace Chipestra.App
{
    public partial class MainWindow : Window
    {
        // 1% of Pan's -1..+1 range per wheel notch, i.e. one degree of needle movement.
        private const double PanWheelStep = 0.01;

        private readonly MainViewModel _viewModel;

        public MainWindow(MainViewModel viewModel)
        {
            InitializeComponent();
            Title = $"Chipestra {Chipestra.App.Settings.VersionFormatter.CurrentDisplay()}";
            _viewModel = viewModel;
            DataContext = _viewModel;
            RestoreWindowBounds(_viewModel.CurrentSettings);
            RestoreLibraryColumnWidth(_viewModel.CurrentSettings);
            Closed += (_, _) => _viewModel.Dispose();
            Closing += (_, _) =>
            {
                SaveWindowBounds();
                SaveLibraryColumnWidth();
            };
        }

        // Restores the previous session's window size/position if saved (all four properties are null on first run).
        private void RestoreWindowBounds(AppSettings settings)
        {
            if (settings.WindowWidth is double width && settings.WindowHeight is double height)
            {
                Width = Math.Max(MinWidth, width);
                Height = Math.Max(MinHeight, height);
            }
            // Left/Top are restored independently of Width/Height, and only
            // if they'd still land the window (mostly) on some current
            // monitor - a saved position from a since-removed second monitor
            // would otherwise put the window somewhere the user can't see or
            // reach it. Falling back to WPF's own default placement in that
            // case is safer than restoring a bad position.
            if (settings.WindowLeft is double left && settings.WindowTop is double top
                && WindowBoundsValidator.ShouldRestorePosition(left, top, SystemParameters.VirtualScreenWidth, SystemParameters.VirtualScreenHeight))
            {
                WindowStartupLocation = WindowStartupLocation.Manual;
                Left = left;
                Top = top;
            }
        }

        // Persists the window bounds directly to the shared AppSettings, so the size sticks even if Settings is never opened.
        // Skipped while maximized/minimized: that geometry is not the size to restore.
        private void SaveWindowBounds()
        {
            if (WindowState != WindowState.Normal)
            {
                return;
            }
            AppSettings settings = _viewModel.CurrentSettings;
            settings.WindowWidth = Width;
            settings.WindowHeight = Height;
            settings.WindowLeft = Left;
            settings.WindowTop = Top;
            AppSettingsStore.Save(settings);
        }

        // Restores the Library/track-list splitter position (the Library tree column's width).
        // Runs after RestoreWindowBounds so Width already reflects any restored window size.
        private void RestoreLibraryColumnWidth(AppSettings settings)
        {
            if (settings.LibraryColumnWidth is double width
                && WindowBoundsValidator.ShouldRestoreLibraryColumnWidth(width, Width))
            {
                LibraryColumn.Width = new GridLength(width);
            }
        }

        // Persists the splitter position; unlike the window bounds, WindowState does not affect this column's width.
        private void SaveLibraryColumnWidth()
        {
            AppSettings settings = _viewModel.CurrentSettings;
            settings.LibraryColumnWidth = LibraryColumn.Width.Value;
            AppSettingsStore.Save(settings);
        }

        // TreeView.SelectedItem is read-only and cannot be bound TwoWay, so the selection is pushed into the ViewModel here.
        private void LibraryTreeView_SelectedItemChanged(object sender, RoutedPropertyChangedEventArgs<object> e)
        {
            _viewModel.Library.SelectedNode = e.NewValue as LibraryTreeNode;
        }

        // The PAN knob is changed only by mouse wheel. A grayed-out (inactive) strip is disabled and gets no input events,
        // so no IsActive check is needed here.
        private void Knob_MouseWheel(object sender, MouseWheelEventArgs e)
        {
            if (sender is FrameworkElement { DataContext: ChannelStripViewModel strip })
            {
                strip.Pan += e.Delta > 0 ? PanWheelStep : -PanWheelStep;
                e.Handled = true;
            }
        }

        // Empty SearchText shows the full Library hierarchy again.
        private void ClearSearchButton_Click(object sender, RoutedEventArgs e)
        {
            _viewModel.Library.SearchText = string.Empty;
        }

        private void SettingsButton_Click(object sender, RoutedEventArgs e)
        {
            var settingsViewModel = new SettingsViewModel(_viewModel.CurrentSettings);
            var settingsWindow = new SettingsWindow(settingsViewModel) { Owner = this };
            bool? result = settingsWindow.ShowDialog();
            if (result == true)
            {
                _viewModel.ApplySettings(settingsViewModel.ToAppSettings());
            }
        }
    }
}
