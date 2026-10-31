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

    // <rom archive="..."> (optional) - the archive this resource is looked for in (a ZIP name without its
    // extension, or a folder name). Empty means the game's own archive (GameDefinition::Archive). It lets a
    // game take a resource from another archive, e.g. a sound chip ROM.
    std::string Archive;
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

    // <track type="sfx"> (optional, default false) - a request for a sound effect of the game's own sound
    // driver: Command is the effect number, and Player sends it as MusicCommandKind::Effect instead of
    // Raw. Native never interprets it.
    bool IsEffect = false;

    // <track set="..." request="..."> (optional) - which combination of the game's data files to load and which
    // request address the command value is written to, for a game whose <driver> element declares them (a
    // Famicom Disk System game). Resolved at parse time to one byte, (request number << 4) | set number (| 0x80 for a control track): the set
    // number counts the <driver><set> elements from 1 (0 = the base files alone), the request number counts the
    // addresses of <driver requests="..."> from 1 (0 = none; at most 7). Bit 7 is set for a control track
    // (<track type="control">). Native carries it as MusicCommand::Auxiliary and never interprets it.
    std::uint8_t Auxiliary = 0;
};

// GameDefinition (<game>) - one Game's full definition: identity, which Machine/Variant it
// targets, the resources it needs, and its tracks.
struct GameDefinition {
    std::string Id;
    std::string Title;
    std::string Machine;   // MachineId::Value() this Game targets
    std::string Variant;   // VariantId::Value() this Game targets

    // <game manufacturer="..."> - informational for an arcade game (Library grouping uses
    // MachineDefinition::Manufacturer, a property of the hardware); for a console game the Library
    // shows it as the publisher level. Native only carries it.
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
