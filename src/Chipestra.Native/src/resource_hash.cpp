// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "resource_hash.h"

#include <array>
#include <stdexcept>

std::uint32_t Crc32(const std::uint8_t* data, std::size_t size) {
    static const std::array<std::uint32_t, 256> crc_table = []() {
        std::array<std::uint32_t, 256> table{};
        constexpr std::uint32_t polynomial = 0xEDB88320;
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t crc = i;
            for (int j = 0; j < 8; ++j) {
                if (crc & 1) {
                    crc = (crc >> 1) ^ polynomial;
                } else {
                    crc >>= 1;
                }
            }
            table[i] = crc;
        }
        return table;
    }();

    std::uint32_t crc = 0xFFFFFFFF;
    for (std::size_t i = 0; i < size; ++i) {
        std::uint8_t table_index = static_cast<std::uint8_t>((crc ^ data[i]) & 0xFF);
        crc = (crc >> 8) ^ crc_table[table_index];
    }
    return crc ^ 0xFFFFFFFF;
}

// Sha1Hex - via Windows CNG (BCrypt).

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <bcrypt.h>

#include <vector>

namespace {

// RAII guards so every BCrypt* handle is released on every exit path,
// including exceptions.
struct AlgHandle {
    BCRYPT_ALG_HANDLE handle = nullptr;
    ~AlgHandle() {
        if (handle != nullptr) {
            BCryptCloseAlgorithmProvider(handle, 0);
        }
    }
};

struct HashHandle {
    BCRYPT_HASH_HANDLE handle = nullptr;
    ~HashHandle() {
        if (handle != nullptr) {
            BCryptDestroyHash(handle);
        }
    }
};

void ThrowIfNotSuccess(NTSTATUS status, const char* what) {
    if (!BCRYPT_SUCCESS(status)) {
        throw std::runtime_error(what);
    }
}

}  // namespace

std::string Sha1Hex(const std::uint8_t* data, std::size_t size) {
    AlgHandle alg;
    ThrowIfNotSuccess(BCryptOpenAlgorithmProvider(&alg.handle, BCRYPT_SHA1_ALGORITHM, nullptr, 0),
                       "BCryptOpenAlgorithmProvider(SHA1) failed");

    DWORD hash_object_size = 0;
    DWORD data_written = 0;
    ThrowIfNotSuccess(BCryptGetProperty(alg.handle, BCRYPT_OBJECT_LENGTH,
                                         reinterpret_cast<PUCHAR>(&hash_object_size), sizeof(hash_object_size),
                                         &data_written, 0),
                       "BCryptGetProperty(BCRYPT_OBJECT_LENGTH) failed");

    DWORD hash_length = 0;
    ThrowIfNotSuccess(BCryptGetProperty(alg.handle, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hash_length),
                                         sizeof(hash_length), &data_written, 0),
                       "BCryptGetProperty(BCRYPT_HASH_LENGTH) failed");

    std::vector<UCHAR> hash_object(hash_object_size);
    HashHandle hash;
    ThrowIfNotSuccess(
        BCryptCreateHash(alg.handle, &hash.handle, hash_object.data(), hash_object_size, nullptr, 0, 0),
        "BCryptCreateHash failed");

    ThrowIfNotSuccess(
        BCryptHashData(hash.handle, const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(data)),
                        static_cast<ULONG>(size), 0),
        "BCryptHashData failed");

    std::vector<UCHAR> digest(hash_length);
    ThrowIfNotSuccess(BCryptFinishHash(hash.handle, digest.data(), hash_length, 0), "BCryptFinishHash failed");

    static const char kHexDigits[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(static_cast<std::size_t>(hash_length) * 2);
    for (UCHAR byte : digest) {
        hex.push_back(kHexDigits[byte >> 4]);
        hex.push_back(kHexDigits[byte & 0x0F]);
    }
    return hex;
}
