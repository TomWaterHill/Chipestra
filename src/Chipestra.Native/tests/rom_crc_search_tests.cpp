// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Tests of the CRC-32 search for ROM files whose names differ from the ones a game definition lists.
//
// The synthetic tests build their ROM Root under a temp directory from self-authored bytes. The tests at the end
// use the real cartridge and disk files when CHIPESTRA_ROM_ROOT is set: they repackage the bytes the normal lookup
// finds under other names, in a temp directory, and check the search finds them there.

#include "rom_crc_search_tests.h"

#include "chipestra_abi.h"
#include "definition_xml_locator.h"
#include "devices/nintendo/nes/nes_driver_config.h"
#include "devices/sega/megadrive/megadrive_machine.h"
#include "game_definition.h"
#include "machine_registry.h"
#include "resource_hash.h"
#include "resource_resolver.h"
#include "test_support.h"
#include "xml_definition_parser.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

const char* GetEnvVar(const char* name) {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    return std::getenv(name);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
}

std::optional<std::string> ReadFileText(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::nullopt;
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::filesystem::path MakeTempRomRoot(const std::string& testName) {
    auto root = std::filesystem::temp_directory_path() / "chipestra_romcrcsearch_tests" / testName;
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root);
    return root;
}

void WriteFile(const std::filesystem::path& path, const Bytes& data) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    if (!data.empty()) file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

struct ZipEntry {
    std::string Name;
    Bytes Data;
    std::optional<std::uint32_t> DeclaredCrc32;  // the CRC-32 the central directory claims (default: the real one)
};

// A ZIP of stored (uncompressed) entries.
Bytes BuildZip(const std::vector<ZipEntry>& entries) {
    Bytes out;
    auto u16 = [&out](std::uint16_t v) {
        out.push_back(static_cast<std::uint8_t>(v & 0xFF));
        out.push_back(static_cast<std::uint8_t>(v >> 8));
    };
    auto u32 = [&out](std::uint32_t v) {
        for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
    };
    struct Record {
        std::uint32_t offset;
        std::uint32_t crc;
        std::uint32_t size;
        std::string name;
    };
    std::vector<Record> records;
    for (const auto& entry : entries) {
        Record rec{static_cast<std::uint32_t>(out.size()),
                   entry.DeclaredCrc32.value_or(Crc32(entry.Data.empty() ? nullptr : entry.Data.data(), entry.Data.size())),
                   static_cast<std::uint32_t>(entry.Data.size()), entry.Name};
        records.push_back(rec);
        u32(0x04034b50);
        u16(20);
        u16(0);
        u16(0);
        u16(0);
        u16(0);
        u32(rec.crc);
        u32(rec.size);
        u32(rec.size);
        u16(static_cast<std::uint16_t>(entry.Name.size()));
        u16(0);
        out.insert(out.end(), entry.Name.begin(), entry.Name.end());
        out.insert(out.end(), entry.Data.begin(), entry.Data.end());
    }
    const auto centralStart = static_cast<std::uint32_t>(out.size());
    for (const auto& rec : records) {
        u32(0x02014b50);
        u16(20);
        u16(20);
        u16(0);
        u16(0);
        u16(0);
        u16(0);
        u32(rec.crc);
        u32(rec.size);
        u32(rec.size);
        u16(static_cast<std::uint16_t>(rec.name.size()));
        u16(0);
        u16(0);
        u16(0);
        u16(0);
        u32(0);
        u32(rec.offset);
        out.insert(out.end(), rec.name.begin(), rec.name.end());
    }
    const auto centralSize = static_cast<std::uint32_t>(out.size()) - centralStart;
    u32(0x06054b50);
    u16(0);
    u16(0);
    u16(static_cast<std::uint16_t>(records.size()));
    u16(static_cast<std::uint16_t>(records.size()));
    u32(centralSize);
    u32(centralStart);
    u16(0);
    return out;
}

Bytes Pattern(std::size_t size, std::uint8_t seed) {
    Bytes data(size);
    for (std::size_t i = 0; i < size; ++i) data[i] = static_cast<std::uint8_t>((i * 31 + (i >> 7) * 5 + seed) & 0xFF);
    return data;
}

bool SameBytes(const ResolvedResource* resource, const Bytes& expected) {
    return resource != nullptr && resource->Size() == expected.size() &&
           std::equal(expected.begin(), expected.end(), resource->Data());
}

RequiredResource CartRequest(const Bytes& content) {
    RequiredResource req;
    req.LogicalName = "cart";
    req.AcceptedNames = {"Game (Japan).nes"};
    req.SizeBytes = content.size();
    req.Crc32 = Crc32(content.data(), content.size());
    req.CrcSearch = true;
    req.Required = true;
    req.Archive = "Game (Japan)";
    return req;
}

bool Contains(const std::string& text, const char* part) {
    return text.find(part) != std::string::npos;
}

}  // namespace

void RomCrcSearchFindsRenamedZipEntry() {
    std::printf("RomCrcSearchFindsRenamedZipEntry\n");
    const Bytes content = Pattern(2000, 1);
    auto root = MakeTempRomRoot("renamed_entry");
    WriteFile(root / "anywhere" / "My Dump.zip", BuildZip({{"Game (Japan, USA).nes", content, std::nullopt}}));
    WriteFile(root / "other.zip", BuildZip({{"other.bin", Pattern(100, 9), std::nullopt}}));

    const auto result = ResourceResolver(root).Resolve({CartRequest(content)});
    Check(result.Ok, "an entry of a differently named ZIP and entry is found by its CRC-32");
    const ResolvedResource* found = result.Resources.Find("cart");
    Check(SameBytes(found, content), "the bytes of the entry found by the CRC-32 are the ones requested");
    Check(found != nullptr && found->FoundByCrcSearch(), "the resource is marked as found by the CRC-32 search");
    Check(found != nullptr && Contains(found->SourcePath(), "My Dump.zip!Game (Japan, USA).nes"),
          "the source path names the ZIP and the entry that were used");
}

void RomCrcSearchIsOnlyUsedWhenNoNameMatches() {
    std::printf("RomCrcSearchIsOnlyUsedWhenNoNameMatches\n");
    const Bytes content = Pattern(500, 2);
    const Bytes decoy = Pattern(500, 3);

    // The file with the listed name is there (and is the one used); a ZIP with the right content is also there.
    auto root = MakeTempRomRoot("name_first");
    WriteFile(root / "Game (Japan).zip", BuildZip({{"Game (Japan).nes", content, std::nullopt}}));
    WriteFile(root / "Elsewhere.zip", BuildZip({{"copy.nes", content, std::nullopt}}));
    auto result = ResourceResolver(root).Resolve({CartRequest(content)});
    Check(result.Ok && SameBytes(result.Resources.Find("cart"), content), "a file with the listed name resolves as before");
    Check(!result.Resources.Find("cart")->FoundByCrcSearch(), "a resolution by name is not marked as found by the CRC-32");
    Check(Contains(result.Resources.Find("cart")->SourcePath(), "Game (Japan).zip!Game (Japan).nes"),
          "a resolution by name also reports its source path");

    // A file with the listed name but other content stays a hash mismatch: the search is not a way around it.
    auto root2 = MakeTempRomRoot("name_mismatch");
    WriteFile(root2 / "Game (Japan).zip", BuildZip({{"Game (Japan).nes", decoy, std::nullopt}}));
    WriteFile(root2 / "Elsewhere.zip", BuildZip({{"copy.nes", content, std::nullopt}}));
    RequiredResource req = CartRequest(content);
    req.SizeBytes = content.size();
    result = ResourceResolver(root2).Resolve({req});
    Check(!result.Ok && result.Failures.size() == 1 && result.Failures[0].Result == CHIPESTRA_E_HASH_MISMATCH,
          "a file with the listed name but another CRC-32 is still a hash mismatch, not replaced by a search");

    // Without the opt-in the search does not happen.
    auto root3 = MakeTempRomRoot("no_opt_in");
    WriteFile(root3 / "Elsewhere.zip", BuildZip({{"copy.nes", content, std::nullopt}}));
    req = CartRequest(content);
    req.CrcSearch = false;
    result = ResourceResolver(root3).Resolve({req});
    Check(!result.Ok && result.Failures[0].Result == CHIPESTRA_E_NOT_FOUND, "a resource without crc_search is not searched for");
    req.CrcSearch = true;
    result = ResourceResolver(root3).Resolve({req});
    Check(result.Ok, "the same resource with crc_search is found");
}

void RomCrcSearchAcceptsAlternativeCrcValues() {
    std::printf("RomCrcSearchAcceptsAlternativeCrcValues\n");
    // A disk image with and without a 16-byte header: the search CRC-32 values list both.
    const Bytes headerless = Pattern(4000, 4);
    Bytes headered(16, 0);
    headered[0] = 'F';
    headered[1] = 'D';
    headered[2] = 'S';
    headered[3] = 0x1A;
    headered.insert(headered.end(), headerless.begin(), headerless.end());

    RequiredResource req;
    req.LogicalName = "disk";
    req.AcceptedNames = {"Disk.fds"};
    req.CrcSearch = true;
    req.SearchCrc32 = {Crc32(headered.data(), headered.size()), Crc32(headerless.data(), headerless.size())};
    req.Required = true;

    for (int variant = 0; variant < 2; ++variant) {
        const Bytes& image = variant == 0 ? headered : headerless;
        auto root = MakeTempRomRoot(variant == 0 ? "alt_headered" : "alt_headerless");
        WriteFile(root / "x.zip", BuildZip({{"Some Disk (Rev 1).bin", image, std::nullopt}}));
        const auto result = ResourceResolver(root).Resolve({req});
        Check(result.Ok && SameBytes(result.Resources.Find("disk"), image),
              variant == 0 ? "the image with its header is found by the first listed CRC-32"
                           : "the image without its header is found by the second listed CRC-32");
    }

    // The CRC-32 list of a normal lookup: a file with the listed name matching any one of the values is accepted.
    RequiredResource byName = req;
    byName.CrcSearch = false;
    byName.SearchCrc32.clear();
    byName.Crc32 = Crc32(headered.data(), headered.size());
    byName.AlternateCrc32 = {Crc32(headerless.data(), headerless.size())};
    auto root = MakeTempRomRoot("alt_by_name");
    WriteFile(root / "Disk.fds", headerless);
    const auto result = ResourceResolver(root).Resolve({byName});
    Check(result.Ok && SameBytes(result.Resources.Find("disk"), headerless), "a named file matching the second of two listed CRC-32 values is accepted");
}

void RomCrcSearchFallsBackToEntrySize() {
    std::printf("RomCrcSearchFallsBackToEntrySize\n");
    // A cartridge that comes as a plain image or, with another layout and 512 more bytes, as a copier file.
    const Bytes plain = Pattern(4096, 5);
    const Bytes copierFile = Pattern(4096 + 512, 6);

    RequiredResource req;
    req.LogicalName = "rom";
    req.AcceptedNames = {"Game.smd", "Game.bin"};
    req.CrcSearch = true;
    req.SearchCrc32 = {Crc32(plain.data(), plain.size())};
    req.SearchSizes = {plain.size() + 512};
    req.Required = true;

    auto root = MakeTempRomRoot("size_only");
    WriteFile(root / "a.zip", BuildZip({{"Game (World).smd", copierFile, std::nullopt}, {"readme.txt", Pattern(50, 7), std::nullopt}}));
    auto result = ResourceResolver(root).Resolve({req});
    Check(result.Ok && SameBytes(result.Resources.Find("rom"), copierFile) && result.Resources.Find("rom")->FoundByCrcSearch(),
          "an entry of the listed size is used when no entry has a listed CRC-32");

    // An entry with a listed CRC-32 wins over entries of the listed size.
    auto root2 = MakeTempRomRoot("crc_before_size");
    WriteFile(root2 / "a.zip", BuildZip({{"one.smd", copierFile, std::nullopt}}));
    WriteFile(root2 / "b.zip", BuildZip({{"two.md", plain, std::nullopt}}));
    result = ResourceResolver(root2).Resolve({req});
    Check(result.Ok && SameBytes(result.Resources.Find("rom"), plain), "an entry with a listed CRC-32 is preferred to entries of the listed size");

    // Two entries of the listed size and none with a listed CRC-32 are not told apart.
    auto root3 = MakeTempRomRoot("two_by_size");
    WriteFile(root3 / "a.zip", BuildZip({{"one.smd", copierFile, std::nullopt}}));
    WriteFile(root3 / "b.zip", BuildZip({{"two.smd", Pattern(4096 + 512, 8), std::nullopt}}));
    result = ResourceResolver(root3).Resolve({req});
    Check(!result.Ok && result.Failures[0].Result == CHIPESTRA_E_AMBIGUOUS, "two entries of the listed size are ambiguous");
}

void RomCrcSearchRefusesSeveralMatches() {
    std::printf("RomCrcSearchRefusesSeveralMatches\n");
    const Bytes content = Pattern(700, 10);
    auto root = MakeTempRomRoot("ambiguous");
    WriteFile(root / "one.zip", BuildZip({{"a.nes", content, std::nullopt}}));
    WriteFile(root / "sub" / "two.zip", BuildZip({{"b.nes", content, std::nullopt}}));
    const auto result = ResourceResolver(root).Resolve({CartRequest(content)});
    Check(!result.Ok && result.Failures.size() == 1 && result.Failures[0].Result == CHIPESTRA_E_AMBIGUOUS,
          "the same content in two ZIPs is CHIPESTRA_E_AMBIGUOUS, never an automatic choice");
    Check(Contains(result.Failures[0].Detail, "one.zip") && Contains(result.Failures[0].Detail, "two.zip"),
          "the failure lists the files that matched");
}

void RomCrcSearchReportsMismatches() {
    std::printf("RomCrcSearchReportsMismatches\n");
    const Bytes content = Pattern(900, 11);

    auto root = MakeTempRomRoot("nothing");
    WriteFile(root / "a.zip", BuildZip({{"a.nes", Pattern(900, 12), std::nullopt}}));
    auto result = ResourceResolver(root).Resolve({CartRequest(content)});
    Check(!result.Ok && result.Failures[0].Result == CHIPESTRA_E_NOT_FOUND, "no entry with a matching CRC-32 is CHIPESTRA_E_NOT_FOUND");

    // A central directory that claims the right CRC-32 for other bytes: the content is hashed again.
    auto root2 = MakeTempRomRoot("lying_directory");
    const std::uint32_t wanted = Crc32(content.data(), content.size());
    WriteFile(root2 / "a.zip", BuildZip({{"a.nes", Pattern(900, 13), wanted}}));
    result = ResourceResolver(root2).Resolve({CartRequest(content)});
    Check(!result.Ok && result.Failures[0].Result == CHIPESTRA_E_HASH_MISMATCH,
          "an entry whose content does not hash to the CRC-32 its directory declares is CHIPESTRA_E_HASH_MISMATCH");

    // Loose files are not searched.
    auto root3 = MakeTempRomRoot("loose");
    WriteFile(root3 / "renamed.nes", content);
    result = ResourceResolver(root3).Resolve({CartRequest(content)});
    Check(!result.Ok && result.Failures[0].Result == CHIPESTRA_E_NOT_FOUND, "a loose file with another name is not found by the search");

    // The size of the definition still has to match.
    auto root4 = MakeTempRomRoot("wrong_size");
    WriteFile(root4 / "a.zip", BuildZip({{"a.nes", content, std::nullopt}}));
    RequiredResource req = CartRequest(content);
    req.SizeBytes = content.size() + 1;
    result = ResourceResolver(root4).Resolve({req});
    Check(!result.Ok && result.Failures[0].Result == CHIPESTRA_E_NOT_FOUND, "an entry of another size than the definition's is not taken");
}

void RomCrcSearchDefinitionAttributesReachTheResolver() {
    std::printf("RomCrcSearchDefinitionAttributesReachTheResolver\n");
    const Bytes headered = Pattern(1234, 14);
    const Bytes headerless = Pattern(1200, 15);
    const std::string headeredCrc = FormatNumberList({Crc32(headered.data(), headered.size())}, 16);
    const std::string headerlessCrc = FormatNumberList({Crc32(headerless.data(), headerless.size())}, 16);
    const std::string xml =
        "<chipestra><game id=\"g\" title=\"G\" machine=\"M\" variant=\"V\" archive=\"G\"><resources>"
        "<rom name=\"a\" file=\"a.bin|a.md\" crc32=\"" + headeredCrc + "|" + headerlessCrc + "\" crc_search=\"true\"/>"
        "<rom name=\"b\" file=\"b.bin\" crc_search=\"true\" search_crc32=\"" + headeredCrc + "|" + headerlessCrc +
        "\" search_size=\"524800|1\"/>"
        "<rom name=\"c\" file=\"c.bin\"/>"
        "</resources><tracks><track id=\"t\" command=\"1\" title=\"T\"/></tracks></game></chipestra>";

    const XmlParseResult parsed = ParseGameDefinition(xml);
    Check(parsed.Status == XmlParseStatus::Ok, "a definition with crc32 lists, crc_search, search_crc32 and search_size parses");
    if (parsed.Status != XmlParseStatus::Ok) return;
    const auto& a = parsed.Definition.Resources[0];
    const auto& b = parsed.Definition.Resources[1];
    const auto& c = parsed.Definition.Resources[2];
    Check(a.Crc32 == Crc32(headered.data(), headered.size()) && a.AlternateCrc32.size() == 1 &&
              a.AlternateCrc32[0] == Crc32(headerless.data(), headerless.size()),
          "crc32=\"a|b\": the first value is the CRC-32, the rest are alternatives");
    Check(a.CrcSearch && !c.CrcSearch, "crc_search is off unless the definition says true");
    Check(!b.Crc32.has_value() && b.SearchCrc32.size() == 2 && b.SearchSizes.size() == 2 && b.SearchSizes[0] == 524800,
          "search_crc32 and search_size lists are read, and do not set the CRC-32 of a normal lookup");

    const std::vector<RequiredResource> required = parsed.Definition.BuildRequiredResources();
    Check(required[0].AlternateCrc32.size() == 1 && required[1].SearchSizes.size() == 2 && required[1].CrcSearch,
          "the required resources carry the lookup attributes");

    // Through the C ABI: parse, read the resources back, resolve, read the source.
    ChipestraGameDefinitionHandle handle = nullptr;
    char detail[256] = {};
    Check(chipestra_parse_game_definition(xml.c_str(), static_cast<uint32_t>(xml.size()), &handle, detail, sizeof(detail)) == CHIPESTRA_OK,
          "the C ABI parses the definition");
    if (handle == nullptr) return;
    std::vector<ChipestraRequiredResourceInfo> infos(3);
    for (uint32_t i = 0; i < 3; ++i) {
        std::memset(&infos[i], 0, sizeof(infos[i]));
        Check(chipestra_game_definition_get_resource(handle, i, &infos[i]) == CHIPESTRA_OK, "a resource is read back");
        infos[i].archive[0] = 'G';
    }
    Check(infos[0].crc_search == 1 && std::string(infos[0].crc32_alternatives) == headerlessCrc && infos[2].crc_search == 0,
          "the C ABI reports crc32 alternatives and crc_search");
    Check(std::string(infos[1].search_crc32) == headeredCrc + "|" + headerlessCrc && std::string(infos[1].search_size) == "524800|1",
          "the C ABI reports the search lists");

    auto root = MakeTempRomRoot("abi");
    WriteFile(root / "Whatever.zip", BuildZip({{"x.rom", headerless, std::nullopt}}));
    ChipestraResourceResolutionHandle resolution = nullptr;
    Check(chipestra_resolve_resources(root.string().c_str(), infos.data(), 2, &resolution) == CHIPESTRA_OK, "the C ABI resolves");
    ChipestraResolutionSummary summary{};
    chipestra_resource_resolution_get_summary(resolution, &summary);
    Check(summary.ok != 0 && summary.resolved_count == 2, "both resources resolve through the search with the lists from the C ABI");
    ChipestraResolvedSource source{};
    Check(chipestra_resource_resolution_get_source(resolution, 0, &source) == CHIPESTRA_OK && source.found_by_crc == 1 &&
              Contains(source.path, "Whatever.zip!x.rom") && std::string(source.logical_name) == "a",
          "the C ABI reports which file was used and that it was found by its CRC-32");
    Check(chipestra_resource_resolution_get_source(resolution, 2, &source) == CHIPESTRA_E_NOT_FOUND, "an index past the resolved ones is NOT_FOUND");
    chipestra_free_resource_resolution(resolution);
    chipestra_free_game_definition(handle);

    // Invalid values are errors in the definition.
    const char* bad[] = {"crc32=\"12|zz\"", "crc32=\"\"", "crc_search=\"yes\"", "search_crc32=\"12|\"", "search_size=\"abc\""};
    for (const char* attribute : bad) {
        const std::string badXml = std::string("<chipestra><game id=\"g\" title=\"G\" machine=\"M\" variant=\"V\"><resources><rom name=\"a\" file=\"a\" ") +
                                   attribute + "/></resources><tracks><track id=\"t\" command=\"1\" title=\"T\"/></tracks></game></chipestra>";
        Check(ParseGameDefinition(badXml).Status == XmlParseStatus::InvalidAttributeValue,
              (std::string("a definition with ") + attribute + " is rejected").c_str());
    }
}

void RomCrcSearchRepositoryDefinitionsAreOptedIn() {
    std::printf("RomCrcSearchRepositoryDefinitionsAreOptedIn\n");
    // Every console cartridge or disk of the repository's definitions can be found by its CRC-32; arcade ROMs are not searched.
    const char* files[] = {"starsoldier.xml", "smb3.xml", "akumajou.xml", "lagrange.xml", "rthunder.xml", "gimmick.xml",
                           "silversurfer.xml", "metroid.xml", "spaceharrier2.xml"};
    for (const char* file : files) {
        const auto path = FindDefinitionXml(file);
        Check(path.has_value(), (std::string(file) + " is present").c_str());
        if (!path) continue;
        const auto text = ReadFileText(*path);
        const XmlParseResult parsed = ParseGameDefinition(text.value_or(""));
        Check(parsed.Status == XmlParseStatus::Ok, (std::string(file) + " parses").c_str());
        if (parsed.Status != XmlParseStatus::Ok) continue;
        for (const auto& rom : parsed.Definition.Resources) {
            const bool hasCrc = rom.Crc32.has_value() || !rom.SearchCrc32.empty();
            Check(rom.CrcSearch && hasCrc, (std::string(file) + ": <rom name=\"" + rom.Name + "\"> is searched by CRC-32").c_str());
        }
    }
    for (const char* file : {"outrun.xml", "gradius.xml", "dspirit.xml"}) {
        const auto path = FindDefinitionXml(file);
        if (!path) continue;
        const XmlParseResult parsed = ParseGameDefinition(ReadFileText(*path).value_or(""));
        bool any = false;
        for (const auto& rom : parsed.Definition.Resources) any = any || rom.CrcSearch;
        Check(parsed.Status == XmlParseStatus::Ok && !any, (std::string(file) + ": arcade ROMs are not searched by CRC-32").c_str());
    }
}

namespace {

struct LoadedDefinition {
    GameDefinition definition;
    std::string text;
    bool ok = false;
};

LoadedDefinition LoadRepositoryDefinition(const char* file) {
    LoadedDefinition loaded;
    const auto path = FindDefinitionXml(file);
    if (!path) return loaded;
    loaded.text = ReadFileText(*path).value_or("");
    const XmlParseResult parsed = ParseGameDefinition(loaded.text);
    if (parsed.Status != XmlParseStatus::Ok) return loaded;
    loaded.definition = parsed.Definition;
    loaded.ok = true;
    return loaded;
}

// Resolves `file`'s definition under the real ROM Root by the normal lookup, and returns the bytes of resource `name`.
std::optional<Bytes> RealBytes(const char* romRoot, const GameDefinition& definition, const char* name) {
    const auto result = ResourceResolver(std::filesystem::path(romRoot)).Resolve(definition.BuildRequiredResources());
    const ResolvedResource* found = result.Resources.Find(name);
    if (!result.Ok || found == nullptr) return std::nullopt;
    return Bytes(found->Data(), found->Data() + found->Size());
}

// Puts `content` in a ZIP and entry that have none of the names the definition lists, resolves the definition there,
// and checks the search found it.
bool ResolvesRenamed(const GameDefinition& definition, const char* name, const Bytes& content, const std::string& entryName,
                     const std::string& testName, const ResolvedResource** outFound, ResourceResolutionResult& result) {
    auto root = MakeTempRomRoot(testName);
    WriteFile(root / "Renamed" / "A User's Dump.zip", BuildZip({{entryName, content, std::nullopt}}));
    result = ResourceResolver(root).Resolve(definition.BuildRequiredResources());
    *outFound = result.Resources.Find(name);
    return result.Ok && *outFound != nullptr && SameBytes(*outFound, content) && (*outFound)->FoundByCrcSearch();
}

}  // namespace

void RomCrcSearchFindsRenamedRealRoms() {
    std::printf("RomCrcSearchFindsRenamedRealRoms\n");
    const char* romRoot = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRoot == nullptr || romRoot[0] == '\0') {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is not set - real cartridges and disks not exercised\n");
        return;
    }

    // NES: the cartridge is found by the CRC-32 and size of the definition.
    for (const char* file : {"starsoldier.xml", "smb3.xml", "gimmick.xml"}) {
        const LoadedDefinition game = LoadRepositoryDefinition(file);
        Check(game.ok, (std::string(file) + " parses").c_str());
        if (!game.ok) continue;
        const auto bytes = RealBytes(romRoot, game.definition, "cart");
        if (!bytes) {
            std::printf("  SKIP: %s - cartridge not under CHIPESTRA_ROM_ROOT\n", file);
            continue;
        }
        const ResolvedResource* found = nullptr;
        ResourceResolutionResult result;
        Check(ResolvesRenamed(game.definition, "cart", *bytes, "Renamed (Rev A) [!].nes", std::string("nes_") + file, &found, result),
              (std::string(file) + ": the cartridge is found under another ZIP and entry name").c_str());
    }

    // FDS: the disk image with its header and without it.
    {
        const LoadedDefinition game = LoadRepositoryDefinition("metroid.xml");
        Check(game.ok, "metroid.xml parses");
        const auto bytes = game.ok ? RealBytes(romRoot, game.definition, "disk") : std::nullopt;
        if (bytes) {
            const bool headered = bytes->size() > 16 && (*bytes)[0] == 'F' && (*bytes)[1] == 'D' && (*bytes)[2] == 'S' && (*bytes)[3] == 0x1A;
            const Bytes headerless = headered ? Bytes(bytes->begin() + 16, bytes->end()) : *bytes;
            Bytes withHeader(16, 0);
            withHeader[0] = 'F';
            withHeader[1] = 'D';
            withHeader[2] = 'S';
            withHeader[3] = 0x1A;
            withHeader[4] = static_cast<std::uint8_t>(headerless.size() / 65500);
            withHeader.insert(withHeader.end(), headerless.begin(), headerless.end());
            const ResolvedResource* found = nullptr;
            ResourceResolutionResult result;
            Check(ResolvesRenamed(game.definition, "disk", headerless, "Famicom Disk (1986).bin", "fds_headerless", &found, result),
                  "the disk image without a header is found under another name");
            Check(ResolvesRenamed(game.definition, "disk", withHeader, "Famicom Disk (1986).fds", "fds_headered", &found, result),
                  "the disk image with its header is found under another name");
        } else {
            std::printf("  SKIP: the Metroid disk is not under CHIPESTRA_ROM_ROOT\n");
        }
    }

    // Mega Drive: a plain image is found by its CRC-32, a copier file by its size, and both give the same cartridge.
    {
        const LoadedDefinition game = LoadRepositoryDefinition("spaceharrier2.xml");
        Check(game.ok, "spaceharrier2.xml parses");
        const auto original = game.ok ? RealBytes(romRoot, game.definition, "rom") : std::nullopt;
        if (original) {
            const Bytes plain = DecodeMegaDriveCartridge(original->data(), original->size());
            Bytes copierFile = *original;
            if (original->size() == plain.size()) {
                // The user's file is plain: there is no copier file to test with; make one the way a copier would.
                copierFile.clear();
            }
            const std::string driverXml = ExtractDriverElementXml(game.text);
            const ResolvedResource* found = nullptr;
            ResourceResolutionResult result;
            Check(ResolvesRenamed(game.definition, "rom", plain, "Space Harrier II (Japan, USA).md", "md_plain", &found, result),
                  "a plain image is found under another name by its CRC-32");
            if (found != nullptr) {
                MachineResources resources;
                resources.Add(ResolvedResource("rom", Bytes(found->Data(), found->Data() + found->Size())));
                resources.Add(ResolvedResource("driver_xml", Bytes(driverXml.begin(), driverXml.end())));
                Check(AssembleMegaDriveMachineInputs(resources).cartridge == plain, "the plain image found by the search passes the cart_crc32 check");
            }
            if (!copierFile.empty()) {
                Check(ResolvesRenamed(game.definition, "rom", copierFile, "Space Harrier II (J) [!].smd", "md_smd", &found, result),
                      "a copier file (plain size + 512 bytes) is found under another name by its size");
                if (found != nullptr) {
                    MachineResources resources;
                    resources.Add(ResolvedResource("rom", Bytes(found->Data(), found->Data() + found->Size())));
                    resources.Add(ResolvedResource("driver_xml", Bytes(driverXml.begin(), driverXml.end())));
                    Check(AssembleMegaDriveMachineInputs(resources).cartridge == plain,
                          "the copier file found by the search is laid out again and passes the cart_crc32 check");
                }
            } else {
                std::printf("  SKIP: the Space Harrier II file under CHIPESTRA_ROM_ROOT is a plain image - no copier file to search for\n");
            }
        } else {
            std::printf("  SKIP: the Space Harrier II cartridge is not under CHIPESTRA_ROM_ROOT\n");
        }
    }
}
