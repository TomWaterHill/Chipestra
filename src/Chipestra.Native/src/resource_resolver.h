// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "loose_file_index.h"
#include "machine_registry.h"
#include "chipestra_abi.h"
#include "zip_index.h"

// ResolvedResource - one successfully-resolved resource: its Machine-internal logical
// name plus an owned, immutable copy of its bytes.
class ResolvedResource {
public:
    ResolvedResource(std::string logicalName, std::vector<std::uint8_t> data);

    const std::string& LogicalName() const;
    const std::uint8_t* Data() const;
    std::size_t Size() const;

private:
    std::string logical_name_;
    std::vector<std::uint8_t> data_;
};

// MachineResources - the set of resources resolved for one Machine instance, looked up
// by logical name.
class MachineResources {
public:
    void Add(ResolvedResource resource);
    const ResolvedResource* Find(const std::string& logicalName) const;
    std::size_t Count() const;

    // Indexed access, in Add() call order (which ResourceResolver::Resolve() populates in
    // the same order as its `resources` request list, skipping failures). Lets
    // chipestra_resource_resolution_get_resolved enumerate entries without a logical name.
    // Throws std::out_of_range if index >= Count().
    const ResolvedResource& At(std::size_t index) const;

private:
    std::vector<ResolvedResource> resources_;
};

// ResourceResolutionFailure - why one RequiredResource failed to resolve.
// Result is one of CHIPESTRA_E_NOT_FOUND / CHIPESTRA_E_AMBIGUOUS / CHIPESTRA_E_HASH_MISMATCH /
// CHIPESTRA_E_UNSUPPORTED; Detail is a human-readable diagnostic string,
// not part of the stable ABI.
struct ResourceResolutionFailure {
    std::string LogicalName;
    ChipestraResult Result = CHIPESTRA_E_NOT_FOUND;
    std::string Detail;
};

// ResourceResolutionResult - overall outcome of ResourceResolver::Resolve().
// Ok is true iff every RequiredResource with Required==true resolved
// successfully; a failed *optional* resource (Required==false) is recorded
// in Failures for diagnostics but does not clear Ok, matching
// RequiredResource::Required's contract ("Machine can run without this
// resource present").
struct ResourceResolutionResult {
    bool Ok = false;
    MachineResources Resources;
    std::vector<ResourceResolutionFailure> Failures;
};

// ResourceResolver - resolves a Machine's RequiredResource
// list against a ROM Root directory: loose files (recursive) and ZIP
// archives found under it (Stored/Deflate entries only). Candidates are
// narrowed by AcceptedNames, then SizeBytes (if specified), then Crc32/Sha1
// (if specified, requires reading/decompressing the remaining candidates).
// Zero candidates -> CHIPESTRA_E_NOT_FOUND; more than one surviving candidate is
// never auto-selected -> CHIPESTRA_E_AMBIGUOUS; a candidate that can't be read
// (unsupported ZIP compression, or would exceed the decompression cap) is
// dropped from consideration and, if it was the only match, reported as
// CHIPESTRA_E_UNSUPPORTED.
//
// Indexes (loose files + every ZIP directly under the ROM Root) are built
// once at construction time, so repeated Resolve() calls against an
// unchanged ROM Root layout are deterministic.
class ResourceResolver {
public:
    static constexpr std::uint64_t kDefaultMaxUncompressedBytesPerResource = 64ull * 1024 * 1024;

    explicit ResourceResolver(std::filesystem::path romRoot,
                               std::uint64_t maxUncompressedBytesPerResource = kDefaultMaxUncompressedBytesPerResource);

    ResourceResolutionResult Resolve(const std::vector<RequiredResource>& resources) const;

private:
    std::filesystem::path rom_root_;
    std::uint64_t max_uncompressed_bytes_per_resource_;
    LooseFileIndex loose_index_;
    std::vector<ZipIndex> zip_indexes_;
};
