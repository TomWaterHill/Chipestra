// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#include "resource_resolver.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <optional>

#include "resource_hash.h"

// ---------------------------------------------------------------------
// ResolvedResource / MachineResources
// ---------------------------------------------------------------------

ResolvedResource::ResolvedResource(std::string logicalName, std::vector<std::uint8_t> data)
    : logical_name_(std::move(logicalName)), data_(std::move(data)) {}

const std::string& ResolvedResource::LogicalName() const {
    return logical_name_;
}

const std::uint8_t* ResolvedResource::Data() const {
    return data_.data();
}

std::size_t ResolvedResource::Size() const {
    return data_.size();
}

void MachineResources::Add(ResolvedResource resource) {
    resources_.push_back(std::move(resource));
}

const ResolvedResource* MachineResources::Find(const std::string& logicalName) const {
    for (const auto& resource : resources_) {
        if (resource.LogicalName() == logicalName) {
            return &resource;
        }
    }
    return nullptr;
}

std::size_t MachineResources::Count() const {
    return resources_.size();
}

const ResolvedResource& MachineResources::At(std::size_t index) const {
    return resources_.at(index);
}

// ---------------------------------------------------------------------
// ResourceResolver
// ---------------------------------------------------------------------

namespace {

bool EqualsIgnoreCase(const std::string& a, const std::string& b) {
    return a.size() == b.size() && _stricmp(a.c_str(), b.c_str()) == 0;
}

// One name+size candidate, from either a loose file or a ZIP entry. Never
// outlives the ResourceResolver whose indexes it points into.
struct Candidate {
    enum class Kind { Loose, Zip };

    Kind kind;
    const LooseFileCandidate* looseFile = nullptr;
    const ZipIndex* zipIndex = nullptr;
    const ZipEntryInfo* zipEntry = nullptr;

    const std::string& BaseName() const {
        return kind == Kind::Loose ? looseFile->FileName : zipEntry->BaseName;
    }

    std::uint64_t DeclaredSize() const {
        return kind == Kind::Loose ? looseFile->SizeBytes : zipEntry->UncompressedSize;
    }

    // ArchiveName - the "archive" this candidate belongs to: a ZIP's own name is its
    // filename stem; a loose file's is its immediate containing directory's name (one
    // subfolder per ROM set under the ROM Root). A loose file directly at the top level
    // simply won't match any specific archive scope.
    std::string ArchiveName() const {
        return kind == Kind::Loose ? looseFile->AbsolutePath.parent_path().filename().string()
                                    : zipIndex->ZipPath().stem().string();
    }

    // A human-readable identifier for this exact candidate, distinct even when two candidates
    // share both a filename and an ArchiveName() (e.g. two same-named ZIPs at different paths
    // under the ROM Root). Local path, used only in runtime error messages.
    std::string DisplayPath() const {
        return kind == Kind::Loose ? looseFile->AbsolutePath.string()
                                    : zipIndex->ZipPath().string() + "!" + zipEntry->Name;
    }
};

bool NameMatches(const std::string& candidateName, const std::vector<std::string>& acceptedNames) {
    for (const auto& accepted : acceptedNames) {
        if (EqualsIgnoreCase(candidateName, accepted)) {
            return true;
        }
    }
    return false;
}

// Reads a candidate's full bytes, bounded by maxBytes. Returns std::nullopt
// (with *outError set) if the candidate can't be read at all - a loose file
// larger than maxBytes, an unsupported ZIP compression method, a ZIP entry
// that would exceed maxBytes once decompressed, or any I/O/corruption
// failure. Never throws.
std::optional<std::vector<std::uint8_t>> TryReadCandidateBytes(const Candidate& candidate, std::uint64_t maxBytes,
                                                                 std::string* outError) {
    try {
        if (candidate.kind == Candidate::Kind::Loose) {
            if (candidate.looseFile->SizeBytes > maxBytes) {
                if (outError) {
                    *outError = "loose file '" + candidate.looseFile->FileName + "' size " +
                                std::to_string(candidate.looseFile->SizeBytes) + " exceeds limit " +
                                std::to_string(maxBytes);
                }
                return std::nullopt;
            }
            std::ifstream file(candidate.looseFile->AbsolutePath, std::ios::binary);
            if (!file) {
                if (outError) {
                    *outError = "failed to open loose file '" + candidate.looseFile->AbsolutePath.string() + "'";
                }
                return std::nullopt;
            }
            std::vector<std::uint8_t> data(static_cast<std::size_t>(candidate.looseFile->SizeBytes));
            if (!data.empty()) {
                file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
                if (!file) {
                    if (outError) {
                        *outError = "failed to read loose file '" + candidate.looseFile->AbsolutePath.string() + "'";
                    }
                    return std::nullopt;
                }
            }
            return data;
        }

        return candidate.zipIndex->ReadEntry(*candidate.zipEntry, maxBytes);
    } catch (const std::exception& e) {
        if (outError) {
            *outError = e.what();
        }
        return std::nullopt;
    }
}

struct SingleResolution {
    ChipestraResult Result = CHIPESTRA_E_NOT_FOUND;
    std::optional<ResolvedResource> Resource;
    std::string Detail;
};

SingleResolution ResolveOne(const RequiredResource& req, const std::vector<Candidate>& allCandidates,
                             std::uint64_t maxBytes) {
    // Step 0: archive scope filter, if the Game declared one - narrows the search to one
    // ZIP/folder before the filename filter, so a same-named file in a different archive is
    // never a candidate (never triggers CHIPESTRA_E_AMBIGUOUS). No archive specified ->
    // every candidate under the ROM Root is considered.
    const std::vector<Candidate>* scopedPool = &allCandidates;
    std::vector<Candidate> archiveScoped;
    if (req.Archive.has_value()) {
        for (const auto& candidate : allCandidates) {
            if (EqualsIgnoreCase(candidate.ArchiveName(), *req.Archive)) {
                archiveScoped.push_back(candidate);
            }
        }
        scopedPool = &archiveScoped;
    }

    // Step 1: filename filter.
    std::vector<Candidate> named;
    for (const auto& candidate : *scopedPool) {
        if (NameMatches(candidate.BaseName(), req.AcceptedNames)) {
            named.push_back(candidate);
        }
    }
    if (named.empty()) {
        return {CHIPESTRA_E_NOT_FOUND, std::nullopt, "no candidate matched an accepted filename for '" + req.LogicalName + "'"};
    }

    // Step 2: size filter (hard, declared size only - no I/O needed yet).
    std::vector<Candidate> sized;
    if (req.SizeBytes.has_value()) {
        for (const auto& candidate : named) {
            if (candidate.DeclaredSize() == *req.SizeBytes) {
                sized.push_back(candidate);
            }
        }
        if (sized.empty()) {
            return {CHIPESTRA_E_NOT_FOUND,
                    std::nullopt,
                    "no candidate matched the expected size for '" + req.LogicalName + "'"};
        }
    } else {
        sized = named;
    }

    // Step 3: read bytes for every remaining candidate, dropping any that
    // can't be read (unsupported compression, exceeds cap, corrupt data).
    // Each entry carries its Candidate::DisplayPath() for the CHIPESTRA_E_AMBIGUOUS
    // message (Step 5).
    struct ReadableCandidate {
        std::vector<std::uint8_t> Data;
        std::string Path;
    };
    std::vector<ReadableCandidate> readable;
    bool anyUnreadable = false;
    for (const auto& candidate : sized) {
        std::string readError;
        auto data = TryReadCandidateBytes(candidate, maxBytes, &readError);
        if (!data.has_value()) {
            anyUnreadable = true;
            continue;
        }
        readable.push_back(ReadableCandidate{std::move(*data), candidate.DisplayPath()});
    }
    if (readable.empty()) {
        if (anyUnreadable) {
            return {CHIPESTRA_E_UNSUPPORTED,
                    std::nullopt,
                    "matching candidate(s) for '" + req.LogicalName +
                        "' could not be read (unsupported compression or exceeds the decompression limit)"};
        }
        return {CHIPESTRA_E_NOT_FOUND, std::nullopt, "no candidate matched for '" + req.LogicalName + "'"};
    }

    // Step 4: hash filter (if either Crc32 or Sha1 was specified). An
    // unspecified hash is never required.
    std::vector<ReadableCandidate> hashMatched;
    const bool hashSpecified = req.Crc32.has_value() || req.Sha1.has_value();
    if (hashSpecified) {
        for (auto& candidate : readable) {
            bool matches = true;
            if (req.Crc32.has_value()) {
                matches = matches && (Crc32(candidate.Data.data(), candidate.Data.size()) == *req.Crc32);
            }
            if (matches && req.Sha1.has_value()) {
                matches = matches && EqualsIgnoreCase(Sha1Hex(candidate.Data.data(), candidate.Data.size()), *req.Sha1);
            }
            if (matches) {
                hashMatched.push_back(std::move(candidate));
            }
        }
        if (hashMatched.empty()) {
            return {CHIPESTRA_E_HASH_MISMATCH,
                    std::nullopt,
                    "no candidate for '" + req.LogicalName + "' matched the expected hash"};
        }
    } else {
        hashMatched = std::move(readable);
    }

    // Step 5: decide (0/1/many -> NotFound/Resolved/Ambiguous).
    if (hashMatched.size() > 1) {
        // List every surviving candidate's path, and name the archive scope (if any), so a user
        // can tell genuinely different ROM sets from the same archive name appearing at two
        // paths under the ROM Root.
        std::string pathList;
        for (std::size_t i = 0; i < hashMatched.size(); ++i) {
            if (i > 0) {
                pathList += "; ";
            }
            pathList += hashMatched[i].Path;
        }
        // Deliberately doesn't claim why more than one candidate survived; the path list above
        // states the actual fact.
        std::string archiveNote = req.Archive.has_value() ? " (archive scope '" + *req.Archive + "' was applied)" : "";
        return {CHIPESTRA_E_AMBIGUOUS,
                std::nullopt,
                std::to_string(hashMatched.size()) + " candidates matched for '" + req.LogicalName + "'" +
                    archiveNote + " - refusing to auto-select: " + pathList};
    }

    SingleResolution ok;
    ok.Result = CHIPESTRA_OK;
    ok.Resource.emplace(req.LogicalName, std::move(hashMatched.front().Data));
    return ok;
}

}  // namespace

ResourceResolver::ResourceResolver(std::filesystem::path romRoot, std::uint64_t maxUncompressedBytesPerResource)
    : rom_root_(std::move(romRoot)),
      max_uncompressed_bytes_per_resource_(maxUncompressedBytesPerResource),
      loose_index_(rom_root_) {
    if (!std::filesystem::exists(rom_root_)) {
        return;
    }

    std::vector<std::filesystem::path> zipPaths;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(rom_root_)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        std::string extension = entry.path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension == ".zip") {
            zipPaths.push_back(entry.path());
        }
    }
    // Deterministic indexing order, independent of filesystem iteration order.
    std::sort(zipPaths.begin(), zipPaths.end());

    for (const auto& zipPath : zipPaths) {
        try {
            zip_indexes_.emplace_back(zipPath);
        } catch (const std::exception&) {
            // A corrupt/unparseable ZIP under the ROM Root shouldn't take
            // down resolution of every other resource - skip it.
            continue;
        }
    }
}

ResourceResolutionResult ResourceResolver::Resolve(const std::vector<RequiredResource>& resources) const {
    std::vector<Candidate> allCandidates;
    for (const auto& looseFile : loose_index_.Entries()) {
        Candidate candidate;
        candidate.kind = Candidate::Kind::Loose;
        candidate.looseFile = &looseFile;
        allCandidates.push_back(candidate);
    }
    for (const auto& zipIndex : zip_indexes_) {
        for (const auto& entry : zipIndex.Entries()) {
            Candidate candidate;
            candidate.kind = Candidate::Kind::Zip;
            candidate.zipIndex = &zipIndex;
            candidate.zipEntry = &entry;
            allCandidates.push_back(candidate);
        }
    }

    ResourceResolutionResult result;
    result.Ok = true;

    for (const auto& req : resources) {
        SingleResolution single = ResolveOne(req, allCandidates, max_uncompressed_bytes_per_resource_);
        if (single.Result == CHIPESTRA_OK) {
            result.Resources.Add(std::move(*single.Resource));
            continue;
        }

        ResourceResolutionFailure failure;
        failure.LogicalName = req.LogicalName;
        failure.Result = single.Result;
        failure.Detail = single.Detail;
        result.Failures.push_back(std::move(failure));
        if (req.Required) {
            result.Ok = false;
        }
    }

    return result;
}
