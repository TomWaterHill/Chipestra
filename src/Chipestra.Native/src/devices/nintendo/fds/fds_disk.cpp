// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill

#include "fds_disk.h"

#include "resource_hash.h"

#include <cstdio>
#include <stdexcept>

namespace {

constexpr std::size_t kSideSize = 65500;
constexpr std::size_t kHeaderSize = 16;
constexpr std::size_t kDiskInfoBlockSize = 56;
constexpr std::size_t kFileHeaderBlockSize = 16;

[[noreturn]] void Fail(const char* what, unsigned side, std::size_t offset) {
    char message[160];
    std::snprintf(message, sizeof(message), "FDS disk image: %s (side %u, offset %zu)", what, side + 1, offset);
    throw std::runtime_error(message);
}

}  // namespace

FdsDisk ParseFdsDisk(const std::uint8_t* data, std::size_t size) {
    std::size_t offset = 0;
    std::size_t declaredSides = 0;
    if (size >= kHeaderSize && data[0] == 'F' && data[1] == 'D' && data[2] == 'S' && data[3] == 0x1A) {
        offset = kHeaderSize;
        declaredSides = data[4];
    }
    const std::size_t body = size - offset;
    if (body == 0 || body % kSideSize != 0)
        throw std::runtime_error("FDS disk image: the size is not a whole number of 65500-byte disk sides");
    const std::size_t sides = body / kSideSize;
    if (declaredSides != 0 && declaredSides != sides)
        throw std::runtime_error("FDS disk image: the header announces a different number of sides than the file holds");

    FdsDisk disk;
    disk.sideCount = static_cast<std::uint32_t>(sides);
    for (std::size_t s = 0; s < sides; ++s) {
        const std::uint8_t* side = data + offset + s * kSideSize;
        const unsigned sideNumber = static_cast<unsigned>(s);
        if (side[0] != 0x01 || std::string(reinterpret_cast<const char*>(side + 1), 14) != "*NINTENDO-HVC*")
            Fail("block 1 (disk information) is missing", sideNumber, 0);
        std::size_t pos = kDiskInfoBlockSize;
        if (pos + 2 > kSideSize || side[pos] != 0x02) Fail("block 2 (file count) is missing", sideNumber, pos);
        const std::size_t fileCount = side[pos + 1];
        pos += 2;
        for (std::size_t f = 0; f < fileCount; ++f) {
            if (pos + kFileHeaderBlockSize > kSideSize || side[pos] != 0x03) Fail("block 3 (file header) is missing", sideNumber, pos);
            FdsFile file;
            file.side = static_cast<std::uint8_t>(s);
            file.number = side[pos + 1];
            file.id = side[pos + 2];
            for (std::size_t i = 0; i < 8; ++i) file.name.push_back(static_cast<char>(side[pos + 3 + i]));
            while (!file.name.empty() && (file.name.back() == '\0' || file.name.back() == ' ')) file.name.pop_back();
            file.loadAddress = static_cast<std::uint16_t>(side[pos + 11] | (side[pos + 12] << 8));
            const std::size_t length = static_cast<std::size_t>(side[pos + 13] | (side[pos + 14] << 8));
            file.kind = side[pos + 15];
            pos += kFileHeaderBlockSize;
            if (pos + 1 + length > kSideSize || side[pos] != 0x04) Fail("block 4 (file data) is missing or runs past the side", sideNumber, pos);
            file.data.assign(side + pos + 1, side + pos + 1 + length);
            file.crc32 = Crc32(file.data.data(), file.data.size());
            pos += 1 + length;
            disk.files.push_back(std::move(file));
        }
    }
    return disk;
}
