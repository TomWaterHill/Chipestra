// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using System.IO;
using System.Windows;
using Microsoft.Win32;
using Chipestra.App.Settings;
using Chipestra.App.ViewModels;

namespace Chipestra.App
{
    /// <summary>
    /// Settings modal dialog. Folder picking and the OK/Cancel close behavior live here (view-layer
    /// concerns) rather than in <see cref="SettingsViewModel"/>, which only holds the edited values.
    /// </summary>
    public partial class SettingsWindow : Window
    {
        private readonly SettingsViewModel _viewModel;

        public SettingsWindow(SettingsViewModel viewModel)
        {
            InitializeComponent();
            _viewModel = viewModel;
            DataContext = _viewModel;
        }

        private void BrowseButton_Click(object sender, RoutedEventArgs e)
        {
            var dialog = new OpenFolderDialog
            {
                Title = "Select ROM Root",
            };
            if (!string.IsNullOrWhiteSpace(_viewModel.RomRoot))
            {
                dialog.InitialDirectory = _viewModel.RomRoot;
            }

            if (dialog.ShowDialog(this) == true)
            {
                _viewModel.RomRoot = dialog.FolderName;
            }
        }

        private void OkButton_Click(object sender, RoutedEventArgs e)
        {
            DialogResult = true;
            Close();
        }

        private void CancelButton_Click(object sender, RoutedEventArgs e)
        {
            DialogResult = false;
            Close();
        }

        // The License window shows THIRD-PARTY-NOTICES.md, licenses/*.txt and LICENSE, all shipped
        // next to the EXE (see LicenseTextBuilder); the version heading is formatted by VersionFormatter.
        private void LicenseButton_Click(object sender, RoutedEventArgs e)
        {
            string licenseText = LicenseTextBuilder.BuildLicenseText(AppContext.BaseDirectory);
            string versionText = $"Chipestra {VersionFormatter.CurrentDisplay()}";

            var licenseWindow = new LicenseWindow(versionText, licenseText) { Owner = this };
            licenseWindow.ShowDialog();
        }
    }
}
