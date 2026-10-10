// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include <cstddef>
#include <memory>
#include <vector>

// IDevice - marker interface for anything a DeviceManager owns.
class IDevice {
public:
    virtual ~IDevice() = default;
};

// DeviceManager - exclusively owns every Device belonging to one
// Machine and hands out non-owning references to them. No other code shares
// ownership of a Device: shared_ptr is never used here. This keeps
// destruction order explicit (all Devices die with the DeviceManager, which
// dies with its Machine) and prevents a Device from accidentally being
// shared between two Native instances.
class DeviceManager {
public:
    // Takes ownership of `device` and returns a non-owning pointer to it,
    // valid for as long as this DeviceManager is alive.
    IDevice* Add(std::unique_ptr<IDevice> device);

    std::size_t Count() const;

private:
    std::vector<std::unique_ptr<IDevice>> devices_;
};
