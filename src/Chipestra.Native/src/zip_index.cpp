// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "zip_index.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>

#include "miniz/miniz_tinfl.h"

namespace {

std::uint16_t ReadUint16LE(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

std::uint32_t ReadUint32LE(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

// IsSafeZipEntryName - rejects path traversal ("../" segments),
// absolute paths (leading '/' or '\\'), and drive-letter prefixes ("C:...").
// Entry names failing this check are dropped from the index entirely: they
// can never become resolvable candidates.
bool IsSafeZipEntryName(const std::string& name) {
    if (name.empty()) {
        return false;
    }
    if (name.find('\0') != std::string::npos) {
        return false;
    }
    if (name.front() == '/' || name.front() == '\\') {
        return false;
    }
    if (name.size() >= 2 && name[1] == ':') {
        // Drive-letter prefix, e.g. "C:foo" or "C:/foo".
        return false;
    }

    std::size_t segmentStart = 0;
    for (std::size_t i = 0; i <= name.size(); ++i) {
        if (i == name.size() || name[i] == '/' || name[i] == '\\') {
            std::string segment = name.substr(segmentStart, i - segmentStart);
            if (segment == "..") {
                return false;
            }
            segmentStart = i + 1;
        }
    }
    return true;
}

std::string BaseNameOf(const std::string& zipEntryName) {
    std::size_t slash = zipEntryName.find_last_of("/\\");
    return slash == std::string::npos ? zipEntryName : zipEntryName.substr(slash + 1);
}

}  // namespace

ZipIndex::ZipIndex(std::filesystem::path zipPath) : zip_path_(std::move(zipPath)) {
    std::ifstream file(zip_path_, std::ios::binary);
    if (!file) {
        throw std::runtime_error("ZipIndex: cannot open file: " + zip_path_.string());
    }

    file.seekg(0, std::ios::end);
    const std::uint64_t fileSize = static_cast<std::uint64_t>(file.tellg());

    // Locate the End Of Central Directory record by scanning backward for
    // its signature. The comment field is variable-length (up to 65535
    // bytes), so the signature can appear anywhere in that trailing window.
    constexpr std::uint32_t kEocdSignature = 0x06054b50;
    constexpr std::uint64_t kEocdMinSize = 22;
    constexpr std::uint64_t kMaxCommentSize = 65535;

    const std::uint64_t searchStart =
        fileSize > (kEocdMinSize + kMaxCommentSize) ? fileSize - (kEocdMinSize + kMaxCommentSize) : 0;
    const std::uint64_t searchSize = fileSize - searchStart;

    std::vector<std::uint8_t> tail(searchSize);
    file.seekg(static_cast<std::streamoff>(searchStart));
    file.read(reinterpret_cast<char*>(tail.data()), static_cast<std::streamsize>(searchSize));
    if (!file) {
        throw std::runtime_error("ZipIndex: failed to read file tail: " + zip_path_.string());
    }

    std::int64_t eocdOffset = -1;
    for (std::int64_t i = static_cast<std::int64_t>(searchSize) - static_cast<std::int64_t>(kEocdMinSize); i >= 0;
         --i) {
        if (ReadUint32LE(tail.data() + i) == kEocdSignature) {
            eocdOffset = i;
            break;
        }
    }
    if (eocdOffset < 0) {
        throw std::runtime_error("ZipIndex: not a valid ZIP file (EOCD not found): " + zip_path_.string());
    }

    const std::uint8_t* eocd = tail.data() + eocdOffset;
    const std::uint16_t totalEntries = ReadUint16LE(eocd + 10);
    const std::uint32_t centralDirSize = ReadUint32LE(eocd + 12);
    const std::uint32_t centralDirOffset = ReadUint32LE(eocd + 16);

    if (static_cast<std::uint64_t>(centralDirOffset) + centralDirSize > fileSize) {
        throw std::runtime_error("ZipIndex: central directory bounds exceed file size: " + zip_path_.string());
    }

    std::vector<std::uint8_t> centralDir(centralDirSize);
    file.seekg(static_cast<std::streamoff>(centralDirOffset));
    file.read(reinterpret_cast<char*>(centralDir.data()), static_cast<std::streamsize>(centralDirSize));
    if (!file) {
        throw std::runtime_error("ZipIndex: failed to read central directory: " + zip_path_.string());
    }

    constexpr std::uint32_t kCentralDirSignature = 0x02014b50;
    constexpr std::size_t kCentralDirRecordFixedSize = 46;

    std::size_t pos = 0;
    for (std::uint16_t i = 0; i < totalEntries; ++i) {
        if (pos + kCentralDirRecordFixedSize > centralDir.size()) {
            throw std::runtime_error("ZipIndex: truncated central directory record: " + zip_path_.string());
        }
        const std::uint8_t* rec = centralDir.data() + pos;
        if (ReadUint32LE(rec) != kCentralDirSignature) {
            throw std::runtime_error("ZipIndex: malformed central directory (bad signature): " + zip_path_.string());
        }

        const std::uint16_t compressionMethod = ReadUint16LE(rec + 10);
        const std::uint32_t crc32 = ReadUint32LE(rec + 16);
        const std::uint32_t compressedSize = ReadUint32LE(rec + 20);
        const std::uint32_t uncompressedSize = ReadUint32LE(rec + 24);
        const std::uint16_t nameLen = ReadUint16LE(rec + 28);
        const std::uint16_t extraLen = ReadUint16LE(rec + 30);
        const std::uint16_t commentLen = ReadUint16LE(rec + 32);
        const std::uint32_t localHeaderOffset = ReadUint32LE(rec + 42);

        const std::size_t nameStart = pos + kCentralDirRecordFixedSize;
        if (nameStart + nameLen > centralDir.size()) {
            throw std::runtime_error("ZipIndex: truncated filename in central directory: " + zip_path_.string());
        }
        std::string name(reinterpret_cast<const char*>(centralDir.data() + nameStart), nameLen);

        pos = nameStart + static_cast<std::size_t>(nameLen) + extraLen + commentLen;

        if (static_cast<std::uint64_t>(localHeaderOffset) >= fileSize) {
            throw std::runtime_error("ZipIndex: local header offset out of bounds: " + zip_path_.string());
        }

        if (!name.empty() && name.back() != '/' && IsSafeZipEntryName(name)) {
            ZipEntryInfo entryInfo;
            entryInfo.Name = name;
            entryInfo.BaseName = BaseNameOf(name);
            entryInfo.UncompressedSize = uncompressedSize;
            entryInfo.CompressedSize = compressedSize;
            entryInfo.Crc32 = crc32;
            entryInfo.CompressionMethod = compressionMethod;
            entryInfo.LocalHeaderOffset = localHeaderOffset;
            entries_.push_back(std::move(entryInfo));
        }
        // Directory entries (trailing '/') and unsafe names (path traversal,
        // absolute, drive-prefixed) are silently excluded from the index -
        // they can never be resolved as a candidate.
    }

    // Deterministic order regardless of the archive's own directory order.
    std::sort(entries_.begin(), entries_.end(),
              [](const ZipEntryInfo& a, const ZipEntryInfo& b) { return a.Name < b.Name; });
}

const std::filesystem::path& ZipIndex::ZipPath() const {
    return zip_path_;
}

const std::vector<ZipEntryInfo>& ZipIndex::Entries() const {
    return entries_;
}

std::vector<std::uint8_t> ZipIndex::ReadEntry(const ZipEntryInfo& entry, std::uint64_t maxUncompressedBytes) const {
    if (entry.CompressionMethod != 0 && entry.CompressionMethod != 8) {
        throw std::runtime_error("ZipIndex::ReadEntry: unsupported compression method " +
                                  std::to_string(entry.CompressionMethod) + " for entry " + entry.Name);
    }
    if (entry.UncompressedSize > maxUncompressedBytes) {
        throw std::runtime_error("ZipIndex::ReadEntry: entry " + entry.Name + " declared size " +
                                  std::to_string(entry.UncompressedSize) + " exceeds limit " +
                                  std::to_string(maxUncompressedBytes));
    }

    std::ifstream file(zip_path_, std::ios::binary);
    if (!file) {
        throw std::runtime_error("ZipIndex::ReadEntry: cannot open file: " + zip_path_.string());
    }

    file.seekg(static_cast<std::streamoff>(entry.LocalHeaderOffset));
    std::uint8_t localHeader[30];
    file.read(reinterpret_cast<char*>(localHeader), sizeof(localHeader));
    if (!file || ReadUint32LE(localHeader) != 0x04034b50) {
        throw std::runtime_error("ZipIndex::ReadEntry: invalid local file header for entry " + entry.Name);
    }
    const std::uint16_t nameLen = ReadUint16LE(localHeader + 26);
    const std::uint16_t extraLen = ReadUint16LE(localHeader + 28);

    const std::uint64_t dataOffset =
        entry.LocalHeaderOffset + sizeof(localHeader) + static_cast<std::uint64_t>(nameLen) + extraLen;

    file.seekg(static_cast<std::streamoff>(dataOffset));
    std::vector<std::uint8_t> compressed(static_cast<std::size_t>(entry.CompressedSize));
    if (!compressed.empty()) {
        file.read(reinterpret_cast<char*>(compressed.data()), static_cast<std::streamsize>(compressed.size()));
        if (!file) {
            throw std::runtime_error("ZipIndex::ReadEntry: truncated compressed data for entry " + entry.Name);
        }
    }

    if (entry.CompressionMethod == 0) {
        if (compressed.size() != entry.UncompressedSize) {
            throw std::runtime_error("ZipIndex::ReadEntry: stored entry size mismatch for entry " + entry.Name);
        }
        return compressed;
    }

    // Deflate (raw, no zlib wrapper - ZIP entries never carry one). The
    // output buffer is pre-sized to the (capped) expected decompressed
    // size; TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF tells tinfl the whole
    // output fits in one buffer, and tinfl itself refuses to write past the
    // buffer's end (TINFL_DECOMPRESS_MEM_TO_MEM_FAILED) - so a dishonest
    // declared size can't be used to overflow the allocation.
    const std::size_t outCap =
        static_cast<std::size_t>(std::min<std::uint64_t>(entry.UncompressedSize, maxUncompressedBytes));
    std::vector<std::uint8_t> out(outCap);
    const std::size_t actualOut =
        tinfl_decompress_mem_to_mem(out.empty() ? nullptr : out.data(), out.size(),
                                     compressed.empty() ? nullptr : compressed.data(), compressed.size(),
                                     TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    if (actualOut == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED) {
        throw std::runtime_error("ZipIndex::ReadEntry: decompression failed (corrupt data or exceeds limit) for "
                                  "entry " +
                                  entry.Name);
    }

    out.resize(actualOut);
    return out;
}
