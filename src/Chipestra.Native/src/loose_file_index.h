// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// LooseFileCandidate - one regular file found under a ROM Root
// during LooseFileIndex's recursive scan.
struct LooseFileCandidate {
    std::filesystem::path AbsolutePath;  // internal to the Resolver only; never exposed on a MachineDefinition
    std::string FileName;                // basename, used to match against RequiredResource::AcceptedNames
    std::uint64_t SizeBytes;
};

// LooseFileIndex - a recursive, deterministically-ordered snapshot
// of every regular file under a ROM Root, built once at construction time.
// Machine/Game definitions never see or hold these absolute paths - only
// ResourceResolver does.
class LooseFileIndex {
public:
    explicit LooseFileIndex(const std::filesystem::path& rootDirectory);

    const std::vector<LooseFileCandidate>& Entries() const;

private:
    std::vector<LooseFileCandidate> entries_;
};
