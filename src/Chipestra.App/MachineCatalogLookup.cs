// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
using System.Collections.Generic;
using Chipestra.Core;

namespace Chipestra.App
{
    /// <summary>
    /// A snapshot of the process-wide MachineRegistry, keyed by MachineId, read once at startup via
    /// <see cref="NativeRuntimeSession.GetMachineCount"/>/<see cref="NativeRuntimeSession.GetMachineInfo"/>.
    /// The registry never changes at runtime, so one snapshot is enough and no
    /// NativeRuntimeSession instance is needed to read it.
    ///
    /// Used by <see cref="ViewModels.LibraryViewModel"/> to group games by
    /// Category-&gt;Manufacturer-&gt;System (using MachineDefinition.Manufacturer/DisplayName, not the
    /// informational-only GameDefinition.Manufacturer), and by the Player Information panel to show a
    /// Machine's friendly display name instead of its raw MachineId.
    /// </summary>
    public sealed class MachineCatalogLookup
    {
        private readonly Dictionary<string, MachineInfo> _byMachineId = new();

        public MachineCatalogLookup()
        {
            uint count = NativeRuntimeSession.GetMachineCount();
            for (uint i = 0; i < count; i++)
            {
                if (NativeRuntimeSession.GetMachineInfo(i, out MachineInfo info) == ChipestraResult.Ok)
                {
                    _byMachineId[info.MachineId] = info;
                }
            }
        }

        public MachineInfo? Find(string machineId)
        {
            return _byMachineId.TryGetValue(machineId, out MachineInfo info) ? info : null;
        }
    }
}
