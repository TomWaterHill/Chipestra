// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Crc32 - standard IEEE 802.3 CRC32 (the ZIP/PNG/gzip
// variant: polynomial 0xEDB88320, init/final XOR 0xFFFFFFFF).
std::uint32_t Crc32(const std::uint8_t* data, std::size_t size);

// Sha1Hex - SHA-1 digest of `data`, formatted as 40
// lowercase hex characters (matching RequiredResource::Sha1's format).
// Uses Windows CNG (BCrypt) rather than a hand-rolled implementation.
std::string Sha1Hex(const std::uint8_t* data, std::size_t size);
