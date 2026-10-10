// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System;
using System.Diagnostics;
using System.Runtime.InteropServices;

namespace Chipestra.App.Settings
{
    // Brings the already-running Chipestra's main window to the front (restoring it if minimized).
    // Windows refuses SetForegroundWindow from a process that does not own the foreground, so when the
    // plain call fails this attaches to the foreground window's input thread first (the usual reliable
    // workaround) and finally falls back to flashing the taskbar button.
    public static class RunningInstanceActivator
    {
        private const int SW_RESTORE = 9;

        [DllImport("user32.dll")] private static extern bool IsIconic(IntPtr hWnd);
        [DllImport("user32.dll")] private static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
        [DllImport("user32.dll")] private static extern bool SetForegroundWindow(IntPtr hWnd);
        [DllImport("user32.dll")] private static extern bool BringWindowToTop(IntPtr hWnd);
        [DllImport("user32.dll")] private static extern IntPtr GetForegroundWindow();
        [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);
        [DllImport("user32.dll")] private static extern bool AttachThreadInput(uint idAttach, uint idAttachTo, bool fAttach);
        [DllImport("kernel32.dll")] private static extern uint GetCurrentThreadId();
        [DllImport("user32.dll")] private static extern bool FlashWindow(IntPtr hWnd, bool bInvert);

        // Returns false if no other Chipestra window was found (e.g. it is still starting up).
        public static bool TryActivateOtherInstance()
        {
            using Process self = Process.GetCurrentProcess();
            foreach (Process other in Process.GetProcessesByName(self.ProcessName))
            {
                using (other)
                {
                    if (other.Id == self.Id)
                    {
                        continue;
                    }
                    IntPtr hwnd = other.MainWindowHandle;
                    if (hwnd == IntPtr.Zero)
                    {
                        continue;
                    }
                    Activate(hwnd);
                    return true;
                }
            }
            return false;
        }

        private static void Activate(IntPtr hwnd)
        {
            if (IsIconic(hwnd))
            {
                ShowWindow(hwnd, SW_RESTORE);
            }
            if (SetForegroundWindow(hwnd))
            {
                return;
            }

            IntPtr foreground = GetForegroundWindow();
            uint foregroundThread = foreground == IntPtr.Zero ? 0 : GetWindowThreadProcessId(foreground, out _);
            uint thisThread = GetCurrentThreadId();
            bool attached = foregroundThread != 0 && foregroundThread != thisThread &&
                            AttachThreadInput(thisThread, foregroundThread, true);
            try
            {
                BringWindowToTop(hwnd);
                if (!SetForegroundWindow(hwnd))
                {
                    FlashWindow(hwnd, true);
                }
            }
            finally
            {
                if (attached)
                {
                    AttachThreadInput(thisThread, foregroundThread, false);
                }
            }
        }
    }
}
