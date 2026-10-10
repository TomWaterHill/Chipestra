// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Resource Resolver native tests.
//
// All fixtures are synthetic, self-authored byte sequences (dummy loose
// files, hand-constructed minimal ZIP archives) built at test time under a
// temp directory - no real ROM data is used or committed anywhere.

#include "resource_resolver_tests.h"

#include "machine_registry.h"
#include "resource_hash.h"
#include "resource_resolver.h"
#include "test_support.h"
#include "zip_index.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------
// Synthetic fixture helpers
// ---------------------------------------------------------------------

std::filesystem::path MakeTempRomRoot(const std::string& testName) {
    auto root = std::filesystem::temp_directory_path() / "chipestra_resourceresolver_tests" / testName;
    std::error_code ec;
    std::filesystem::remove_all(root, ec);  // clean up any leftover from a previous run
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

// Encodes `data` as a single raw-DEFLATE "stored" block (BFINAL=1,
// BTYPE=00): a real, decodable DEFLATE stream that happens not to compress
// anything. Lets tests exercise ZipIndex's actual tinfl decompression path
// without hand-writing a Huffman encoder. data.size() must fit in 16 bits.
std::vector<std::uint8_t> BuildStoredDeflateBlock(const std::vector<std::uint8_t>& data) {
    std::vector<std::uint8_t> out;
    out.push_back(0x01);  // BFINAL=1, BTYPE=00, rest of byte is padding
    auto len = static_cast<std::uint16_t>(data.size());
    auto nlen = static_cast<std::uint16_t>(~len);
    out.push_back(static_cast<std::uint8_t>(len & 0xFF));
    out.push_back(static_cast<std::uint8_t>((len >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(nlen & 0xFF));
    out.push_back(static_cast<std::uint8_t>((nlen >> 8) & 0xFF));
    out.insert(out.end(), data.begin(), data.end());
    return out;
}

// One entry to synthesize into a hand-built ZIP via BuildSyntheticZip().
struct SyntheticZipEntry {
    std::string Name;
    std::vector<std::uint8_t> UncompressedData;
    std::uint16_t CompressionMethod = 0;  // 0 = Stored, 8 = Deflate (auto-encoded via BuildStoredDeflateBlock)

    // Test-only escape hatches for exercising failure paths:
    std::optional<std::uint64_t> DeclaredUncompressedSizeOverride;    // lie about the size (ZIP bomb tests)
    std::optional<std::vector<std::uint8_t>> RawCompressedDataOverride;  // arbitrary bytes for an unsupported method
};

// Hand-builds a minimal ZIP (local file headers + central directory + EOCD)
// from `entries`, mirroring exactly the record layout ZipIndex parses. Used
// instead of a real ZIP library so every fixture byte is visibly
// self-authored (and no external tool dependency for the test suite).
std::vector<std::uint8_t> BuildSyntheticZip(const std::vector<SyntheticZipEntry>& entries) {
    std::vector<std::uint8_t> out;

    auto putU16 = [&out](std::uint16_t v) {
        out.push_back(static_cast<std::uint8_t>(v & 0xFF));
        out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    };
    auto putU32 = [&out](std::uint32_t v) {
        out.push_back(static_cast<std::uint8_t>(v & 0xFF));
        out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
        out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
        out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
    };

    struct LocalRecord {
        std::uint32_t offset;
        std::uint32_t crc32;
        std::uint32_t compressedSize;
        std::uint32_t declaredUncompressedSize;
        std::uint16_t compressionMethod;
        std::string name;
    };
    std::vector<LocalRecord> records;

    for (const auto& entry : entries) {
        std::vector<std::uint8_t> compressedData;
        if (entry.RawCompressedDataOverride.has_value()) {
            compressedData = *entry.RawCompressedDataOverride;
        } else if (entry.CompressionMethod == 8) {
            compressedData = BuildStoredDeflateBlock(entry.UncompressedData);
        } else {
            compressedData = entry.UncompressedData;
        }
        const std::uint32_t crc =
            Crc32(entry.UncompressedData.empty() ? nullptr : entry.UncompressedData.data(),
                  entry.UncompressedData.size());
        const auto declaredUncompressedSize = static_cast<std::uint32_t>(
            entry.DeclaredUncompressedSizeOverride.value_or(entry.UncompressedData.size()));

        LocalRecord rec;
        rec.offset = static_cast<std::uint32_t>(out.size());
        rec.crc32 = crc;
        rec.compressedSize = static_cast<std::uint32_t>(compressedData.size());
        rec.declaredUncompressedSize = declaredUncompressedSize;
        rec.compressionMethod = entry.CompressionMethod;
        rec.name = entry.Name;
        records.push_back(rec);

        putU32(0x04034b50);  // local file header signature
        putU16(20);           // version needed to extract
        putU16(0);             // general purpose bit flag
        putU16(entry.CompressionMethod);
        putU16(0);  // last mod file time
        putU16(0);  // last mod file date
        putU32(crc);
        putU32(rec.compressedSize);
        putU32(declaredUncompressedSize);
        putU16(static_cast<std::uint16_t>(entry.Name.size()));
        putU16(0);  // extra field length
        out.insert(out.end(), entry.Name.begin(), entry.Name.end());
        out.insert(out.end(), compressedData.begin(), compressedData.end());
    }

    const auto centralDirStart = static_cast<std::uint32_t>(out.size());
    for (const auto& rec : records) {
        putU32(0x02014b50);  // central directory file header signature
        putU16(20);            // version made by
        putU16(20);            // version needed to extract
        putU16(0);              // general purpose bit flag
        putU16(rec.compressionMethod);
        putU16(0);  // last mod file time
        putU16(0);  // last mod file date
        putU32(rec.crc32);
        putU32(rec.compressedSize);
        putU32(rec.declaredUncompressedSize);
        putU16(static_cast<std::uint16_t>(rec.name.size()));
        putU16(0);  // extra field length
        putU16(0);  // file comment length
        putU16(0);  // disk number start
        putU16(0);  // internal file attributes
        putU32(0);  // external file attributes
        putU32(rec.offset);
        out.insert(out.end(), rec.name.begin(), rec.name.end());
    }
    const auto centralDirSize = static_cast<std::uint32_t>(out.size()) - centralDirStart;

    putU32(0x06054b50);  // end of central directory signature
    putU16(0);             // disk number
    putU16(0);             // disk with start of central directory
    putU16(static_cast<std::uint16_t>(records.size()));
    putU16(static_cast<std::uint16_t>(records.size()));
    putU32(centralDirSize);
    putU32(centralDirStart);
    putU16(0);  // comment length

    return out;
}

bool BytesEqual(const ResolvedResource* resource, const std::vector<std::uint8_t>& expected) {
    return resource != nullptr && resource->Size() == expected.size() &&
           std::equal(expected.begin(), expected.end(), resource->Data());
}

}  // namespace

// ---------------------------------------------------------------------
// Loose-file resolution
// ---------------------------------------------------------------------

void ResourceResolverLooseFileResolved() {
    auto root = MakeTempRomRoot("loose_resolved");
    const std::vector<std::uint8_t> data = {0x01, 0x02, 0x03, 0x04, 0x05};
    WriteFile(root / "maincpu.bin", data);

    ResourceResolver resolver(root);
    RequiredResource req;
    req.LogicalName = "maincpu";
    req.AcceptedNames = {"maincpu.bin"};
    req.Required = true;

    auto result = resolver.Resolve({req});
    Check(result.Ok, "loose file: single matching candidate resolves (Ok)");
    Check(result.Failures.empty(), "loose file: no failures reported");
    Check(BytesEqual(result.Resources.Find("maincpu"), data), "loose file: resolved bytes match the synthetic fixture");
}

void ResourceResolverMissing() {
    auto root = MakeTempRomRoot("missing");

    ResourceResolver resolver(root);
    RequiredResource req;
    req.LogicalName = "maincpu";
    req.AcceptedNames = {"maincpu.bin"};
    req.Required = true;

    auto result = resolver.Resolve({req});
    Check(!result.Ok, "missing: overall result is not Ok when a required resource is missing");
    Check(result.Failures.size() == 1 && result.Failures[0].Result == CHIPESTRA_E_NOT_FOUND,
          "missing: CHIPESTRA_E_NOT_FOUND reported for zero matching candidates");
}

void ResourceResolverAmbiguous() {
    auto root = MakeTempRomRoot("ambiguous");
    WriteFile(root / "d1" / "maincpu.bin", {0x01, 0x02});
    WriteFile(root / "d2" / "maincpu.bin", {0x03, 0x04, 0x05});

    ResourceResolver resolver(root);
    RequiredResource req;
    req.LogicalName = "maincpu";
    req.AcceptedNames = {"maincpu.bin"};
    req.Required = true;

    auto result = resolver.Resolve({req});
    Check(!result.Ok, "ambiguous: overall result is not Ok");
    Check(result.Failures.size() == 1 && result.Failures[0].Result == CHIPESTRA_E_AMBIGUOUS,
          "ambiguous: CHIPESTRA_E_AMBIGUOUS reported for multiple same-name candidates, never auto-selected");
}

// ---------------------------------------------------------------------
// Hash verification
// ---------------------------------------------------------------------

void ResourceResolverHashMismatch() {
    auto root = MakeTempRomRoot("hash_mismatch");
    const std::vector<std::uint8_t> data = {0xAA, 0xBB, 0xCC};
    WriteFile(root / "maincpu.bin", data);

    ResourceResolver resolver(root);
    RequiredResource req;
    req.LogicalName = "maincpu";
    req.AcceptedNames = {"maincpu.bin"};
    req.Sha1 = "0000000000000000000000000000000000dead";  // deliberately wrong
    req.Required = true;

    auto result = resolver.Resolve({req});
    Check(!result.Ok, "hash mismatch: overall result is not Ok");
    Check(result.Failures.size() == 1 && result.Failures[0].Result == CHIPESTRA_E_HASH_MISMATCH,
          "hash mismatch: CHIPESTRA_E_HASH_MISMATCH reported, candidate not accepted");
}

void ResourceResolverUnspecifiedHashNotRequired() {
    auto root = MakeTempRomRoot("hash_unspecified");
    const std::vector<std::uint8_t> data = {0x10, 0x20, 0x30};
    WriteFile(root / "maincpu.bin", data);

    ResourceResolver resolver(root);
    RequiredResource req;
    req.LogicalName = "maincpu";
    req.AcceptedNames = {"maincpu.bin"};
    // Crc32/Sha1 intentionally left unset.
    req.Required = true;

    auto result = resolver.Resolve({req});
    Check(result.Ok, "unspecified hash: resolves successfully without a hash requirement");
}

void ResourceResolverCorrectHashAccepted() {
    auto root = MakeTempRomRoot("hash_correct");
    const std::vector<std::uint8_t> data = {0x11, 0x22, 0x33, 0x44};
    WriteFile(root / "maincpu.bin", data);

    ResourceResolver resolver(root);
    RequiredResource req;
    req.LogicalName = "maincpu";
    req.AcceptedNames = {"maincpu.bin"};
    req.Crc32 = Crc32(data.data(), data.size());
    req.Sha1 = Sha1Hex(data.data(), data.size());
    req.Required = true;

    auto result = resolver.Resolve({req});
    Check(result.Ok, "correct hash: resolves successfully when Crc32/Sha1 both match");
}

// ---------------------------------------------------------------------
// ZIP entries
// ---------------------------------------------------------------------

void ResourceResolverZipEntryResolved() {
    auto root = MakeTempRomRoot("zip_resolved");
    const std::vector<std::uint8_t> data = {'H', 'E', 'L', 'L', 'O'};

    SyntheticZipEntry entry;
    entry.Name = "subcpu.bin";
    entry.UncompressedData = data;
    entry.CompressionMethod = 8;  // Deflate (real tinfl round-trip via a stored block)
    WriteFile(root / "roms.zip", BuildSyntheticZip({entry}));

    ResourceResolver resolver(root);
    RequiredResource req;
    req.LogicalName = "subcpu";
    req.AcceptedNames = {"subcpu.bin"};
    req.Required = true;

    auto result = resolver.Resolve({req});
    Check(result.Ok, "zip entry: Deflate-compressed entry resolves successfully");
    Check(BytesEqual(result.Resources.Find("subcpu"), data),
          "zip entry: decompressed bytes match the original synthetic data");
}

void ResourceResolverLooseAndZipBothCandidates() {
    auto root = MakeTempRomRoot("loose_and_zip");
    WriteFile(root / "shared.bin", {0x01});

    SyntheticZipEntry entry;
    entry.Name = "shared.bin";
    entry.UncompressedData = {0x02, 0x03};
    entry.CompressionMethod = 0;  // Stored
    WriteFile(root / "archive.zip", BuildSyntheticZip({entry}));

    ResourceResolver resolver(root);
    RequiredResource req;
    req.LogicalName = "shared";
    req.AcceptedNames = {"shared.bin"};
    req.Required = true;

    // An identically-named loose file and ZIP entry are two distinct
    // candidates -> ambiguous. This proves both loose and ZIP entries are
    // considered as candidates.
    auto result = resolver.Resolve({req});
    Check(!result.Ok && result.Failures.size() == 1 && result.Failures[0].Result == CHIPESTRA_E_AMBIGUOUS,
          "loose+zip: identically-named loose file and zip entry both count as candidates");
}

void ResourceResolverSizeFilterNarrowsCandidates() {
    auto root = MakeTempRomRoot("size_filter");
    WriteFile(root / "d1" / "maincpu.bin", std::vector<std::uint8_t>(4, 0xAA));
    WriteFile(root / "d2" / "maincpu.bin", std::vector<std::uint8_t>(8, 0xBB));

    ResourceResolver resolver(root);
    RequiredResource req;
    req.LogicalName = "maincpu";
    req.AcceptedNames = {"maincpu.bin"};
    req.SizeBytes = 8;
    req.Required = true;

    auto result = resolver.Resolve({req});
    Check(result.Ok, "size filter: expected size narrows two same-named candidates down to exactly one");
    const ResolvedResource* resolved = result.Resources.Find("maincpu");
    Check(resolved != nullptr && resolved->Size() == 8, "size filter: the resolved candidate matches SizeBytes");
}

void ResourceResolverOptionalResourceMissingDoesNotBlock() {
    auto root = MakeTempRomRoot("optional_missing");

    ResourceResolver resolver(root);
    RequiredResource req;
    req.LogicalName = "optional_extra";
    req.AcceptedNames = {"optional_extra.bin"};
    req.Required = false;

    auto result = resolver.Resolve({req});
    Check(result.Ok, "optional: a missing optional (Required=false) resource does not block overall Ok");
    Check(result.Resources.Find("optional_extra") == nullptr,
          "optional: a missing optional resource is simply absent, not fabricated");
}

// ---------------------------------------------------------------------
// Safety: path traversal, ZIP bomb cap, unsupported compression
// ---------------------------------------------------------------------

void ResourceResolverRejectsOversizedZipEntry() {
    auto root = MakeTempRomRoot("resolver_zip_bomb");

    SyntheticZipEntry oversized;
    oversized.Name = "maincpu.bin";
    oversized.UncompressedData = {0x01, 0x02};
    oversized.CompressionMethod = 0;
    oversized.DeclaredUncompressedSizeOverride = 10ull * 1024 * 1024;  // lies: declares 10 MiB
    WriteFile(root / "bomb.zip", BuildSyntheticZip({oversized}));

    ResourceResolver resolver(root, /*maxUncompressedBytesPerResource=*/1024);
    RequiredResource req;
    req.LogicalName = "maincpu";
    req.AcceptedNames = {"maincpu.bin"};
    req.Required = true;

    auto result = resolver.Resolve({req});
    Check(!result.Ok && result.Failures.size() == 1 && result.Failures[0].Result == CHIPESTRA_E_UNSUPPORTED,
          "zip bomb: an entry exceeding the decompression cap resolves to CHIPESTRA_E_UNSUPPORTED, not truncated data");
}

void ResourceResolverDeterministicAcrossRepeatedResolves() {
    auto root = MakeTempRomRoot("determinism");
    WriteFile(root / "a.bin", {0x01, 0x02, 0x03});
    WriteFile(root / "sub" / "b.bin", {0x04, 0x05});

    SyntheticZipEntry entry;
    entry.Name = "c.bin";
    entry.UncompressedData = {0x06, 0x07, 0x08, 0x09};
    entry.CompressionMethod = 0;
    WriteFile(root / "pack.zip", BuildSyntheticZip({entry}));

    ResourceResolver resolver(root);
    RequiredResource reqA;
    reqA.LogicalName = "a";
    reqA.AcceptedNames = {"a.bin"};
    reqA.Required = true;
    RequiredResource reqB;
    reqB.LogicalName = "b";
    reqB.AcceptedNames = {"b.bin"};
    reqB.Required = true;
    RequiredResource reqC;
    reqC.LogicalName = "c";
    reqC.AcceptedNames = {"c.bin"};
    reqC.Required = true;
    const std::vector<RequiredResource> reqs = {reqA, reqB, reqC};

    auto result1 = resolver.Resolve(reqs);
    auto result2 = resolver.Resolve(reqs);

    Check(result1.Ok && result2.Ok, "determinism: both resolve attempts succeed");

    bool sameBytes = true;
    for (const char* logicalName : {"a", "b", "c"}) {
        const ResolvedResource* r1 = result1.Resources.Find(logicalName);
        const ResolvedResource* r2 = result2.Resources.Find(logicalName);
        if (r1 == nullptr || r2 == nullptr || r1->Size() != r2->Size() ||
            !std::equal(r1->Data(), r1->Data() + r1->Size(), r2->Data())) {
            sameBytes = false;
        }
    }
    Check(sameBytes, "determinism: repeated Resolve() calls against an unchanged ROM Root agree byte-for-byte");
}

void ZipIndexRejectsPathTraversal() {
    auto root = MakeTempRomRoot("zip_traversal");

    SyntheticZipEntry good;
    good.Name = "ok.bin";
    good.UncompressedData = {0x01};

    SyntheticZipEntry traversal1;
    traversal1.Name = "../evil1.bin";
    traversal1.UncompressedData = {0x02};

    SyntheticZipEntry traversal2;
    traversal2.Name = "sub/../../evil2.bin";
    traversal2.UncompressedData = {0x03};

    SyntheticZipEntry absolute;
    absolute.Name = "/evil3.bin";
    absolute.UncompressedData = {0x04};

    SyntheticZipEntry drivePrefix;
    drivePrefix.Name = "C:evil4.bin";
    drivePrefix.UncompressedData = {0x05};

    auto zipPath = root / "traversal.zip";
    WriteFile(zipPath, BuildSyntheticZip({good, traversal1, traversal2, absolute, drivePrefix}));

    ZipIndex index(zipPath);
    Check(index.Entries().size() == 1, "zip index: only the safe entry name survives indexing");
    if (index.Entries().size() == 1) {
        Check(index.Entries()[0].Name == "ok.bin", "zip index: the surviving entry is the safe one");
    }
}

void ZipIndexRejectsUnsupportedCompressionMethod() {
    auto root = MakeTempRomRoot("zip_unsupported_method");

    SyntheticZipEntry entry;
    entry.Name = "weird.bin";
    entry.UncompressedData = {0xAA, 0xBB};
    entry.CompressionMethod = 12;  // BZIP2 - not Stored (0) or Deflate (8)
    entry.RawCompressedDataOverride = std::vector<std::uint8_t>{0xAA, 0xBB};  // arbitrary, never decoded

    auto zipPath = root / "weird.zip";
    WriteFile(zipPath, BuildSyntheticZip({entry}));

    ZipIndex index(zipPath);
    Check(index.Entries().size() == 1, "unsupported method: entry is still indexed (rejected at read time)");

    bool threw = false;
    try {
        index.ReadEntry(index.Entries()[0], 1024 * 1024);
    } catch (const std::exception&) {
        threw = true;
    }
    Check(threw, "unsupported method: ReadEntry refuses a non-Stored/Deflate compression method");
}

void ZipIndexEnforcesUncompressedSizeCap() {
    auto root = MakeTempRomRoot("zip_bomb");

    SyntheticZipEntry oversized;
    oversized.Name = "bomb.bin";
    oversized.UncompressedData = {0x01, 0x02, 0x03, 0x04};
    oversized.DeclaredUncompressedSizeOverride = 10ull * 1024 * 1024;  // lies: declares 10 MiB

    auto zipPath = root / "bomb.zip";
    WriteFile(zipPath, BuildSyntheticZip({oversized}));

    ZipIndex index(zipPath);
    Check(index.Entries().size() == 1, "zip bomb: entry is still indexed (cap enforced at read time, not index time)");

    bool threw = false;
    try {
        index.ReadEntry(index.Entries()[0], /*maxUncompressedBytes=*/1024);
    } catch (const std::exception&) {
        threw = true;
    }
    Check(threw, "zip bomb: ReadEntry refuses an entry whose declared size exceeds the cap");
}

// ---------------------------------------------------------------------
// Archive-scoped resolution
// ---------------------------------------------------------------------

void ResourceResolverArchiveScopeDisambiguatesZips() {
    auto root = MakeTempRomRoot("archive_scope_zips");

    // Two ZIPs, each holding a same-named entry with different content -
    // exactly the "two ROM revisions sharing filenames" scenario archive
    // scoping exists to solve. Without an archive scope this would be CHIPESTRA_E_AMBIGUOUS
    // (ResourceResolverAmbiguous/ResourceResolverLooseAndZipBothCandidates
    // already cover that case).
    SyntheticZipEntry entryA;
    entryA.Name = "maincpu.bin";
    entryA.UncompressedData = {0x01, 0x01};
    WriteFile(root / "setA.zip", BuildSyntheticZip({entryA}));

    SyntheticZipEntry entryB;
    entryB.Name = "maincpu.bin";
    entryB.UncompressedData = {0x02, 0x02, 0x02};
    WriteFile(root / "setB.zip", BuildSyntheticZip({entryB}));

    ResourceResolver resolver(root);
    RequiredResource req;
    req.LogicalName = "maincpu";
    req.AcceptedNames = {"maincpu.bin"};
    req.Archive = "setB";  // matched case-insensitively against the zip's stem
    req.Required = true;

    auto result = resolver.Resolve({req});
    Check(result.Ok, "archive scope (zip): scoping to 'setB' resolves despite a same-named entry in 'setA.zip'");
    Check(BytesEqual(result.Resources.Find("maincpu"), entryB.UncompressedData),
          "archive scope (zip): the resolved bytes come from the scoped archive, not the other one");
}

void ResourceResolverArchiveScopeDisambiguatesFolders() {
    auto root = MakeTempRomRoot("archive_scope_folders");

    // Same scenario as above, but with the "unzipped, one folder per ROM
    // set" layout.
    WriteFile(root / "aburner2" / "maincpu.bin", {0x0A, 0x0B});
    WriteFile(root / "aburner2_3ds" / "maincpu.bin", {0x0C, 0x0D, 0x0E});

    ResourceResolver resolver(root);
    RequiredResource req;
    req.LogicalName = "maincpu";
    req.AcceptedNames = {"maincpu.bin"};
    req.Archive = "aburner2";
    req.Required = true;

    auto result = resolver.Resolve({req});
    Check(result.Ok, "archive scope (folder): scoping to 'aburner2' resolves despite 'aburner2_3ds' sharing the filename");
    Check(BytesEqual(result.Resources.Find("maincpu"), std::vector<std::uint8_t>{0x0A, 0x0B}),
          "archive scope (folder): the resolved bytes come from the 'aburner2' folder, not 'aburner2_3ds'");
}

void ResourceResolverArchiveScopeExcludesOtherArchives() {
    auto root = MakeTempRomRoot("archive_scope_excludes");

    // Only the *other* archive has the file - proves the scope actually
    // excludes candidates outside it, rather than merely reordering them.
    WriteFile(root / "otherset" / "maincpu.bin", {0x01});

    ResourceResolver resolver(root);
    RequiredResource req;
    req.LogicalName = "maincpu";
    req.AcceptedNames = {"maincpu.bin"};
    req.Archive = "aburner2";  // no folder/zip of this name exists
    req.Required = true;

    auto result = resolver.Resolve({req});
    Check(!result.Ok && result.Failures.size() == 1 && result.Failures[0].Result == CHIPESTRA_E_NOT_FOUND,
          "archive scope: a candidate outside the requested archive is never considered (CHIPESTRA_E_NOT_FOUND, "
          "not silently matched)");
}

void ResourceResolverArchiveScopeWorksWithoutHash() {
    auto root = MakeTempRomRoot("archive_scope_no_hash");

    // The motivating scenario in full: crc32 omitted (e.g. a
    // sound-quality-improved replacement ROM whose hash legitimately differs
    // from the catalog) *and* multiple revisions share a ROM Root - archive
    // scoping alone must be enough to disambiguate.
    WriteFile(root / "revA" / "pcm1.bin", std::vector<std::uint8_t>(64, 0xAA));
    WriteFile(root / "revB" / "pcm1.bin", std::vector<std::uint8_t>(64, 0xBB));

    ResourceResolver resolver(root);
    RequiredResource req;
    req.LogicalName = "pcm1";
    req.AcceptedNames = {"pcm1.bin"};
    req.Archive = "revB";
    req.Required = true;
    // Crc32/Sha1 deliberately left unset - this is the whole point (archive +
    // omitted crc32 must not be ambiguous).

    auto result = resolver.Resolve({req});
    Check(result.Ok, "archive scope + no hash: resolves via archive scope alone when crc32/sha1 are both omitted");
    Check(BytesEqual(result.Resources.Find("pcm1"), std::vector<std::uint8_t>(64, 0xBB)),
          "archive scope + no hash: the resolved bytes come from the scoped 'revB' folder");
}

void ResourceResolverNoArchiveRemainsAmbiguousAcrossArchives() {
    auto root = MakeTempRomRoot("archive_scope_backward_compat");

    // Same layout as ResourceResolverArchiveScopeDisambiguatesFolders, but
    // the request carries no Archive at all - every candidate under the ROM
    // Root is considered, so two same-named files are ambiguous.
    WriteFile(root / "setA" / "maincpu.bin", {0x01});
    WriteFile(root / "setB" / "maincpu.bin", {0x02});

    ResourceResolver resolver(root);
    RequiredResource req;
    req.LogicalName = "maincpu";
    req.AcceptedNames = {"maincpu.bin"};
    req.Required = true;
    // req.Archive left as nullopt (default).

    auto result = resolver.Resolve({req});
    Check(!result.Ok && result.Failures.size() == 1 && result.Failures[0].Result == CHIPESTRA_E_AMBIGUOUS,
          "no archive scope: backward-compatible - same-named files across sets are still CHIPESTRA_E_AMBIGUOUS");
}

void ResourceResolverArchiveScopeDeterministicAcrossRepeatedResolves() {
    auto root = MakeTempRomRoot("archive_scope_determinism");
    WriteFile(root / "setA" / "maincpu.bin", {0x01, 0x02});
    WriteFile(root / "setB" / "maincpu.bin", {0x03, 0x04, 0x05});

    ResourceResolver resolver(root);
    RequiredResource req;
    req.LogicalName = "maincpu";
    req.AcceptedNames = {"maincpu.bin"};
    req.Archive = "setA";
    req.Required = true;

    auto result1 = resolver.Resolve({req});
    auto result2 = resolver.Resolve({req});

    Check(result1.Ok && result2.Ok, "archive scope determinism: both resolve attempts succeed");
    Check(BytesEqual(result1.Resources.Find("maincpu"), {0x01, 0x02}) &&
              BytesEqual(result2.Resources.Find("maincpu"), {0x01, 0x02}),
          "archive scope determinism: repeated Resolve() calls agree byte-for-byte on the scoped candidate");
}

// Two ZIPs sharing the *same* archive name ("aburner2") at two different
// paths under one broad ROM Root (one in a dedicated subfolder, one directly
// under a parent folder). Archive scoping (Candidate::ArchiveName(), matched
// by name/stem only) cannot tell two identically-named containers apart, so
// both remain candidates and CHIPESTRA_E_AMBIGUOUS is the correct, safe
// outcome. The failure detail must name the archive scope and list every
// surviving candidate's distinguishing path, so the user can find the
// duplicate.
void ResourceResolverArchiveScopeSameNameAtDifferentPathsIsAmbiguousWithPathDetail() {
    auto root = MakeTempRomRoot("archive_scope_duplicate_name");

    SyntheticZipEntry entryA;
    entryA.Name = "soundcpu.bin";
    entryA.UncompressedData = {0x01, 0x02, 0x03};
    WriteFile(root / "dedicated" / "aburner2.zip", BuildSyntheticZip({entryA}));

    SyntheticZipEntry entryB;
    entryB.Name = "soundcpu.bin";
    entryB.UncompressedData = {0x04, 0x05, 0x06};
    WriteFile(root / "aburner2.zip", BuildSyntheticZip({entryB}));  // same archive name, different path

    ResourceResolver resolver(root);
    RequiredResource req;
    req.LogicalName = "soundcpu";
    req.AcceptedNames = {"soundcpu.bin"};
    req.Archive = "aburner2";
    req.Required = true;

    auto result = resolver.Resolve({req});
    Check(!result.Ok, "archive scope, duplicate name: still refuses to auto-select (never silently picks one)");
    Check(result.Failures.size() == 1 && result.Failures[0].Result == CHIPESTRA_E_AMBIGUOUS,
          "archive scope, duplicate name: CHIPESTRA_E_AMBIGUOUS, not a false NOT_FOUND/OK");

    const std::string& detail = result.Failures[0].Detail;
    Check(detail.find("aburner2") != std::string::npos,
          "archive scope, duplicate name: failure detail names the archive scope that was applied");
    Check(detail.find("dedicated") != std::string::npos && detail.find(".zip") != std::string::npos,
          "archive scope, duplicate name: failure detail lists a distinguishing path for the candidates");
}
