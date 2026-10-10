// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using System.IO;
using System.Text.Json;

namespace Chipestra.App.Settings
{
    /// <summary>
    /// Loads/saves <see cref="AppSettings"/> as plain JSON under
    /// <c>%LOCALAPPDATA%\Chipestra\settings.json</c>. A per-user local file needs no registry or
    /// external config library (<see cref="System.Text.Json"/> is part of the .NET SDK). Never written
    /// to the repository: it is machine-local state, like the ROM Root path it stores.
    /// </summary>
    public static class AppSettingsStore
    {
        private static string SettingsFilePath => Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Chipestra", "settings.json");

        /// <summary>Reads settings from disk, or returns defaults if the file is absent, unreadable, or corrupt.</summary>
        public static AppSettings Load()
        {
            try
            {
                if (File.Exists(SettingsFilePath))
                {
                    string json = File.ReadAllText(SettingsFilePath);
                    AppSettings? settings = JsonSerializer.Deserialize<AppSettings>(json);
                    if (settings != null)
                    {
                        return settings;
                    }
                }
            }
            catch (Exception)
            {
                // Corrupt/unreadable settings file: fall back to defaults rather
                // than crashing app startup over a settings-load failure.
            }
            return new AppSettings();
        }

        public static void Save(AppSettings settings)
        {
            string? directory = Path.GetDirectoryName(SettingsFilePath);
            if (directory != null)
            {
                Directory.CreateDirectory(directory);
            }
            string json = JsonSerializer.Serialize(settings, new JsonSerializerOptions { WriteIndented = true });
            File.WriteAllText(SettingsFilePath, json);
        }
    }
}
