// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "game_definition.h"

std::vector<RequiredResource> GameDefinition::BuildRequiredResources() const {
    std::vector<RequiredResource> resources;
    resources.reserve(Resources.size());

    // A resource's own archive wins; otherwise it is looked for in the game's archive.
    const std::optional<std::string> archive = Archive.empty() ? std::nullopt : std::make_optional(Archive);

    for (const auto& rom : Resources) {
        RequiredResource req;
        req.LogicalName = rom.Name;
        req.AcceptedNames = {rom.File};
        req.SizeBytes = rom.SizeBytes;
        req.Crc32 = rom.Crc32;
        req.Sha1 = rom.Sha1;
        req.Required = true;
        req.Archive = rom.Archive.empty() ? archive : std::make_optional(rom.Archive);
        resources.push_back(std::move(req));
    }

    return resources;
}
