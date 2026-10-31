// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// Finds a game definition file of the repository's xml folder by its file name, wherever it sits below
// that folder. Shared by the native tests and the diagnostic tools.

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// Looks for a folder named "xml" in the current directory and then in up to 8 parents (tools\verify.ps1
// runs from the repository root, other runs may start deeper), and returns the first file called
// `fileName` below it (paths compared in order, so the answer does not depend on the file system).
inline std::optional<std::filesystem::path> FindDefinitionXml(const std::filesystem::path& fileName) {
    std::filesystem::path dir = std::filesystem::current_path();
    for (int depth = 0; depth < 8; ++depth) {
        std::error_code ec;
        const std::filesystem::path xmlFolder = dir / "xml";
        if (std::filesystem::is_directory(xmlFolder, ec)) {
            std::vector<std::filesystem::path> found;
            std::filesystem::recursive_directory_iterator it(xmlFolder, std::filesystem::directory_options::skip_permission_denied, ec);
            for (; !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
                if (it->is_regular_file(ec) && it->path().filename() == fileName.filename()) found.push_back(it->path());
            }
            if (!found.empty()) {
                std::sort(found.begin(), found.end());
                return found.front();
            }
        }
        if (!dir.has_parent_path() || dir.parent_path() == dir) break;
        dir = dir.parent_path();
    }
    return std::nullopt;
}
