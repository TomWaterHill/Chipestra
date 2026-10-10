// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// XML definition parser + game/track model native tests. All XML
// fixtures below are self-authored synthetic data (no real game/ROM XML).

#include "game_definition_tests.h"

#include "game_definition.h"
#include "resource_resolver.h"
#include "test_support.h"
#include "xml_definition_parser.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view kValidXml =
    "<chipestra>"
    "<game id=\"AB2\" title=\"After Burner II (synthetic)\" machine=\"TEST_MACHINE\" variant=\"STANDARD\" "
    "manufacturer=\"Test Co.\">"
    "<resources>"
    "<rom name=\"maincpu\" file=\"maincpu.bin\" size=\"131072\" crc32=\"1A2B3C4D\" "
    "sha1=\"0000000000000000000000000000000000dead\"/>"
    "</resources>"
    "<tracks>"
    "<track id=\"T1\" command=\"1\" title=\"Opening\"/>"
    "<track id=\"T2\" command=\"2\" title=\"After Burner\"/>"
    "</tracks>"
    "</game>"
    "</chipestra>";

std::filesystem::path MakeTempRomRoot(const std::string& testName) {
    auto root = std::filesystem::temp_directory_path() / "chipestra_gamedefinition_tests" / testName;
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root);
    return root;
}

void WriteFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& data) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    if (!data.empty()) {
        file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
}

}  // namespace

void ParseGameDefinitionValidXml() {
    XmlParseResult result = ParseGameDefinition(kValidXml);
    Check(result.Status == XmlParseStatus::Ok, "valid xml: Status == Ok");

    const GameDefinition& def = result.Definition;
    Check(def.Id == "AB2", "valid xml: game.id read correctly");
    Check(def.Title == "After Burner II (synthetic)", "valid xml: game.title read correctly");
    Check(def.Machine == "TEST_MACHINE", "valid xml: game.machine read correctly");
    Check(def.Variant == "STANDARD", "valid xml: game.variant read correctly");
    Check(def.Manufacturer == "Test Co.", "valid xml: game.manufacturer read correctly");

    Check(def.Resources.size() == 1, "valid xml: one <rom> parsed");
    if (def.Resources.size() == 1) {
        const RomResourceDefinition& rom = def.Resources[0];
        Check(rom.Name == "maincpu", "valid xml: rom.name read correctly");
        Check(rom.File == "maincpu.bin", "valid xml: rom.file read correctly");
        Check(rom.SizeBytes.has_value() && *rom.SizeBytes == 131072, "valid xml: rom.size read correctly");
        Check(rom.Sha1.has_value() && *rom.Sha1 == "0000000000000000000000000000000000dead",
              "valid xml: rom.sha1 read correctly");
    }

    Check(def.Tracks.size() == 2, "valid xml: two <track> elements parsed");
    if (def.Tracks.size() == 2) {
        Check(def.Tracks[0].Id == "T1" && def.Tracks[0].Command == "1" && def.Tracks[0].Title == "Opening",
              "valid xml: first track fields read correctly");
        Check(def.Tracks[1].Id == "T2" && def.Tracks[1].Command == "2" && def.Tracks[1].Title == "After Burner",
              "valid xml: second track fields read correctly");
        Check(!def.Tracks[0].IsControlCommand && !def.Tracks[1].IsControlCommand,
              "valid xml: IsControlCommand defaults to false when <track type> is absent");
        Check(def.Tracks[0].Loops && def.Tracks[1].Loops,
              "valid xml: Loops defaults to true when <track loops> is absent");
    }
}

void ParseGameDefinitionMissingGameAttribute() {
    constexpr std::string_view xml =
        "<chipestra><game title=\"No Id\" machine=\"TEST_MACHINE\" variant=\"STANDARD\">"
        "<resources></resources><tracks></tracks></game></chipestra>";
    XmlParseResult result = ParseGameDefinition(xml);
    Check(result.Status == XmlParseStatus::MissingAttribute,
          "missing game attribute: Status == MissingAttribute when <game id> is absent");
}

void ParseGameDefinitionMissingRomAttribute() {
    constexpr std::string_view xml =
        "<chipestra><game id=\"X\" title=\"X\" machine=\"TEST_MACHINE\" variant=\"STANDARD\">"
        "<resources><rom name=\"maincpu\"/></resources>"
        "<tracks><track command=\"1\" title=\"T\"/></tracks></game></chipestra>";
    XmlParseResult result = ParseGameDefinition(xml);
    Check(result.Status == XmlParseStatus::MissingAttribute,
          "missing rom attribute: Status == MissingAttribute when <rom file> is absent");
}

void ParseGameDefinitionMissingTrackAttribute() {
    constexpr std::string_view xml =
        "<chipestra><game id=\"X\" title=\"X\" machine=\"TEST_MACHINE\" variant=\"STANDARD\">"
        "<resources><rom name=\"maincpu\" file=\"maincpu.bin\"/></resources>"
        "<tracks><track id=\"T1\" title=\"No Command\"/></tracks></game></chipestra>";
    XmlParseResult result = ParseGameDefinition(xml);
    Check(result.Status == XmlParseStatus::MissingAttribute,
          "missing track attribute: Status == MissingAttribute when <track command> is absent");
}

void ParseGameDefinitionMissingTrackIdAttribute() {
    // <track id="..."> is required: it is Player.SelectTrack's stable track
    // handle (see game_definition.h's TrackDefinition::Id remarks).
    constexpr std::string_view xml =
        "<chipestra><game id=\"X\" title=\"X\" machine=\"TEST_MACHINE\" variant=\"STANDARD\">"
        "<resources><rom name=\"maincpu\" file=\"maincpu.bin\"/></resources>"
        "<tracks><track command=\"1\" title=\"No Id\"/></tracks></game></chipestra>";
    XmlParseResult result = ParseGameDefinition(xml);
    Check(result.Status == XmlParseStatus::MissingAttribute,
          "missing track id: Status == MissingAttribute when <track id> is absent");
}

void ParseGameDefinitionManufacturerIsOptional() {
    constexpr std::string_view xml =
        "<chipestra><game id=\"X\" title=\"X\" machine=\"TEST_MACHINE\" variant=\"STANDARD\">"
        "<resources></resources><tracks></tracks></game></chipestra>";
    XmlParseResult result = ParseGameDefinition(xml);
    Check(result.Status == XmlParseStatus::Ok, "manufacturer optional: Status == Ok without a manufacturer attribute");
    Check(result.Definition.Manufacturer.empty(), "manufacturer optional: Manufacturer defaults to empty, not an error");
}

void ParseGameDefinitionOptionalRomAttributesAreOptional() {
    constexpr std::string_view xml =
        "<chipestra><game id=\"X\" title=\"X\" machine=\"TEST_MACHINE\" variant=\"STANDARD\">"
        "<resources><rom name=\"maincpu\" file=\"maincpu.bin\"/></resources>"
        "<tracks></tracks></game></chipestra>";
    XmlParseResult result = ParseGameDefinition(xml);
    Check(result.Status == XmlParseStatus::Ok, "optional rom attrs: Status == Ok without size/crc32/sha1");
    if (result.Status == XmlParseStatus::Ok && !result.Definition.Resources.empty()) {
        const RomResourceDefinition& rom = result.Definition.Resources[0];
        Check(!rom.SizeBytes.has_value(), "optional rom attrs: SizeBytes is nullopt when absent");
        Check(!rom.Crc32.has_value(), "optional rom attrs: Crc32 is nullopt when absent");
        Check(!rom.Sha1.has_value(), "optional rom attrs: Sha1 is nullopt when absent");
    }
}

void ParseGameDefinitionCrc32IsParsedAsHex() {
    XmlParseResult result = ParseGameDefinition(kValidXml);
    Check(result.Status == XmlParseStatus::Ok, "crc32 hex: setup parse succeeds");
    if (result.Status == XmlParseStatus::Ok && !result.Definition.Resources.empty()) {
        const auto& crc = result.Definition.Resources[0].Crc32;
        Check(crc.has_value() && *crc == 0x1A2B3C4Du,
              "crc32 hex: XML attribute \"1A2B3C4D\" is parsed as hex, not decimal");
    }
}

void ParseGameDefinitionNoGameElement() {
    constexpr std::string_view xml = "<chipestra></chipestra>";
    XmlParseResult result = ParseGameDefinition(xml);
    Check(result.Status == XmlParseStatus::NoGameElement, "no game element: Status == NoGameElement");
}

void ParseGameDefinitionRejectsOldRootElement() {
    // Pre-rename root element: no compatibility shim, so a <vgmp> document must not parse.
    constexpr std::string_view xml =
        "<vgmp><game id=\"G\" title=\"G\" machine=\"TEST_MACHINE\" variant=\"STANDARD\">"
        "<resources></resources><tracks></tracks></game></vgmp>";
    XmlParseResult result = ParseGameDefinition(xml);
    Check(result.Status == XmlParseStatus::NoGameElement,
          "old root element: <vgmp> document is rejected with NoGameElement");
}

void ParseGameDefinitionMalformedXml() {
    constexpr std::string_view xml = "<chipestra><game id=\"X\" title=\"X\"";  // truncated, never closed
    XmlParseResult result = ParseGameDefinition(xml);
    Check(result.Status == XmlParseStatus::MalformedXml, "malformed xml: Status == MalformedXml for unclosed input");
}

void ParseGameDefinitionRejectsDoctype() {
    constexpr std::string_view xxeXml =
        "<?xml version=\"1.0\"?>"
        "<!DOCTYPE chipestra [<!ENTITY xxe \"INJECTED\">]>"
        "<chipestra><game id=\"&xxe;\" title=\"T\" machine=\"TEST_MACHINE\" variant=\"STANDARD\">"
        "<resources></resources><tracks></tracks></game></chipestra>";
    XmlParseResult result = ParseGameDefinition(xxeXml);
    Check(result.Status == XmlParseStatus::DoctypeRejected,
          "xxe: a DOCTYPE-bearing document is rejected outright (Status == DoctypeRejected)");
    Check(result.Status != XmlParseStatus::Ok || result.Definition.Id != "INJECTED",
          "xxe: even if parsing somehow proceeded, the entity is never expanded into game.id");
}

void ParseGameDefinitionRejectsOversizedInput() {
    XmlParseResult result = ParseGameDefinition(kValidXml, /*maxXmlSizeBytes=*/16);
    Check(result.Status == XmlParseStatus::TooLarge,
          "oversized: Status == TooLarge when input exceeds the configured cap");
}

void ParseGameDefinitionDeterministicAcrossRepeatedParses() {
    XmlParseResult first = ParseGameDefinition(kValidXml);
    XmlParseResult second = ParseGameDefinition(kValidXml);

    Check(first.Status == XmlParseStatus::Ok && second.Status == XmlParseStatus::Ok,
          "determinism: both parses succeed");
    const GameDefinition& a = first.Definition;
    const GameDefinition& b = second.Definition;
    Check(a.Id == b.Id && a.Title == b.Title && a.Machine == b.Machine && a.Variant == b.Variant &&
              a.Manufacturer == b.Manufacturer && a.Resources.size() == b.Resources.size() &&
              a.Tracks.size() == b.Tracks.size(),
          "determinism: repeated ParseGameDefinition() calls on the same XML agree");
}

void GameDefinitionBuildsRequiredResourcesForResolver() {
    XmlParseResult result = ParseGameDefinition(kValidXml);
    Check(result.Status == XmlParseStatus::Ok, "resource request: setup parse succeeds");
    if (result.Status != XmlParseStatus::Ok) {
        return;
    }

    std::vector<RequiredResource> resources = result.Definition.BuildRequiredResources();
    Check(resources.size() == 1, "resource request: one RequiredResource built per <rom>");
    if (!resources.empty()) {
        const RequiredResource& req = resources[0];
        Check(req.LogicalName == "maincpu", "resource request: LogicalName == rom.name");
        Check(req.AcceptedNames.size() == 1 && req.AcceptedNames[0] == "maincpu.bin",
              "resource request: AcceptedNames == {rom.file}");
        Check(req.SizeBytes.has_value() && *req.SizeBytes == 131072, "resource request: SizeBytes carried over");
        Check(req.Crc32.has_value() && *req.Crc32 == 0x1A2B3C4Du, "resource request: Crc32 carried over");
        Check(req.Required, "resource request: XML-declared resources default to Required");
    }

    // Definition-driven resource request: the built RequiredResource list must
    // be directly usable by ResourceResolver with no adaptation - prove it
    // end-to-end against a synthetic ROM Root whose file matches the XML's
    // <rom> (not its hash, since a hash match would need real matching bytes;
    // size is enough to prove the wiring, not the hash-verification logic
    // which the resolver tests cover on their own).
    auto root = MakeTempRomRoot("definition_driven_resolve");
    WriteFile(root / "maincpu.bin", std::vector<std::uint8_t>(131072, 0xAB));

    ResourceResolver resolver(root);
    RequiredResource req = resources[0];
    req.Crc32.reset();  // drop the (unmatched, synthetic) hash - only proving the size/name wiring here
    req.Sha1.reset();
    auto resolution = resolver.Resolve({req});
    Check(resolution.Ok, "resource request: RequiredResource built from XML resolves via ResourceResolver unmodified");
}

// ---------------------------------------------------------------------
// Archive attribute
// ---------------------------------------------------------------------

void ParseGameDefinitionArchiveIsOptional() {
    XmlParseResult result = ParseGameDefinition(kValidXml);  // kValidXml has no archive="..."
    Check(result.Status == XmlParseStatus::Ok, "archive optional: setup parse succeeds");
    Check(result.Definition.Archive.empty(), "archive optional: Archive defaults to empty, not an error, when absent");
}

void ParseGameDefinitionArchiveIsParsedWhenPresent() {
    constexpr std::string_view xml =
        "<chipestra><game id=\"X\" title=\"X\" machine=\"TEST_MACHINE\" variant=\"STANDARD\" archive=\"aburner2\">"
        "<resources></resources><tracks></tracks></game></chipestra>";
    XmlParseResult result = ParseGameDefinition(xml);
    Check(result.Status == XmlParseStatus::Ok, "archive present: parses cleanly");
    Check(result.Definition.Archive == "aburner2", "archive present: <game archive=\"...\"> is read correctly");
}

void GameDefinitionBuildsRequiredResourcesWithArchiveScope() {
    constexpr std::string_view xml =
        "<chipestra><game id=\"X\" title=\"X\" machine=\"TEST_MACHINE\" variant=\"STANDARD\" archive=\"aburner2\">"
        "<resources>"
        "<rom name=\"soundcpu\" file=\"soundcpu.bin\"/>"
        "<rom name=\"pcm1\" file=\"pcm1.bin\"/>"
        "</resources><tracks></tracks></game></chipestra>";
    XmlParseResult result = ParseGameDefinition(xml);
    Check(result.Status == XmlParseStatus::Ok, "archive scope wiring: setup parse succeeds");
    if (result.Status != XmlParseStatus::Ok) {
        return;
    }

    std::vector<RequiredResource> resources = result.Definition.BuildRequiredResources();
    Check(resources.size() == 2, "archive scope wiring: one RequiredResource per <rom>, as before");
    bool allScoped = true;
    for (const RequiredResource& req : resources) {
        allScoped = allScoped && req.Archive.has_value() && *req.Archive == "aburner2";
    }
    Check(allScoped, "archive scope wiring: every built RequiredResource carries the Game's Archive");
}

// ---------------------------------------------------------------------
// Control-command / loops track attributes
// ---------------------------------------------------------------------

void ParseGameDefinitionTrackTypeIsOptional() {
    XmlParseResult result = ParseGameDefinition(kValidXml);  // kValidXml's tracks have no type="..."
    Check(result.Status == XmlParseStatus::Ok, "track type optional: setup parse succeeds");
    Check(!result.Definition.Tracks.empty() && !result.Definition.Tracks[0].IsControlCommand,
          "track type optional: IsControlCommand defaults to false, not an error, when <track type> is absent");
}

void ParseGameDefinitionTrackTypeControlWhenPresent() {
    constexpr std::string_view xml =
        "<chipestra><game id=\"X\" title=\"X\" machine=\"TEST_MACHINE\" variant=\"STANDARD\">"
        "<resources></resources>"
        "<tracks>"
        "<track id=\"T1\" command=\"0x00\" title=\"Stop\" type=\"control\"/>"
        "<track id=\"T2\" command=\"1\" title=\"Song\"/>"
        "</tracks></game></chipestra>";
    XmlParseResult result = ParseGameDefinition(xml);
    Check(result.Status == XmlParseStatus::Ok, "track type=control: parses cleanly");
    Check(result.Definition.Tracks.size() == 2, "track type=control: both tracks parsed");
    if (result.Definition.Tracks.size() == 2) {
        Check(result.Definition.Tracks[0].IsControlCommand,
              "track type=control: <track type=\"control\"> is read as IsControlCommand == true");
        Check(!result.Definition.Tracks[1].IsControlCommand,
              "track type=control: a sibling <track> without type=\"control\" stays IsControlCommand == false");
    }
}

void ParseGameDefinitionTrackTypeWsgWhenPresent() {
    constexpr std::string_view xml =
        "<chipestra><game id=\"X\" title=\"X\" machine=\"TEST_MACHINE\" variant=\"STANDARD\">"
        "<resources></resources><tracks>"
        "<track id=\"W\" command=\"0x05\" title=\"w\" type=\"wsg\"/>"
        "<track id=\"S\" command=\"0x00\" title=\"s\" type=\"control\"/>"
        "<track id=\"N\" command=\"0x11\" title=\"n\"/>"
        "</tracks></game></chipestra>";
    XmlParseResult result = ParseGameDefinition(xml);
    Check(result.Status == XmlParseStatus::Ok, "track type=wsg: Status == Ok");
    if (result.Status != XmlParseStatus::Ok) return;
    const auto& t = result.Definition.Tracks;
    Check(t[0].IsWsg && !t[0].IsControlCommand, "track type=wsg: read as IsWsg only");
    Check(!t[1].IsWsg && t[1].IsControlCommand, "track type=control: still IsControlCommand only");
    Check(!t[2].IsWsg && !t[2].IsControlCommand, "no type: neither flag (existing behavior unchanged)");
}

void ParseGameDefinitionTrackTypeUnknownValueIsRejected() {
    for (const char* bad : {"Control", "WSG", "psg", "wsg ", "", "musc"}) {
        const std::string xml =
            std::string("<chipestra><game id=\"X\" title=\"X\" machine=\"TEST_MACHINE\" variant=\"STANDARD\">"
                        "<resources></resources><tracks><track id=\"T\" command=\"1\" title=\"t\" type=\"") +
            bad + "\"/></tracks></game></chipestra>";
        XmlParseResult result = ParseGameDefinition(xml);
        const std::string label = std::string("track type=\"") + bad + "\": rejected as InvalidAttributeValue";
        Check(result.Status == XmlParseStatus::InvalidAttributeValue, label.c_str());
        Check(result.Detail.find("unknown type") != std::string::npos, "...with a detail naming the problem");
    }
}

void ParseGameDefinitionTrackLoopsIsOptional() {
    XmlParseResult result = ParseGameDefinition(kValidXml);  // kValidXml's tracks have no loops="..."
    Check(result.Status == XmlParseStatus::Ok, "track loops optional: setup parse succeeds");
    Check(!result.Definition.Tracks.empty() && result.Definition.Tracks[0].Loops,
          "track loops optional: Loops defaults to true, not an error, when <track loops> is absent");
}

void ParseGameDefinitionTrackLoopsFalseWhenPresent() {
    constexpr std::string_view xml =
        "<chipestra><game id=\"X\" title=\"X\" machine=\"TEST_MACHINE\" variant=\"STANDARD\">"
        "<resources></resources>"
        "<tracks>"
        "<track id=\"T1\" command=\"1\" title=\"One-shot\" loops=\"false\"/>"
        "<track id=\"T2\" command=\"2\" title=\"BGM\"/>"
        "</tracks></game></chipestra>";
    XmlParseResult result = ParseGameDefinition(xml);
    Check(result.Status == XmlParseStatus::Ok, "track loops=false: parses cleanly");
    Check(result.Definition.Tracks.size() == 2, "track loops=false: both tracks parsed");
    if (result.Definition.Tracks.size() == 2) {
        Check(!result.Definition.Tracks[0].Loops,
              "track loops=false: <track loops=\"false\"> is read as Loops == false");
        Check(result.Definition.Tracks[1].Loops,
              "track loops=false: a sibling <track> without loops=\"false\" stays Loops == true");
    }
}
