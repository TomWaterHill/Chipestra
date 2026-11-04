// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// Reader for the recordings of the original emulator's Mega Drive sound (CHIPESTRA_REFERENCE_DIR/md/<game>/):
//   <name>_writes.txt  "<Z80 cycle> YM <port> <value>", "<Z80 cycle> PSG <value>", "<Z80 cycle> RESET <0|1>",
//                      and a last line "# samples ym=<n> psg=<n> startidx ym=<i> psg=<i>"
//   <name>_ym.f32      the YM2612 stream from sample startidx on (left, right interleaved floats)
//   <name>_psg.f32     the PSG stream from sample startidx on (one float per sample)
// The cycle is the emulated time of the write in Z80 cycles (3579545 Hz) from power-on.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

struct MdWrite {
    enum Kind { Ym, Psg, Reset } kind = Ym;
    std::uint64_t cycle = 0;
    std::uint32_t port = 0;
    std::uint8_t value = 0;
};

struct MdReferenceLog {
    std::vector<MdWrite> writes;
    std::uint64_t ym_start = 0;
    std::uint64_t psg_start = 0;
    std::vector<float> ym;   // left, right interleaved
    std::vector<float> psg;
};

inline std::filesystem::path MdReferenceDir(const char* subdirectory, const char* game = "spaceharrier2") {
    const char* root = nullptr;
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    root = std::getenv("CHIPESTRA_REFERENCE_DIR");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    if (root == nullptr || root[0] == '\0') return {};
    return std::filesystem::path(root) / "md" / game / subdirectory;
}

// The names (without "_writes.txt") of the stream recordings. False when CHIPESTRA_REFERENCE_DIR is not set.
inline bool MdReferenceNames(std::vector<std::string>& names) {
    const std::filesystem::path dir = MdReferenceDir("streams");
    if (dir.empty()) return false;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        const std::string file = entry.path().filename().string();
        const std::string suffix = "_writes.txt";
        if (file.size() > suffix.size() && file.compare(file.size() - suffix.size(), suffix.size(), suffix) == 0) {
            names.push_back(file.substr(0, file.size() - suffix.size()));
        }
    }
    std::sort(names.begin(), names.end());
    return true;
}

inline bool MdReadFloats(const std::filesystem::path& path, std::vector<float>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    in.seekg(0);
    out.resize(static_cast<std::size_t>(size) / sizeof(float));
    in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size() * sizeof(float)));
    return true;
}

inline bool MdReadReferenceLog(const std::string& name, MdReferenceLog& log) {
    const std::filesystem::path dir = MdReferenceDir("streams");
    std::ifstream in(dir / (name + "_writes.txt"));
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        if (line[0] == '#') {
            const std::size_t pos = line.find("startidx");
            if (pos != std::string::npos) {
                const std::size_t y = line.find("ym=", pos);
                const std::size_t p = line.find("psg=", pos);
                if (y != std::string::npos && p != std::string::npos) {
                    log.ym_start = std::stoull(line.substr(y + 3));
                    log.psg_start = std::stoull(line.substr(p + 4));
                }
            }
            continue;
        }
        std::istringstream fields(line);
        unsigned long long cycle = 0;
        std::string kind;
        fields >> cycle >> kind;
        MdWrite w;
        w.cycle = cycle;
        unsigned a = 0, b = 0;
        if (kind == "YM") {
            fields >> a >> b;
            w.kind = MdWrite::Ym;
            w.port = a;
            w.value = static_cast<std::uint8_t>(b);
        } else if (kind == "PSG") {
            fields >> a;
            w.kind = MdWrite::Psg;
            w.value = static_cast<std::uint8_t>(a);
        } else if (kind == "RESET") {
            fields >> a;
            w.kind = MdWrite::Reset;
            w.value = static_cast<std::uint8_t>(a);
        } else {
            continue;
        }
        if (!fields.fail()) log.writes.push_back(w);
    }
    return MdReadFloats(dir / (name + "_ym.f32"), log.ym) && MdReadFloats(dir / (name + "_psg.f32"), log.psg);
}
