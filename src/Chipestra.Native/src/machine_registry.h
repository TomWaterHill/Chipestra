// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "chipestra_abi.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// Forward-declared rather than #include "resource_resolver.h": that header
// already includes this one (Resolve() takes a RequiredResource list), so
// including it back here would be circular. Factory only needs a reference
// to MachineResources, which a forward declaration is enough for; anything
// that actually calls Factory or constructs a MachineResources includes
// resource_resolver.h itself.
class MachineResources;

// MachineId identifies a Machine *definition* in the MachineRegistry
// - e.g. "XBOARD_AFTERBURNER2". Stable, uppercase ASCII by convention.
//
// A Machine is a piece of hardware (a board + its fixed wiring); a Game is a
// title that runs on it. The same Machine can host many Games.
class MachineId {
public:
    MachineId() = default;
    explicit MachineId(std::string id) : id_(std::move(id)) {}

    const std::string& Value() const { return id_; }
    bool operator==(const MachineId& other) const { return id_ == other.id_; }
    bool operator!=(const MachineId& other) const { return !(*this == other); }

private:
    std::string id_;
};

// VariantId identifies one selectable hardware variant of a Machine
// - e.g. a clock-speed or ROM-revision difference on otherwise the same
// board. Stable, uppercase ASCII by convention, same as MachineId.
//
// Selected by MachineRegistry::Create via exact match only - never fuzzy or
// partial matching.
class VariantId {
public:
    VariantId() = default;
    explicit VariantId(std::string id) : id_(std::move(id)) {}

    const std::string& Value() const { return id_; }
    bool operator==(const VariantId& other) const { return id_ == other.id_; }
    bool operator!=(const VariantId& other) const { return !(*this == other); }

private:
    std::string id_;
};

// Category a Machine belongs to. Lives on MachineDefinition, not on any Game:
// the Library UI's tree (Category -> Manufacturer -> System -> Game -> Track)
// groups by hardware category.
enum class MachineCategory {
    Arcade,
    Console,
};

// MachineVariant - one selectable hardware variant of a Machine.
struct MachineVariant {
    VariantId Id;
    std::string DisplayName;
    // Clock/config options are not modelled (only an Id and a DisplayName).
};

// RequiredResource - describes one ROM/data file a Machine needs, without
// resolving it: locating/opening/verifying the file is the Resource Resolver's job.
struct RequiredResource {
    std::string LogicalName;                  // Machine-internal name for this resource, e.g. "maincpu"
    std::vector<std::string> AcceptedNames;    // acceptable relative filenames (multiple = accepted alternates)
    std::optional<std::uint64_t> SizeBytes;
    std::optional<std::uint32_t> Crc32;
    std::optional<std::string> Sha1;
    bool Required = true;  // false = Machine can run without this resource present

    // Optional archive scope (GameDefinition::Archive) - when set, ResourceResolver
    // only considers candidates from the ZIP of this name (stem, case-insensitive)
    // or the ROM Root subfolder of this name; nullopt means unscoped.
    std::optional<std::string> Archive;
};

// MachineDefinition - static description of one Machine: identity, selectable
// variants, the resources it needs, and how to construct a live IMachine instance.
// Category lives here, not on Game.
struct MachineDefinition {
    MachineId Id;
    std::string DisplayName;
    MachineCategory Category = MachineCategory::Arcade;
    // Lives here rather than on Game/GameDefinition: a manufacturer is a property of
    // the hardware. The game XML's own `manufacturer` attribute is informational
    // only - Library grouping uses this field.
    std::string Manufacturer;
    std::vector<MachineVariant> Variants;
    std::vector<RequiredResource> RequiredResources;

    // Constructs a live IMachine instance for a specific Variant, given the resources
    // ResourceResolver already resolved for this Machine's RequiredResources.
    // MachineRegistry::Create only calls this after confirming variantId exactly matches
    // one of Variants' Id. `resources` is a non-owning view valid only for the duration
    // of this call - a Factory that needs the bytes beyond construction must copy them.
    // A Machine with no RequiredResources simply ignores the parameter.
    std::function<std::unique_ptr<IMachine>(const VariantId& variantId, const MachineResources& resources)> Factory;
};

// MachineRegistry - the single source of truth for what Machines this Native Runtime
// knows how to construct. chipestra_load_machine looks a machine_id/variant_id pair up
// here instead of hardcoding a specific Machine type.
class MachineRegistry {
public:
    // Registers `definition`. Throws std::logic_error if a definition with the same Id is
    // already registered - registration never implicitly replaces an existing entry.
    void Register(MachineDefinition definition);

    // Non-owning lookup by Id. Returns nullptr if `id` is not registered.
    const MachineDefinition* Find(const MachineId& id) const;

    // Every registered definition, in registration order.
    const std::vector<MachineDefinition>& Enumerate() const;

    // Constructs a live IMachine instance for (machineId, variantId). variantId must
    // exactly match one of that definition's Variants' Id - no fuzzy matching.
    //
    // Returns nullptr if machineId is not registered, or if variantId does not match any
    // of its Variants (both cases look the same to the caller;
    // chipestra_load_machine maps both to CHIPESTRA_E_NOT_FOUND).
    //
    // `resources` is passed through to the Factory unchanged; Create() does not validate it
    // against RequiredResources (ResourceResolver::Resolve() already did). Callers with
    // nothing to resolve pass an empty MachineResources{} explicitly (no default argument,
    // since MachineResources is only forward-declared here).
    std::unique_ptr<IMachine> Create(const MachineId& machineId, const VariantId& variantId,
                                      const MachineResources& resources) const;

private:
    std::vector<MachineDefinition> definitions_;               // registration order, for Enumerate()
    std::unordered_map<std::string, std::size_t> index_by_id_;  // MachineId.Value() -> index into definitions_
};
