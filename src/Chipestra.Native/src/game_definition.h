// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "machine_registry.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// RomResourceDefinition (<rom>) - one ROM/data file a Game's Machine needs, as declared
// in definition XML. Converts 1:1 into a RequiredResource via
// GameDefinition::BuildRequiredResources().
struct RomResourceDefinition {
    std::string Name;  // <rom name="..."> - becomes RequiredResource::LogicalName
    std::string File;  // <rom file="..."> - becomes RequiredResource::AcceptedNames[0]
    std::optional<std::uint64_t> SizeBytes;
    std::optional<std::uint32_t> Crc32;
    std::optional<std::string> Sha1;
};

// TrackDefinition (<track>) - one playable track within a Game. `Command` is an opaque
// value (numeric or string) the Machine interprets to select/trigger this track. The
// schema intentionally has no other <track> attribute: a physical location (a latch
// address, a port number) has no field to be written into.
//
// `Id` is the stable handle callers select a track by (Player.SelectTrack), distinct from
// `Command` so callers never need to know the target board's raw command byte.
struct TrackDefinition {
    std::string Id;
    std::string Command;
    std::string Title;

    // <track type="control"> (optional, default false) - a control command (e.g. STOP), not
    // a real song: Player.SelectTrack still sends Command to the Machine but does not
    // transition to Playing, so Play Time never advances.
    bool IsControlCommand = false;

    // <track loops="false"> (optional, default true) - whether the track loops indefinitely
    // like a typical arcade BGM. Only tracks marked loops="false" are eligible for Player's
    // silence-based auto-stop (PollTrackEndBySilence), so a loop's quiet passages are never
    // misdetected as the end of the track.
    bool Loops = true;

    // <track type="wsg"> (optional, default false) - a request for a WSG sound effect: Command is the
    // game's own test-screen number, and Player sends it as MusicCommandKind::Wsg instead of Raw.
    // Native never interprets it.
    bool IsWsg = false;
};

// GameDefinition (<game>) - one Game's full definition: identity, which Machine/Variant it
// targets, the resources it needs, and its tracks.
struct GameDefinition {
    std::string Id;
    std::string Title;
    std::string Machine;   // MachineId::Value() this Game targets
    std::string Variant;   // VariantId::Value() this Game targets

    // <game manufacturer="..."> - informational only. Library grouping uses
    // MachineDefinition::Manufacturer, since it's a property of the hardware.
    std::string Manufacturer;

    // <game archive="..."> - an optional archive-name hint (the ROM zip filename without its
    // extension, or the ROM Root subfolder name, that this game's resources should be looked
    // for in). Used by ResourceResolver to disambiguate when multiple ROM sets under one ROM
    // Root share filenames. Empty string means no archive scope.
    std::string Archive;

    std::vector<RomResourceDefinition> Resources;
    std::vector<TrackDefinition> Tracks;

    // Builds the RequiredResource list ResourceResolver::Resolve() expects, directly from
    // Resources. Every XML-declared resource is Required=true: the schema has no
    // per-resource optionality attribute.
    std::vector<RequiredResource> BuildRequiredResources() const;
};
