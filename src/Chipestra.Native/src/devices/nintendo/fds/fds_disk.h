// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// A Famicom Disk System disk image as far as the sound is concerned: the files of its sides, with the
// address each is loaded to. Both forms of the image are read: the plain one (a whole number of 65500-byte
// sides) and the one with the 16-byte "FDS" 1A header in front.
//
// A side is a chain of blocks: block 1 (disk information, 56 bytes), block 2 (the number of files, 2 bytes),
// then for each file a block 3 (file header, 16 bytes: file number, file id, an 8-character name, load
// address, size, kind) and a block 4 (one byte, then the file's data). Every block starts with its own
// block number; the CRC bytes the disk adds after blocks are not part of the image.
struct FdsFile {
    std::uint8_t side = 0;    // 0-based
    std::uint8_t number = 0;  // file number (order on the side)
    std::uint8_t id = 0;      // file id (the game's key for loading)
    std::string name;         // up to 8 characters; trailing NUL/space padding removed (may be empty)
    std::uint16_t loadAddress = 0;
    std::uint8_t kind = 0;    // 0 = program data (loaded to the CPU's RAM), 1 = character data, 2 = name table data
    std::vector<std::uint8_t> data;
    std::uint32_t crc32 = 0;  // of `data`
};

struct FdsDisk {
    std::uint32_t sideCount = 0;
    std::vector<FdsFile> files;  // in disk order, all sides
};

// Reads a disk image. Throws std::runtime_error naming the problem: an unknown size, a block that is not where the
// block structure says, or a file that runs past the end of its side.
FdsDisk ParseFdsDisk(const std::uint8_t* data, std::size_t size);
