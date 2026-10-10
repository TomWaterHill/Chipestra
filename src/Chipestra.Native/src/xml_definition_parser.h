// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "game_definition.h"

#include <cstddef>
#include <string>
#include <string_view>

// XmlParseStatus - why ParseGameDefinition did not produce a GameDefinition.
// DoctypeRejected is its own status (not folded into MalformedXml) so XXE-defense
// tests can assert precisely on it.
enum class XmlParseStatus {
    Ok,
    TooLarge,          // input exceeds the size cap
    DoctypeRejected,    // input contains a <!DOCTYPE - rejected outright (XXE defense)
    MalformedXml,       // not well-formed XML
    NoGameElement,      // no <chipestra><game> found
    MissingAttribute,   // a required attribute is absent from <game>/<rom>/<track>
    InvalidAttributeValue,  // an attribute has a value the format does not allow (e.g. an unknown <track type>)
};

struct XmlParseResult {
    XmlParseStatus Status = XmlParseStatus::MalformedXml;
    GameDefinition Definition;  // valid only when Status == Ok
    std::string Detail;         // human-readable diagnostic, not part of any stable ABI
};

// Generous but bounded: a hand-authored Game/Track definition XML has no
// legitimate reason to approach this size. Exists mainly so a huge input
// is rejected before pugixml ever allocates a DOM for it.
constexpr std::size_t kDefaultMaxXmlSizeBytes = 4ull * 1024 * 1024;

// ParseGameDefinition - parses one `<chipestra><game>...</game></chipestra>` document
// into a GameDefinition. Never throws: malformed/oversized/incomplete input is reported
// via XmlParseResult::Status.
//
// DTD/external entities are never processed, enforced two ways: (1) any input containing
// "<!DOCTYPE" is rejected outright before parsing, and (2) even without that check, the
// vendored pugixml has no DTD/entity-expansion implementation at all to exploit (see the
// top-of-file comment in xml_definition_parser.cpp).
XmlParseResult ParseGameDefinition(std::string_view xml, std::size_t maxXmlSizeBytes = kDefaultMaxXmlSizeBytes);
