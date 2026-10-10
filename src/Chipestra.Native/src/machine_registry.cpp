// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "machine_registry.h"

#include "resource_resolver.h"

#include <algorithm>
#include <stdexcept>

void MachineRegistry::Register(MachineDefinition definition) {
    if (index_by_id_.find(definition.Id.Value()) != index_by_id_.end()) {
        throw std::logic_error("MachineRegistry::Register: MachineId already registered: " +
                                definition.Id.Value());
    }

    index_by_id_[definition.Id.Value()] = definitions_.size();
    definitions_.push_back(std::move(definition));
}

const MachineDefinition* MachineRegistry::Find(const MachineId& id) const {
    auto it = index_by_id_.find(id.Value());
    if (it == index_by_id_.end()) {
        return nullptr;
    }
    return &definitions_[it->second];
}

const std::vector<MachineDefinition>& MachineRegistry::Enumerate() const {
    return definitions_;
}

std::unique_ptr<IMachine> MachineRegistry::Create(const MachineId& machineId, const VariantId& variantId,
                                                   const MachineResources& resources) const {
    const MachineDefinition* definition = Find(machineId);
    if (definition == nullptr) {
        return nullptr;
    }

    bool variant_matches = std::any_of(
        definition->Variants.begin(), definition->Variants.end(),
        [&variantId](const MachineVariant& variant) { return variant.Id == variantId; });
    if (!variant_matches) {
        return nullptr;
    }

    return definition->Factory(variantId, resources);
}
