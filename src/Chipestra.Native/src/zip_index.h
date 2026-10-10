// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// ZipEntryInfo - one file entry from a ZIP's central directory.
// Only entries that pass IsSafeZipEntryName() (declared in zip_index.cpp)
// ever become a ZipEntryInfo - path-traversal/absolute/drive-prefixed
// names are rejected during indexing, not later.
struct ZipEntryInfo {
    std::string Name;      // full internal ZIP path, e.g. "sub/dir/file.bin" - already validated safe
    std::string BaseName;  // just the filename portion, for matching against RequiredResource::AcceptedNames
    std::uint64_t UncompressedSize = 0;
    std::uint64_t CompressedSize = 0;
    std::uint32_t Crc32 = 0;               // as declared in the ZIP's central directory (informational only -
                                            // ResourceResolver verifies against RequiredResource's *expected*
                                            // hash by re-hashing the actual decompressed bytes, not this field)
    std::uint16_t CompressionMethod = 0;   // 0 = Stored, 8 = Deflate; anything else is rejected by ReadEntry()
    std::uint32_t LocalHeaderOffset = 0;
};

// ZipIndex - parses one ZIP file's central directory into a
// deterministically-ordered list of ZipEntryInfo, without decompressing
// anything yet. Every offset/length read from the archive is bounds-checked
// against the file's actual size before use; a malformed/corrupt ZIP throws
// std::runtime_error out of the constructor rather than producing a
// partial or out-of-bounds index.
class ZipIndex {
public:
    explicit ZipIndex(std::filesystem::path zipPath);

    const std::filesystem::path& ZipPath() const;
    const std::vector<ZipEntryInfo>& Entries() const;

    // Reads and (for CompressionMethod == 8) decompresses `entry`'s data,
    // never producing more than `maxUncompressedBytes` of output (ZIP
    // bomb protection): the check is both a cheap upfront rejection (entry's
    // declared UncompressedSize alone) and enforced by the decompressor
    // itself via a fixed-size output buffer, so a dishonest declared size
    // can't bypass the cap either.
    //
    // Throws std::runtime_error if CompressionMethod is anything other than
    // 0 (Stored) or 8 (Deflate), if the declared UncompressedSize exceeds
    // maxUncompressedBytes, or if reading/decompressing otherwise fails
    // (truncated file, corrupt deflate stream, local-header mismatch).
    std::vector<std::uint8_t> ReadEntry(const ZipEntryInfo& entry, std::uint64_t maxUncompressedBytes) const;

private:
    std::filesystem::path zip_path_;
    std::vector<ZipEntryInfo> entries_;
};
