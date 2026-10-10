// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "loose_file_index.h"

#include <algorithm>

LooseFileIndex::LooseFileIndex(const std::filesystem::path& rootDirectory) {
    if (!std::filesystem::exists(rootDirectory)) {
        return;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(rootDirectory)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        LooseFileCandidate candidate;
        candidate.AbsolutePath = entry.path();
        candidate.FileName = entry.path().filename().string();
        candidate.SizeBytes = static_cast<std::uint64_t>(entry.file_size());
        entries_.push_back(std::move(candidate));
    }

    // Deterministic order regardless of filesystem iteration order, so repeated
    // resolves against the same ROM Root layout agree.
    std::sort(entries_.begin(), entries_.end(), [](const LooseFileCandidate& a, const LooseFileCandidate& b) {
        return a.AbsolutePath < b.AbsolutePath;
    });
}

const std::vector<LooseFileCandidate>& LooseFileIndex::Entries() const {
    return entries_;
}
