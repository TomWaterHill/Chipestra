// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// XXE/DTD safety rationale:
//
// This parser rejects any input containing the literal "<!DOCTYPE" before
// ever handing it to pugixml - an explicit policy that doesn't
// depend on library internals to stay correct if the XML library is ever
// swapped out.
//
// It is also true, independently, that the vendored pugixml 1.15
// (thirdparty/pugixml) could not be used to mount an XXE attack even
// without that check: pugixml.cpp contains no code path that parses a
// `<!ENTITY ...>` declaration into any kind of entity table, and its
// character-reference handling (strconv_escape) only recognizes the five
// predefined XML entities (&lt; &gt; &amp; &apos; &quot;) plus numeric
// character references (&#...;/&#x...;) - grep the vendored source for
// "ENTITY" to confirm it doesn't appear anywhere. A `<!DOCTYPE>` block is
// only ever scanned far enough to find its closing '>' (parse_doctype_group
// in pugixml.cpp), never interpreted. Not passing pugi::parse_doctype means
// even that raw text is discarded rather than exposed as a tree node. This
// project doesn't rely on that fact alone (see the explicit rejection
// above), but it is why "exclude parse_doctype" is a correct defense here
// and not just an easily-bypassed flag.

#include "xml_definition_parser.h"

#include "pugixml/pugixml.hpp"

#include <cstdlib>

namespace {

bool RequiredAttribute(const pugi::xml_node& node, const char* attrName, const char* elementContext,
                        std::string& out, XmlParseResult& result) {
    pugi::xml_attribute attr = node.attribute(attrName);
    if (!attr || attr.value()[0] == '\0') {
        result.Status = XmlParseStatus::MissingAttribute;
        result.Detail = std::string(elementContext) + " is missing required attribute '" + attrName + "'";
        return false;
    }
    out = attr.value();
    return true;
}

}  // namespace

XmlParseResult ParseGameDefinition(std::string_view xml, std::size_t maxXmlSizeBytes) {
    XmlParseResult result;

    if (xml.size() > maxXmlSizeBytes) {
        result.Status = XmlParseStatus::TooLarge;
        result.Detail = "input size exceeds the " + std::to_string(maxXmlSizeBytes) + " byte limit";
        return result;
    }

    if (xml.find("<!DOCTYPE") != std::string_view::npos) {
        result.Status = XmlParseStatus::DoctypeRejected;
        result.Detail = "DOCTYPE declarations are not accepted in definition XML";
        return result;
    }

    pugi::xml_document doc;
    // parse_default excludes parse_doctype/parse_pi/parse_comments/
    // parse_declaration - only element/attribute/text content is parsed.
    pugi::xml_parse_result parseResult =
        doc.load_buffer(xml.data(), xml.size(), pugi::parse_default, pugi::encoding_utf8);
    if (!parseResult) {
        result.Status = XmlParseStatus::MalformedXml;
        result.Detail = std::string("XML parse error: ") + parseResult.description();
        return result;
    }

    pugi::xml_node gameNode = doc.child("chipestra").child("game");
    if (!gameNode) {
        result.Status = XmlParseStatus::NoGameElement;
        result.Detail = "no <chipestra><game> element found";
        return result;
    }

    GameDefinition def;

    if (!RequiredAttribute(gameNode, "id", "<game>", def.Id, result)) return result;
    if (!RequiredAttribute(gameNode, "title", "<game>", def.Title, result)) return result;
    if (!RequiredAttribute(gameNode, "machine", "<game>", def.Machine, result)) return result;
    if (!RequiredAttribute(gameNode, "variant", "<game>", def.Variant, result)) return result;
    // manufacturer is optional and informational only -
    // MachineDefinition::Manufacturer is authoritative for Library grouping.
    def.Manufacturer = gameNode.attribute("manufacturer").as_string("");
    // archive is optional - see GameDefinition::Archive's own comment.
    def.Archive = gameNode.attribute("archive").as_string("");

    for (pugi::xml_node romNode : gameNode.child("resources").children("rom")) {
        RomResourceDefinition rom;
        if (!RequiredAttribute(romNode, "name", "<rom>", rom.Name, result)) return result;
        if (!RequiredAttribute(romNode, "file", "<rom>", rom.File, result)) return result;

        if (pugi::xml_attribute sizeAttr = romNode.attribute("size")) {
            rom.SizeBytes = static_cast<std::uint64_t>(sizeAttr.as_ullong());
        }
        if (pugi::xml_attribute crcAttr = romNode.attribute("crc32")) {
            // CRC32 values are written in hex, as in common ROM lists.
            rom.Crc32 = static_cast<std::uint32_t>(std::strtoul(crcAttr.value(), nullptr, 16));
        }
        if (pugi::xml_attribute shaAttr = romNode.attribute("sha1")) {
            rom.Sha1 = shaAttr.value();
        }

        def.Resources.push_back(std::move(rom));
    }

    for (pugi::xml_node trackNode : gameNode.child("tracks").children("track")) {
        TrackDefinition track;
        if (!RequiredAttribute(trackNode, "id", "<track>", track.Id, result)) return result;
        if (!RequiredAttribute(trackNode, "command", "<track>", track.Command, result)) return result;
        if (!RequiredAttribute(trackNode, "title", "<track>", track.Title, result)) return result;
        // type/loops are optional (see TrackDefinition for defaults). type is "control", "wsg" or
        // absent; any other value (wrong case, a typo, "psg") is an error, not silently a normal track.
        if (pugi::xml_attribute typeAttr = trackNode.attribute("type")) {
            const std::string_view type(typeAttr.value());
            if (type == "control") {
                track.IsControlCommand = true;
            } else if (type == "wsg") {
                track.IsWsg = true;
            } else {
                result.Status = XmlParseStatus::InvalidAttributeValue;
                result.Detail = "<track id=\"" + track.Id + "\">: unknown type \"" + std::string(type) +
                                "\" (expected \"control\" or \"wsg\", or no type attribute)";
                return result;
            }
        }
        track.Loops = std::string_view(trackNode.attribute("loops").as_string("true")) != "false";
        def.Tracks.push_back(std::move(track));
    }

    result.Status = XmlParseStatus::Ok;
    result.Definition = std::move(def);
    return result;
}
