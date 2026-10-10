// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using System.IO;
using System.Windows;
using Chipestra.App.Settings;
using Chipestra.App.ViewModels;
using Chipestra.Core;

namespace Chipestra.App
{
    public partial class App : Application
    {
        // Held for the whole lifetime of the app (a local would let the GC release the mutex).
        private SingleInstanceGuard? _singleInstance;

        protected override void OnExit(ExitEventArgs e)
        {
            _singleInstance?.Dispose();
            _singleInstance = null;
            base.OnExit(e);
        }

        protected override void OnStartup(StartupEventArgs e)
        {
            // Before anything else (settings, audio, Native): a second launch only brings the running
            // window to the front and ends without showing or touching anything.
            _singleInstance = SingleInstanceGuard.TryAcquire();
            if (_singleInstance == null)
            {
                RunningInstanceActivator.TryActivateOtherInstance();
                Shutdown(0);
                return;
            }

            base.OnStartup(e);

            // Show unhandled UI exceptions as a message box instead of crashing silently.
            DispatcherUnhandledException += (_, args) =>
            {
                MessageBox.Show(
                    $"Unhandled error:\n\n{args.Exception}",
                    "Chipestra - Error",
                    MessageBoxButton.OK,
                    MessageBoxImage.Error);
                args.Handled = true;
            };

            try
            {
                // Before anything else touches chipestra_native.dll: an application/DLL pair from
                // different builds fails here with an explicit message (shown by the catch
                // below) instead of silently misbehaving on a changed C ABI signature.
                ChipestraRuntime.EnsureCompatibleNativeVersion();

                string definitionsDirectory = Path.Combine(AppContext.BaseDirectory, "xml");
                GameDefinitionLoadResult loadResult = GameDefinitionCatalog.LoadDirectory(definitionsDirectory);
                var catalog = loadResult.Catalog;
                var machines = new MachineCatalogLookup();
                var library = new LibraryViewModel(catalog, machines);
                AppSettings settings = AppSettingsStore.Load();
                var mainViewModel = new MainViewModel(library, machines, settings);

                var window = new MainWindow(mainViewModel);
                MainWindow = window;
                window.Show();

                // Files that failed to load were skipped (the other games are usable);
                // tell the user once, after the main window is up.
                if (loadResult.Failures.Count > 0)
                {
                    MessageBox.Show(
                        window,
                        XmlLoadErrorMessage.Build(loadResult.Failures),
                        XmlLoadErrorMessage.Title,
                        MessageBoxButton.OK,
                        MessageBoxImage.Warning);
                }
            }
            catch (Exception ex)
            {
                // Expected startup failures (bad xml, Native Runtime not loadable) show a message box.
                MessageBox.Show(
                    $"Chipestra failed to start:\n\n{ex.Message}",
                    "Chipestra - Startup Error",
                    MessageBoxButton.OK,
                    MessageBoxImage.Error);
                Shutdown(1);
            }
        }
    }
}
