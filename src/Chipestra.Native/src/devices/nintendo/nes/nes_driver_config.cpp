// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill

#include "nes_driver_config.h"

#include "pugixml/pugixml.hpp"

#include <charconv>
#include <sstream>

namespace {

// A number: decimal, or hexadecimal with a 0x prefix.
bool ParseNumber(std::string_view text, std::uint32_t& value) {
    int base = 10;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text.remove_prefix(2);
    }
    if (text.empty()) return false;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, base);
    return result.ec == std::errc() && result.ptr == text.data() + text.size();
}

bool ParseAddress(std::string_view text, std::uint16_t& address) {
    std::uint32_t v = 0;
    if (!ParseNumber(text, v) || v > 0xFFFF) return false;
    address = static_cast<std::uint16_t>(v);
    return true;
}

bool IsSeparator(char c) { return c == ' ' || c == ',' || c == '\t' || c == '\n' || c == '\r'; }

std::vector<std::string_view> SplitList(std::string_view text) {
    std::vector<std::string_view> parts;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && IsSeparator(text[i])) ++i;
        const std::size_t start = i;
        while (i < text.size() && !IsSeparator(text[i])) ++i;
        if (i > start) parts.push_back(text.substr(start, i - start));
    }
    return parts;
}

bool Fail(std::string* error, const std::string& message) {
    if (error != nullptr) *error = message;
    return false;
}

bool ParseRoutineList(const pugi::xml_node& node, const char* name, std::vector<std::uint16_t>& out, std::string* error) {
    out.clear();
    for (std::string_view part : SplitList(node.attribute(name).as_string(""))) {
        std::uint16_t address = 0;
        if (!ParseAddress(part, address))
            return Fail(error, std::string("<driver ") + name + ">: bad address \"" + std::string(part) + "\"");
        out.push_back(address);
    }
    // The generated start-up and frame code has to fit in the 256-byte area given to each.
    if (out.size() > 60) return Fail(error, std::string("<driver ") + name + ">: too many routines (at most 60)");
    return true;
}

bool ParseVariable(const pugi::xml_node& node, const char* name, std::optional<std::uint16_t>& out, std::string* error) {
    const pugi::xml_attribute attr = node.attribute(name);
    if (!attr) return true;
    std::uint16_t address = 0;
    if (!ParseAddress(attr.value(), address) || address >= 0x2000)
        return Fail(error, std::string("<driver ") + name + ">: expected a RAM address below 0x2000, got \"" + attr.value() + "\"");
    out = address;
    return true;
}

bool ParseCrc(const pugi::xml_node& node, const char* name, std::optional<std::uint32_t>& out, std::string* error) {
    const pugi::xml_attribute attr = node.attribute(name);
    if (!attr) return true;
    const std::string_view text(attr.value());
    std::uint32_t v = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), v, 16);
    if (text.empty() || text.size() > 8 || result.ec != std::errc() || result.ptr != text.data() + text.size())
        return Fail(error, std::string("<driver ") + name + ">: expected up to 8 hexadecimal digits, got \"" + std::string(text) + "\"");
    out = v;
    return true;
}

}  // namespace

// ram_set="0x0028=0x7C 0x0680=0x01"
static bool ParseRamSet(const pugi::xml_node& node, const std::string& where,
                 std::vector<std::pair<std::uint16_t, std::uint8_t>>& out, std::string* error) {
    for (std::string_view part : SplitList(node.attribute("ram_set").as_string(""))) {
        const std::size_t eq = part.find('=');
        std::uint16_t address = 0;
        std::uint32_t value = 0;
        if (eq == std::string_view::npos || !ParseAddress(part.substr(0, eq), address) || address >= 0x2000 ||
            !ParseNumber(part.substr(eq + 1), value) || value > 0xFF)
            return Fail(error, where + ": expected \"address=value\" pairs (a RAM address below 0x2000, a byte), got \"" + std::string(part) + "\"");
        out.emplace_back(address, static_cast<std::uint8_t>(value));
    }
    return true;
}

bool ParseNesDriverConfig(std::string_view driverXml, NesDriverConfig& out, std::string* error) {
    out = NesDriverConfig{};
    pugi::xml_document doc;
    const pugi::xml_parse_result parsed =
        doc.load_buffer(driverXml.data(), driverXml.size(), pugi::parse_default, pugi::encoding_utf8);
    if (!parsed) return Fail(error, std::string("<driver>: ") + parsed.description());
    const pugi::xml_node node = doc.child("driver");
    if (!node) return Fail(error, "no <driver> element");

    if (pugi::xml_attribute clear = node.attribute("ram_clear")) {
        const std::string_view text(clear.value());
        const std::size_t dash = text.find('-');
        std::uint16_t first = 0, last = 0;
        if (dash == std::string_view::npos || !ParseAddress(text.substr(0, dash), first) ||
            !ParseAddress(text.substr(dash + 1), last) || first > last || last >= 0x2000)
            return Fail(error, "<driver ram_clear>: expected \"first-last\" inside 0x0000-0x1FFF, got \"" + std::string(text) + "\"");
        out.ramClear = std::make_pair(first, last);
    }
    if (!ParseRoutineList(node, "init", out.init, error)) return false;
    if (!ParseRoutineList(node, "frame", out.frame, error)) return false;
    if (!ParseVariable(node, "bgm", out.bgmVariable, error)) return false;
    if (!ParseVariable(node, "sfx", out.sfxVariable, error)) return false;
    if (const pugi::xml_attribute call = node.attribute("request_call")) {
        std::uint16_t address = 0;
        if (!ParseAddress(call.value(), address))
            return Fail(error, std::string("<driver request_call>: bad address \"") + call.value() + "\"");
        out.requestCall = address;
    }
    if (const pugi::xml_attribute mode = node.attribute("mmc3_prg_mode")) {
        const std::string_view text(mode.value());
        if (text != "0" && text != "1")
            return Fail(error, std::string("<driver mmc3_prg_mode>: expected \"0\" or \"1\", got \"") + mode.value() + "\"");
        out.mmc3PrgMode0 = text == "0";
    }
    if (const pugi::xml_attribute irq = node.attribute("frame_irq")) {
        if (std::string_view(irq.value()) != "off")
            return Fail(error, std::string("<driver frame_irq>: expected \"off\", got \"") + irq.value() + "\"");
        out.inhibitFrameIrq = true;
    }
    if (const pugi::xml_attribute sfx = node.attribute("sfx_call")) {
        std::uint16_t address = 0;
        if (!ParseAddress(sfx.value(), address))
            return Fail(error, std::string("<driver sfx_call>: bad address \"") + sfx.value() + "\"");
        if (!out.requestCall) return Fail(error, "<driver sfx_call>: needs request_call");
        out.sfxCall = address;
    }
    if (const pugi::xml_attribute reg = node.attribute("request_register")) {
        const std::string_view text(reg.value());
        if (text != "A" && text != "X" && text != "Y")
            return Fail(error, std::string("<driver request_register>: expected \"A\", \"X\" or \"Y\", got \"") + reg.value() + "\"");
        if (!out.requestCall) return Fail(error, "<driver request_register>: needs request_call");
        out.requestRegister = text[0];
    }
    if (!ParseCrc(node, "prg_crc32", out.prgCrc32, error)) return false;
    if (!ParseCrc(node, "chr_crc32", out.chrCrc32, error)) return false;

    if (!ParseRamSet(node, "<driver ram_set>", out.ramSet, error)) return false;
    if (pugi::xml_attribute warmup = node.attribute("warmup_frames")) {
        std::uint32_t frames = 0;
        if (!ParseNumber(warmup.value(), frames) || frames < 1 || frames > 600)
            return Fail(error, std::string("<driver warmup_frames>: expected 1-600, got \"") + warmup.value() + "\"");
        out.warmupFrames = frames;
    }

    // MMC3: prg_banks_call="R6 R7" prg_banks_idle="R6 R7", both or neither.
    auto parseBanks = [&](const char* name, std::optional<NesDriverConfig::PrgBanks>& dest) {
        const pugi::xml_attribute attr = node.attribute(name);
        if (!attr) return true;
        const std::vector<std::string_view> parts = SplitList(attr.value());
        std::uint32_t values[3] = {0, 0, 0};
        bool ok = parts.size() == 2 || parts.size() == 3;
        for (std::size_t i = 0; ok && i < parts.size(); ++i) ok = ParseNumber(parts[i], values[i]) && values[i] <= 0xFF;
        if (!ok)
            return Fail(error, std::string("<driver ") + name + ">: expected two (MMC3, VRC6) or three (VRC7, Namco 163) bank numbers (0-255), got \"" + attr.value() + "\"");
        dest = NesDriverConfig::PrgBanks{static_cast<std::uint8_t>(values[0]), static_cast<std::uint8_t>(values[1]),
                                         static_cast<std::uint8_t>(values[2]), static_cast<std::uint8_t>(parts.size())};
        return true;
    };
    if (!parseBanks("prg_banks_call", out.prgBanksCall) || !parseBanks("prg_banks_idle", out.prgBanksIdle)) return false;
    if (out.prgBanksCall.has_value() != out.prgBanksIdle.has_value())
        return Fail(error, "<driver>: prg_banks_call and prg_banks_idle are required together");
    if (out.prgBanksCall && out.prgBanksCall->count != out.prgBanksIdle->count)
        return Fail(error, "<driver>: prg_banks_call and prg_banks_idle need the same number of banks");
    if (pugi::xml_attribute line = node.attribute("vrc7_register_line")) {
        const std::string_view text(line.value());
        if (text == "A3") out.vrc7RegisterLine = std::uint8_t{3};
        else if (text == "A4") out.vrc7RegisterLine = std::uint8_t{4};
        else return Fail(error, std::string("<driver vrc7_register_line>: expected \"A3\" or \"A4\", got \"") + line.value() + "\"");
    }
    if (pugi::xml_attribute counter = node.attribute("frame_counter")) {
        std::uint16_t address = 0;
        if (!ParseAddress(counter.value(), address) || address >= 0x2000)
            return Fail(error, std::string("<driver frame_counter>: expected a RAM address below 0x2000, got \"") + counter.value() + "\"");
        out.frameCounter = address;
    }

    for (const char* name : {"n163_channels", "n163_volume"}) {
        pugi::xml_attribute attr = node.attribute(name);
        if (!attr) continue;
        std::uint32_t value = 0;
        const bool channels = std::string_view(name) == "n163_channels";
        if (!ParseNumber(attr.value(), value) || (channels ? (value < 1 || value > 8) : value > 5))
            return Fail(error, std::string("<driver ") + name + ">: expected " + (channels ? "1-8" : "0-5") + ", got \"" + attr.value() + "\"");
        (channels ? out.n163Channels : out.n163Volume) = static_cast<std::uint8_t>(value);
    }

    // Famicom Disk System: <file name crc32/>, base="NAME ...", <set id files="NAME ..."/>, requests="0x0680 ...".
    for (const pugi::xml_node file : node.children("file")) {
        NesDriverConfig::DiskFile disk;
        disk.name = file.attribute("name").as_string("");
        if (disk.name.empty()) return Fail(error, "<driver><file>: the name attribute is required");
        if (!ParseCrc(file, "crc32", disk.crc32, error)) return false;
        for (const NesDriverConfig::DiskFile& other : out.diskFiles)
            if (other.name == disk.name) return Fail(error, "<driver><file>: the name \"" + disk.name + "\" appears twice");
        out.diskFiles.push_back(std::move(disk));
    }
    auto checkFileNames = [&](const std::vector<std::string>& names, const std::string& where) {
        for (const std::string& name : names) {
            bool known = false;
            for (const NesDriverConfig::DiskFile& d : out.diskFiles) known = known || d.name == name;
            if (!known) return Fail(error, where + ": \"" + name + "\" is not declared by a <file> element");
        }
        return true;
    };
    for (std::string_view name : SplitList(node.attribute("base").as_string(""))) out.baseFiles.emplace_back(name);
    if (!checkFileNames(out.baseFiles, "<driver base>")) return false;
    for (const pugi::xml_node set : node.children("set")) {
        NesDriverConfig::FileSet fileSet;
        fileSet.id = set.attribute("id").as_string("");
        if (fileSet.id.empty()) return Fail(error, "<driver><set>: the id attribute is required");
        for (const NesDriverConfig::FileSet& other : out.sets)
            if (other.id == fileSet.id) return Fail(error, "<driver><set>: the id \"" + fileSet.id + "\" appears twice");
        for (std::string_view name : SplitList(set.attribute("files").as_string(""))) fileSet.files.emplace_back(name);
        if (!checkFileNames(fileSet.files, "<driver><set id=\"" + fileSet.id + "\" files>")) return false;
        if (!ParseRamSet(set, "<driver><set id=\"" + fileSet.id + "\" ram_set>", fileSet.ramSet, error)) return false;
        out.sets.push_back(std::move(fileSet));
    }
    if (out.sets.size() > NesDriverConfig::kMaxSets) return Fail(error, "<driver>: at most 15 <set> elements");
    for (std::string_view part : SplitList(node.attribute("requests").as_string(""))) {
        std::uint16_t address = 0;
        if (!ParseAddress(part, address) || address >= 0x2000)
            return Fail(error, std::string("<driver requests>: expected RAM addresses below 0x2000, got \"") + std::string(part) + "\"");
        out.requests.push_back(address);
    }
    if (out.requests.size() > NesDriverConfig::kMaxRequests) return Fail(error, "<driver requests>: at most 7 addresses");
    return true;
}

std::string ExtractDriverElementXml(std::string_view gameDefinitionXml) {
    pugi::xml_document doc;
    const pugi::xml_parse_result parsed = doc.load_buffer(gameDefinitionXml.data(), gameDefinitionXml.size(),
                                                           pugi::parse_default, pugi::encoding_utf8);
    if (!parsed) return {};
    const pugi::xml_node driver = doc.child("chipestra").child("game").child("driver");
    if (!driver) return {};
    std::ostringstream text;
    driver.print(text, "", pugi::format_raw);
    return text.str();
}
