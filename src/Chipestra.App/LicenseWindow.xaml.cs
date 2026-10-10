// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System.Windows;

namespace Chipestra.App
{
    /// <summary>License + version info window, opened from SettingsWindow's License button.</summary>
    public partial class LicenseWindow : Window
    {
        public LicenseWindow(string versionText, string licenseText)
        {
            InitializeComponent();
            VersionTextBlock.Text = versionText;
            LicenseTextBox.Text = licenseText;
        }

        private void CloseButton_Click(object sender, RoutedEventArgs e) => Close();
    }
}
