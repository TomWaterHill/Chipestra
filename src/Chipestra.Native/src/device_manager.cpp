// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "device_manager.h"

#include <utility>

IDevice* DeviceManager::Add(std::unique_ptr<IDevice> device) {
    devices_.push_back(std::move(device));
    return devices_.back().get();
}

std::size_t DeviceManager::Count() const {
    return devices_.size();
}
