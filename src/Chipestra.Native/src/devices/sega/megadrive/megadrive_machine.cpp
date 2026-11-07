// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill

#include "devices/sega/megadrive/megadrive_machine.h"

#include "pugixml/pugixml.hpp"
#include "resource_hash.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace {

constexpr float kDcBlockHz = 30.0f;
}  // namespace

namespace {

// Finds ` name="value"` in `xml`; returns the position of the value and its length.
bool FindAttributeValue(const std::string& xml, const char* name, std::size_t& valueStart, std::size_t& valueLength) {
    const std::string key = std::string(" ") + name + "=" + std::string(1, char(34));
    const std::size_t at = xml.find(key);
    if (at == std::string::npos) return false;
    valueStart = at + key.size();
    const std::size_t end = xml.find(char(34), valueStart);
    if (end == std::string::npos) return false;
    valueLength = end - valueStart;
    return true;
}

std::vector<std::string> SplitBar(const std::string& text) {
    std::vector<std::string> items;
    std::size_t start = 0;
    for (;;) {
        const std::size_t bar = text.find('|', start);
        items.push_back(text.substr(start, bar == std::string::npos ? std::string::npos : bar - start));
        if (bar == std::string::npos) break;
        start = bar + 1;
    }
    return items;
}

}  // namespace

std::size_t CountMegaDriveDriverRegions(std::string_view driverXml) {
    const std::string xml(driverXml);
    std::size_t start = 0, length = 0;
    if (!FindAttributeValue(xml, "region", start, length)) return 1;
    return SplitBar(xml.substr(start, length)).size();
}

std::string SelectMegaDriveDriverRegion(std::string_view driverXml, std::size_t index, std::string* error) {
    std::string xml(driverXml);
    const std::size_t regions = CountMegaDriveDriverRegions(driverXml);
    if (index >= regions) {
        if (error != nullptr) *error = "<driver region>: the region number is beyond the list";
        return {};
    }
    for (const char* name : {"region", "warmup_frames", "release_cycle"}) {
        std::size_t start = 0, length = 0;
        if (!FindAttributeValue(xml, name, start, length)) continue;
        const std::vector<std::string> items = SplitBar(xml.substr(start, length));
        if (items.size() != 1 && items.size() != regions) {
            if (error != nullptr) *error = std::string("<driver ") + name + ">: expected one value or one for each region";
            return {};
        }
        xml.replace(start, length, items.size() == 1 ? items[0] : items[index]);
    }
    return xml;
}

MegaDriveTiming MegaDriveTiming::For(MegaDriveRegion region) {
    MegaDriveTiming t;
    t.masterClockHz = region == MegaDriveRegion::Europe ? 53'203'424 : 53'693'175;
    t.z80ClockHz = t.masterClockHz / 15;
    t.ymClockHz = t.masterClockHz / 7;
    t.cyclesPerFrame = (region == MegaDriveRegion::Europe ? 313u : 262u) * 228u;
    t.ymStreamRateHz = static_cast<std::uint32_t>(t.ymClockHz / 144);
    t.psgStreamRateHz = static_cast<std::uint32_t>(t.z80ClockHz / 2);
    return t;
}

std::uint8_t MegaDriveVersionRegister(MegaDriveRegion region) {
    switch (region) {
        case MegaDriveRegion::Usa: return 0x80;
        case MegaDriveRegion::Europe: return 0xC0;
        default: return 0x00;
    }
}

const char* MegaDriveRegionName(MegaDriveRegion region) {
    switch (region) {
        case MegaDriveRegion::Usa: return "usa";
        case MegaDriveRegion::Europe: return "europe";
        default: return "japan";
    }
}

namespace {

bool ParseNumber(std::string_view text, std::uint64_t& value) {
    int base = 10;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text.remove_prefix(2);
    }
    if (text.empty()) return false;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, base);
    return result.ec == std::errc() && result.ptr == text.data() + text.size();
}

bool ParseImage(std::string_view text, MegaDriveDriverConfig::Image& image) {
    const std::size_t colon = text.find(':');
    if (colon == std::string_view::npos) return false;
    std::uint64_t offset = 0, size = 0;
    if (!ParseNumber(text.substr(0, colon), offset) || offset > 0xFFFFFF) return false;
    if (text.substr(colon + 1) == "lzss") {
        image.romOffset = static_cast<std::uint32_t>(offset);
        image.size = 0;
        image.lzss = true;
        return true;
    }
    if (!ParseNumber(text.substr(colon + 1), size)) return false;
    if (size == 0 || size > 0x2000) return false;
    image.romOffset = static_cast<std::uint32_t>(offset);
    image.size = static_cast<std::uint32_t>(size);
    return true;
}

bool ParseDataCopy(std::string_view text, MegaDriveDriverConfig::DataCopy& copy) {
    const std::size_t at = text.find('@');
    if (at == std::string_view::npos) return false;
    MegaDriveDriverConfig::Image image;
    std::uint64_t address = 0;
    if (!ParseImage(text.substr(0, at), image) || image.lzss || !ParseNumber(text.substr(at + 1), address)) return false;
    if (address + image.size > 0x2000) return false;
    copy.romOffset = image.romOffset;
    copy.size = image.size;
    copy.address = static_cast<std::uint16_t>(address);
    return true;
}

bool Fail(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
    return false;
}

}  // namespace

bool ParseMegaDriveDriverConfig(std::string_view driverXml, MegaDriveDriverConfig& out, std::string* error) {
    out = MegaDriveDriverConfig{};
    pugi::xml_document doc;
    const pugi::xml_parse_result parsed = doc.load_buffer(driverXml.data(), driverXml.size(), pugi::parse_default, pugi::encoding_utf8);
    if (!parsed) return Fail(error, std::string("<driver>: ") + parsed.description());
    const pugi::xml_node node = doc.child("driver");
    if (!node) return Fail(error, "no <driver> element");

    if (!ParseImage(node.attribute("music").as_string(""), out.music))
        return Fail(error, "<driver music>: expected \"offset:size\" (the size at most 0x2000), e.g. music=\"0x16A6C:5366\"");
    if (const pugi::xml_attribute data = node.attribute("music_data")) {
        // "offset:size@address", several separated by spaces
        std::string_view list = data.value();
        std::size_t i = 0;
        while (i < list.size()) {
            while (i < list.size() && list[i] == ' ') ++i;
            const std::size_t start = i;
            while (i < list.size() && list[i] != ' ') ++i;
            if (i == start) break;
            MegaDriveDriverConfig::DataCopy copy;
            if (!ParseDataCopy(list.substr(start, i - start), copy))
                return Fail(error, "<driver music_data>: expected \"offset:size@address\" (the data must end inside the Z80's 8 KB RAM)");
            out.musicData.push_back(copy);
        }
    }
    if (const pugi::xml_attribute stop = node.attribute("stop_request")) {
        std::uint64_t value = 0;
        if (!ParseNumber(stop.value(), value) || value > 0xFF) return Fail(error, "<driver stop_request>: expected a request number (0-0xFF)");
        out.stopRequest = static_cast<std::uint8_t>(value);
    }
    if (const pugi::xml_attribute queue = node.attribute("queue")) {
        // "base:read:write:mask"
        std::string_view text = queue.value();
        std::uint64_t part[4] = {0, 0, 0, 0};
        for (int i = 0; i < 4; ++i) {
            const std::size_t colon = text.find(':');
            if ((i < 3) != (colon != std::string_view::npos) || !ParseNumber(text.substr(0, colon), part[i]))
                return Fail(error, "<driver queue>: expected \"base:read:write:mask\" (Z80 RAM addresses and a mask)");
            if (colon != std::string_view::npos) text.remove_prefix(colon + 1);
        }
        if (part[0] > 0x1FFF || part[1] > 0x1FFF || part[2] > 0x1FFF || part[3] > 0xFF || part[0] + part[3] + 2 > 0x2000)
            return Fail(error, "<driver queue>: the addresses must be inside the Z80's RAM (0-0x1FFF) and the mask at most 0xFF");
        MegaDriveDriverConfig::RequestQueue ring;
        ring.base = static_cast<std::uint16_t>(part[0]);
        ring.readAddress = static_cast<std::uint16_t>(part[1]);
        ring.writeAddress = static_cast<std::uint16_t>(part[2]);
        ring.mask = static_cast<std::uint8_t>(part[3]);
        if (const pugi::xml_attribute code = node.attribute("queue_code")) {
            std::uint64_t value = 0;
            if (!ParseNumber(code.value(), value) || value > 0xFF) return Fail(error, "<driver queue_code>: expected a command code (0-0xFF)");
            ring.code = static_cast<std::uint8_t>(value);
        }
        if (const pugi::xml_attribute init = node.attribute("queue_init")) {
            // "value:code" in hexadecimal, separated by spaces
            std::string_view list = init.value();
            std::size_t i = 0;
            while (i < list.size()) {
                while (i < list.size() && list[i] == ' ') ++i;
                const std::size_t start = i;
                while (i < list.size() && list[i] != ' ') ++i;
                if (i == start) break;
                const std::string_view entry = list.substr(start, i - start);
                const std::size_t colon = entry.find(':');
                unsigned value = 0, code = 0;
                if (colon == std::string_view::npos ||
                    std::from_chars(entry.data(), entry.data() + colon, value, 16).ec != std::errc() ||
                    std::from_chars(entry.data() + colon + 1, entry.data() + entry.size(), code, 16).ec != std::errc() || value > 0xFF || code > 0xFF)
                    return Fail(error, "<driver queue_init>: expected entries \"value:code\" in hexadecimal separated by spaces");
                ring.initial.emplace_back(static_cast<std::uint8_t>(value), static_cast<std::uint8_t>(code));
            }
            if (ring.initial.size() * 2 > static_cast<std::size_t>(ring.mask) + 1)
                return Fail(error, "<driver queue_init>: more entries than the ring holds");
        }
        out.queue = ring;
    }
    if (const pugi::xml_attribute table = node.attribute("song_table")) {
        std::uint64_t offset = 0;
        if (!ParseNumber(table.value(), offset) || offset > 0xFFFFFF) return Fail(error, "<driver song_table>: expected a cartridge offset");
        out.songTable = static_cast<std::uint32_t>(offset);
    }
    if (const pugi::xml_attribute first = node.attribute("song_first")) {
        std::uint64_t value = 0;
        if (!ParseNumber(first.value(), value) || value < 1 || value > 0xFF) return Fail(error, "<driver song_first>: expected a request number (1-0xFF)");
        out.songFirst = static_cast<std::uint8_t>(value);
    }
    if (const pugi::xml_attribute address = node.attribute("song_address")) {
        std::uint64_t value = 0;
        if (!ParseNumber(address.value(), value) || value > 0x1FFF) return Fail(error, "<driver song_address>: expected a Z80 RAM address (0-0x1FFF)");
        out.songAddress = static_cast<std::uint16_t>(value);
    }
    if (const pugi::xml_attribute region = node.attribute("region")) {
        const std::string_view text = region.value();
        if (text == "japan") out.region = MegaDriveRegion::Japan;
        else if (text == "usa") out.region = MegaDriveRegion::Usa;
        else if (text == "europe") out.region = MegaDriveRegion::Europe;
        else return Fail(error, "<driver region>: expected \"japan\", \"usa\" or \"europe\"");
    }
    if (const pugi::xml_attribute crc = node.attribute("cart_crc32")) {
        std::string_view text = crc.value();
        std::uint32_t value = 0;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value, 16);
        if (text.empty() || text.size() > 8 || result.ec != std::errc() || result.ptr != text.data() + text.size())
            return Fail(error, "<driver cart_crc32>: expected a CRC-32 in hexadecimal, e.g. cart_crc32=\"edc0fb28\"");
        out.cartridgeCrc32 = value;
    }
    if (const pugi::xml_attribute request = node.attribute("request")) {
        std::uint64_t address = 0;
        if (!ParseNumber(request.value(), address) || address > 0x1FFF) return Fail(error, "<driver request>: expected a Z80 RAM address (0-0x1FFF)");
        out.requestAddress = static_cast<std::uint16_t>(address);
    }
    if (!ParseNumber(node.attribute("release_cycle").as_string(""), out.releaseCycle))
        return Fail(error, "<driver release_cycle>: expected a number of Z80 cycles");
    std::uint64_t warmupFrames = 0;
    if (!ParseNumber(node.attribute("warmup_frames").as_string(""), warmupFrames) || warmupFrames > 100000)
        return Fail(error, "<driver warmup_frames>: expected a number of frames (0-100000)");
    const std::uint64_t frameCycles = MegaDriveTiming::For(out.region).cyclesPerFrame;
    out.requestCycle = (out.releaseCycle / frameCycles + warmupFrames) * frameCycles;
    if (out.requestCycle < out.releaseCycle) out.requestCycle = out.releaseCycle;
    for (const pugi::xml_node set : node.children("set")) {
        MegaDriveDriverConfig::Image image;
        if (!ParseImage(set.attribute("image").as_string(""), image))
            return Fail(error, "<driver><set image>: expected \"offset:size\" (the size at most 0x2000)");
        out.setIds.emplace_back(set.attribute("id").as_string(""));
        out.sets.push_back(image);
    }
    return true;
}

bool DecodeMegaDriveLzss(const std::vector<std::uint8_t>& cartridge, std::uint32_t offset, std::vector<std::uint8_t>& out, std::uint32_t* consumed) {
    out.clear();
    std::uint64_t p = offset;
    auto next = [&](std::uint8_t& value) {
        if (p >= cartridge.size()) return false;
        value = cartridge[p++];
        return true;
    };
    std::uint8_t high = 0, low = 0;
    if (!next(high) || !next(low)) return false;
    // The count is how many stream bytes follow, less one (a flag byte and a literal count one, a match two); the
    // decoder stops when it has gone through them all.
    std::int64_t remaining = (static_cast<std::int64_t>(high) << 8) | low;
    std::array<std::uint8_t, 0x1000> window{};
    std::uint32_t position = 0xFEE;
    std::uint8_t flags = 0;
    int bitsLeft = 0;
    for (;;) {
        if (bitsLeft == 0) {
            if (!next(flags)) return false;
            --remaining;
            bitsLeft = 8;
        }
        const bool literal = (flags & 0x80) != 0;
        flags = static_cast<std::uint8_t>(flags << 1);
        --bitsLeft;
        if (literal) {
            std::uint8_t b = 0;
            if (!next(b)) return false;
            out.push_back(b);
            window[position] = b;
            position = (position + 1) & 0xFFF;
        } else {
            std::uint8_t b1 = 0, b2 = 0;
            if (!next(b1) || !next(b2)) return false;
            const unsigned pair = (static_cast<unsigned>(b1) << 8) | b2;
            unsigned source = pair >> 4;
            const unsigned length = (pair & 0xF) + 3;
            for (unsigned i = 0; i < length; ++i) {
                const std::uint8_t b = window[source];
                out.push_back(b);
                window[position] = b;
                position = (position + 1) & 0xFFF;
                source = (source + 1) & 0xFFF;
            }
            --remaining;
        }
        if (out.size() > 0x2000) return false;
        if (remaining <= 0) break;
        --remaining;
    }
    if (consumed != nullptr) *consumed = static_cast<std::uint32_t>(p - offset);
    return true;
}

std::vector<std::uint8_t> DecodeMegaDriveCartridge(const std::uint8_t* data, std::size_t size) {
    constexpr std::size_t kHeader = 512;
    constexpr std::size_t kBlock = 16384;
    // A Super Magic Drive file: a 512-byte header (bytes 8 and 9 are 0xAA 0xBB) and blocks of 16 KB whose first half
    // holds the odd bytes and second half the even bytes. Any other file is a plain image (.bin, .md).
    if (size > kHeader && (size - kHeader) % kBlock == 0 && data[8] == 0xAA && data[9] == 0xBB) {
        std::vector<std::uint8_t> out(size - kHeader);
        for (std::size_t block = 0; block < out.size() / kBlock; ++block) {
            const std::uint8_t* in = data + kHeader + block * kBlock;
            std::uint8_t* dst = out.data() + block * kBlock;
            for (std::size_t i = 0; i < kBlock / 2; ++i) {
                dst[2 * i + 1] = in[i];
                dst[2 * i] = in[kBlock / 2 + i];
            }
        }
        return out;
    }
    return std::vector<std::uint8_t>(data, data + size);
}

MegaDriveMachineInputs AssembleMegaDriveMachineInputs(const MachineResources& resources) {
    MegaDriveMachineInputs inputs;
    if (resources.Count() == 0) return inputs;
    const ResolvedResource* rom = resources.Find(kMegaDriveCartridgeResource);
    if (rom == nullptr) throw std::runtime_error(std::string("Mega Drive: required resource \"") + kMegaDriveCartridgeResource + "\" is missing");
    const ResolvedResource* driver = resources.Find(kMegaDriveDriverResource);
    if (driver == nullptr)
        throw std::runtime_error(std::string("Mega Drive: required resource \"") + kMegaDriveDriverResource +
                                 "\" is missing (the game XML needs a <driver> element)");
    std::string error;
    if (!ParseMegaDriveDriverConfig(std::string_view(reinterpret_cast<const char*>(driver->Data()), driver->Size()), inputs.driver, &error))
        throw std::runtime_error("Mega Drive: " + error);
    inputs.cartridge = DecodeMegaDriveCartridge(rom->Data(), rom->Size());
    if (inputs.driver.cartridgeCrc32.has_value()) {
        const std::uint32_t found = Crc32(inputs.cartridge.data(), inputs.cartridge.size());
        if (found != *inputs.driver.cartridgeCrc32) {
            char text[160];
            std::snprintf(text, sizeof(text), "Mega Drive: the cartridge image (in 68000 order) has CRC-32 %08x, but <driver cart_crc32> is %08x: a different dump or a damaged file", found, *inputs.driver.cartridgeCrc32);
            throw std::runtime_error(text);
        }
    }
    auto inside = [&](MegaDriveDriverConfig::Image& image) {
        if (image.lzss) {
            std::vector<std::uint8_t> decoded;
            std::uint32_t used = 0;
            if (!DecodeMegaDriveLzss(inputs.cartridge, image.romOffset, decoded, &used)) return false;
            image.size = used;
            return true;
        }
        return static_cast<std::uint64_t>(image.romOffset) + image.size <= inputs.cartridge.size();
    };
    if (!inside(inputs.driver.music)) throw std::runtime_error("Mega Drive: the sound driver (<driver music>) is not inside the cartridge");
    for (const MegaDriveDriverConfig::DataCopy& copy : inputs.driver.musicData)
        if (static_cast<std::uint64_t>(copy.romOffset) + copy.size > inputs.cartridge.size())
            throw std::runtime_error("Mega Drive: a <driver music_data> range is not inside the cartridge");
    if (inputs.driver.songTable.has_value()) {
        const std::uint32_t table = *inputs.driver.songTable;
        if (static_cast<std::uint64_t>(table) + 2 > inputs.cartridge.size()) throw std::runtime_error("Mega Drive: <driver song_table> is not inside the cartridge");
        const std::uint32_t count = ((static_cast<std::uint32_t>(inputs.cartridge[table]) << 8) | inputs.cartridge[table + 1]) / 2;
        if (count == 0 || count > 255 || static_cast<std::uint64_t>(table) + 2ull * count > inputs.cartridge.size())
            throw std::runtime_error("Mega Drive: <driver song_table> is not a table of songs");
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::uint32_t entry = table + 2 * i;
            const std::uint32_t offset = (static_cast<std::uint32_t>(inputs.cartridge[entry]) << 8) | inputs.cartridge[entry + 1];
            std::vector<std::uint8_t> block;
            if (!DecodeMegaDriveLzss(inputs.cartridge, table + offset, block, nullptr) || inputs.driver.songAddress + block.size() > 0x2000)
                throw std::runtime_error("Mega Drive: a song of <driver song_table> does not decode or does not fit in the Z80's RAM");
        }
        inputs.driver.songCount = count;
    }
    for (MegaDriveDriverConfig::Image& image : inputs.driver.sets)
        if (!inside(image)) throw std::runtime_error("Mega Drive: a <set> program is not inside the cartridge");
    return inputs;
}

MegaDriveMachine::MegaDriveMachine(std::vector<std::uint8_t> cartridge, MegaDriveDriverConfig driver)
    : cartridge_(std::move(cartridge)),
      driver_(std::move(driver)),
      timing_(MegaDriveTiming::For(driver_.region)),
      clock_converter_(kNativeRuntimeTickRateHz, ClockRate{timing_.z80ClockHz}),
      pending_ym_(Ym2612DeviceAdapter::kChannelCount),
      pending_psg_(Sn76489SoundDeviceAdapter::kChannelCount) {
    auto add = [this](const char* device, const char* name, std::uint32_t rate) {
        LogicalChannel channel;
        channel.Id = ChannelId(static_cast<std::uint32_t>(channels_.size()));
        channel.DeviceName = device;
        channel.ChannelName = name;
        channel.NativeSampleRate = rate;
        channels_.push_back(std::move(channel));
    };
    for (const char* name : {"FM 1", "FM 2", "FM 3", "FM 4", "FM 5", "FM 6", "DAC"}) add("YM2612", name, timing_.ymStreamRateHz);
    for (const char* name : {"Tone 1", "Tone 2", "Tone 3", "Noise"}) add("SN76489", name, timing_.psgStreamRateHz);
    for (std::uint32_t c = 0; c < kYmChannelCount; ++c) channels_[c].RoutingGain = kYmRoutingGain * 2.0f;
    for (std::uint32_t c = kPsgFirstChannel; c < channels_.size(); ++c) channels_[c].RoutingGain = kPsgRoutingGain;
    for (std::uint32_t c = 0; c < kYmChannelCount; ++c) channels_[c].AnalogFilterGroup = AnalogFilterGroupId::Fm;
    for (std::uint32_t c = kPsgFirstChannel; c < channels_.size(); ++c) channels_[c].AnalogFilterGroup = AnalogFilterGroupId::Psg;
}

void MegaDriveMachine::Initialize() {
    z80_ = std::make_unique<MameDerivedZ80Adapter>();
    ym_ = std::make_unique<Ym2612DeviceAdapter>(timing_.z80ClockHz);
    psg_ = std::make_unique<Sn76489SoundDeviceAdapter>();
    z80_->SetMemoryCallbacks([this](std::uint16_t address) { return ReadBus(address); },
                             [this](std::uint16_t address, std::uint8_t value) { WriteBus(address, value); });
    z80_->SetIoCallbacks([](std::uint16_t) -> std::uint8_t { return 0xFF; }, [](std::uint16_t, std::uint8_t) {});
    ColdStart(0, true);
}

void MegaDriveMachine::Reset() { ColdStart(0, true); }

void MegaDriveMachine::Shutdown() {}

void MegaDriveMachine::LoadImage(const MegaDriveDriverConfig::Image& image, bool applyData) {
    if (image.lzss) {
        std::vector<std::uint8_t> decoded;
        if (DecodeMegaDriveLzss(cartridge_, image.romOffset, decoded, nullptr))
            for (std::size_t i = 0; i < decoded.size() && i < ram_.size(); ++i) ram_[i] = decoded[i];
    } else {
        for (std::uint32_t i = 0; i < image.size; ++i) ram_[i] = cartridge_[image.romOffset + i];
    }
    if (applyData) {
        for (const MegaDriveDriverConfig::DataCopy& copy : driver_.musicData)
            for (std::uint32_t i = 0; i < copy.size; ++i) ram_[copy.address + i] = cartridge_[copy.romOffset + i];
        if (driver_.queue.has_value()) {
            ram_[driver_.queue->readAddress] = 0;
            ram_[driver_.queue->writeAddress] = 0;
            for (const auto& entry : driver_.queue->initial) QueueRequest(entry.first, entry.second);
        }
    }
}

void MegaDriveMachine::QueueRequest(std::uint8_t value, std::uint8_t code) {
    const MegaDriveDriverConfig::RequestQueue& ring = *driver_.queue;
    const std::uint8_t position = ram_[ring.writeAddress] & ring.mask;
    ram_[(ring.base + position) & 0x1FFF] = value;
    ram_[(ring.base + position + 1) & 0x1FFF] = code;
    ram_[ring.writeAddress] = static_cast<std::uint8_t>((position + 2) & ring.mask);
}

void MegaDriveMachine::LoadSong(std::uint8_t request) {
    if (!driver_.songTable.has_value() || driver_.songCount == 0) return;
    const std::uint32_t table = *driver_.songTable;
    std::uint32_t index = 0;
    if (request >= driver_.songFirst && static_cast<std::uint32_t>(request - driver_.songFirst) < driver_.songCount) index = request - driver_.songFirst;
    const std::uint32_t entry = table + 2 * index;
    const std::uint32_t offset = (static_cast<std::uint32_t>(cartridge_[entry]) << 8) | cartridge_[entry + 1];
    std::vector<std::uint8_t> block;
    if (!DecodeMegaDriveLzss(cartridge_, table + offset, block, nullptr)) return;
    for (std::size_t i = 0; i < block.size() && driver_.songAddress + i < ram_.size(); ++i) ram_[driver_.songAddress + i] = block[i];
}

void MegaDriveMachine::ColdStartForTest(std::uint32_t setNumber, bool runToRequest) { ColdStart(setNumber, runToRequest); }

void MegaDriveMachine::ColdStart(std::uint32_t setNumber, bool runToRequest) {
    if (!cartridge_.empty() && setNumber == 0 && runToRequest && restore_warm_state_ && warm_.valid) {
        // The same state the warm-up run ends in: restore it instead of running again.
        ram_ = warm_.ram;
        bank_ = warm_.bank;
        z80_->RestoreState(warm_.z80);
        ym_->RestoreState(warm_.ym);
        psg_->RestoreState(warm_.psg);
        for (auto& channel : pending_ym_) channel.clear();
        for (auto& channel : pending_psg_) channel.clear();
        dc_block_ = {};
        cycles_done_ = warm_.cyclesDone;
        origin_cycles_ = warm_.cyclesDone;
        cumulative_ticks_ = 0;
        ym_samples_made_ = warm_.ymSamplesMade;
        psg_samples_made_ = warm_.psgSamplesMade;
        next_irq_edge_ = warm_.nextIrqEdge;
        irq_high_ = warm_.irqHigh;
        loaded_set_ = 0;
        command_sent_since_reset_ = false;
        return;
    }
    ResetMachineState();
    loaded_set_ = setNumber;
    if (cartridge_.empty()) return;  // a registered-but-not-playable set

    LoadImage(setNumber == 0 ? driver_.music : driver_.sets[setNumber - 1], setNumber == 0);
    if (setNumber == 0 && runToRequest) {
        RunZ80To(driver_.requestCycle - driver_.releaseCycle, false);
        // The chips have not been touched since the driver's last access; bring them to this time with the sound
        // discarded, or the first Render() would deliver the idle time of the warm-up as the start of the track.
        SyncYm(cycles_done_);
        SyncPsg(cycles_done_);
        origin_cycles_ = cycles_done_;
        warm_.ram = ram_;
        warm_.bank = bank_;
        warm_.z80 = z80_->SaveState();
        warm_.ym = ym_->SaveState();
        warm_.psg = psg_->SaveState();
        warm_.cyclesDone = cycles_done_;
        warm_.ymSamplesMade = ym_samples_made_;
        warm_.psgSamplesMade = psg_samples_made_;
        warm_.nextIrqEdge = next_irq_edge_;
        warm_.irqHigh = irq_high_;
        warm_.valid = true;
    }
}

void MegaDriveMachine::ResetMachineState() {
    ram_.fill(0);
    bank_ = 0;
    z80_->Reset();
    z80_->SetInputLine(Z80InputLine::Irq, Z80InputLineState::Clear);
    ym_->Reset();
    psg_->Reset();
    for (auto& channel : pending_ym_) channel.clear();
    for (auto& channel : pending_psg_) channel.clear();
    dc_block_ = {};
    cycles_done_ = 0;
    origin_cycles_ = 0;
    cumulative_ticks_ = 0;
    ym_samples_made_ = 0;
    psg_samples_made_ = 0;
    irq_high_ = false;
    // The first interrupt falls at scanline 224 of the frame the Z80 was released in, or of the next one.
    const std::uint64_t phase = driver_.releaseCycle % timing_.cyclesPerFrame;
    next_irq_edge_ = kIrqAssertCycle >= phase ? kIrqAssertCycle - phase : kIrqAssertCycle + timing_.cyclesPerFrame - phase;
    loaded_set_ = 0;
    command_sent_since_reset_ = false;
}

bool MegaDriveMachine::AcceptsCommand(const MusicCommand& command) const {
    if (command.Kind == MusicCommandKind::Stop) return true;
    if (command.Kind != MusicCommandKind::Raw && command.Kind != MusicCommandKind::Effect) return false;
    return (command.Auxiliary & 0x0F) <= driver_.sets.size();
}

void MegaDriveMachine::SendCommand(const MusicCommand& command) {
    if (!AcceptsCommand(command) || cartridge_.empty()) return;
    if (command.Kind == MusicCommandKind::Stop) {
        ColdStart(0, true);
        return;
    }
    const std::uint32_t setNumber = command.Auxiliary & 0x0F;
    if (driver_.stopRequest.has_value() && command.Value == *driver_.stopRequest && setNumber == 0) {
        // Silence everything: start again (below) and write nothing.
        ColdStart(0, true);
        command_sent_since_reset_ = true;
        return;
    }
    // The driver has no "silence everything" request, and a voice program has to replace it: start again.
    if (command_sent_since_reset_ || setNumber != loaded_set_ || setNumber != 0) ColdStart(setNumber, true);
    command_sent_since_reset_ = true;
    if (setNumber == 0) {
        LoadSong(command.Value);
        if (driver_.queue.has_value()) QueueRequest(command.Value, driver_.queue->code);
        else ram_[driver_.requestAddress & 0x1FFF] = command.Value;
    }
}

std::uint8_t MegaDriveMachine::ReadBus(std::uint16_t address) {
    if (address < 0x4000) return ram_[address & 0x1FFF];
    if (address < 0x6000) {
        SyncYm(Now());
        ym_->SetAccessTime(Now() * timing_.ymClockHz);
        return ym_->ReadStatus();
    }
    if (address < 0x7F00) return 0xFF;
    if (address < 0x8000) return 0;  // the VDP's ports: nothing the sound driver reads
    const std::uint32_t source = (bank_ + (address & 0x7FFFu));
    return source < cartridge_.size() ? cartridge_[source] : std::uint8_t{0xFF};
}

void MegaDriveMachine::WriteBus(std::uint16_t address, std::uint8_t value) {
    auto psgWrite = [this](std::uint8_t v) {
        SyncPsg(Now());
        if (chip_write_observer_) chip_write_observer_(AbsoluteCycle(Now()), 'P', 0, v);
        psg_->Write(v);
    };
    if (address < 0x4000) {
        ram_[address & 0x1FFF] = value;
    } else if (address < 0x6000) {
        SyncYm(Now());
        if (chip_write_observer_) chip_write_observer_(AbsoluteCycle(Now()), 'Y', address & 3u, value);
        ym_->SetAccessTime(Now() * timing_.ymClockHz);
        ym_->Write(address & 3u, value);
    } else if (address < 0x6100) {
        // The nine-bit bank register: each write shifts bit 0 in at the top (bit 23 of the 68000 address).
        bank_ = ((bank_ >> 1) | ((value & 1u) << 23)) & 0xFF8000u;
    } else if (address >= 0x7F00 && address < 0x8000) {
        const std::uint32_t offset = address & 0xFFu;
        if (offset == 0x11 || offset == 0x13 || offset == 0x15 || offset == 0x17) psgWrite(value);
    } else if (address >= 0x8000) {
        // A write through the cartridge window reaches whatever the 68000 would find there: the PSG port of the VDP.
        const std::uint32_t target = bank_ + (address & 0x7FFFu);
        const std::uint32_t low = target & 0x1Fu;
        if ((target & 0xFFFFE0u) == 0xC00000u && (low == 0x11 || low == 0x13 || low == 0x15 || low == 0x17)) psgWrite(value);
    }
}

void MegaDriveMachine::SyncYm(std::uint64_t cycle) {
    // The original brings its stream up to the time of a write and counts the sample being made at that moment.
    const std::uint64_t target = cycle * timing_.ymStreamRateHz / timing_.z80ClockHz + 1;
    if (target <= ym_samples_made_) return;
    if (keep_sound_) {
        ym_->Render((target - ym_samples_made_) * Ym2612DeviceAdapter::kInputClocksPerSample, pending_ym_);
    }
    ym_samples_made_ = target;
}

void MegaDriveMachine::SyncPsg(std::uint64_t cycle) {
    const std::uint64_t target = (cycle * timing_.psgStreamRateHz + timing_.z80ClockHz - 1) / timing_.z80ClockHz;
    if (target <= psg_samples_made_) return;
    if (keep_sound_) {
        psg_->Render((target - psg_samples_made_) * Sn76489SoundDeviceAdapter::kInputClocksPerSample, pending_psg_);
    }
    psg_samples_made_ = target;
}

void MegaDriveMachine::RunZ80To(std::uint64_t targetCycle, bool keepSound) {
    keep_sound_ = keepSound;
    while (cycles_done_ < targetCycle) {
        if (next_irq_edge_ <= cycles_done_) {
            if (!irq_high_) {
                z80_->SetInputLine(Z80InputLine::Irq, Z80InputLineState::Assert);
                irq_high_ = true;
                next_irq_edge_ += kIrqLengthCycles;
            } else {
                z80_->SetInputLine(Z80InputLine::Irq, Z80InputLineState::Clear);
                irq_high_ = false;
                next_irq_edge_ += timing_.cyclesPerFrame - kIrqLengthCycles;
            }
            continue;
        }
        const std::uint64_t stop = std::min(targetCycle, next_irq_edge_);
        cycles_done_ += z80_->Execute(stop - cycles_done_);
    }
}

void MegaDriveMachine::UpdateRouting() {
    for (std::uint32_t c = 0; c < kYmChannelCount; ++c) {
        const bool left = ym_->ChannelOutputsLeft(c);
        const bool right = ym_->ChannelOutputsRight(c);
        LogicalChannel& channel = channels_[c];
        channel.RoutingGain = kYmRoutingGain * static_cast<float>((left ? 1 : 0) + (right ? 1 : 0));
        if (!channel.PanOverridden) channel.Pan = (left == right) ? 0.0f : (left ? -1.0f : 1.0f);
    }
}

void MegaDriveMachine::WriteChannels(LogicalChannelSink* sink) {
    const float ymR = 1.0f - 2.0f * 3.14159265358979f * kDcBlockHz / static_cast<float>(timing_.ymStreamRateHz);
    for (std::uint32_t c = 0; c < kYmChannelCount; ++c) {
        std::vector<float>& samples = pending_ym_[c];
        dc_block_[c].Process(samples.data(), samples.size(), ymR);
        if (sink != nullptr) sink->WriteSamples(channels_[c].Id, samples.data(), samples.size());
        samples.clear();
    }
    for (std::uint32_t c = 0; c < Sn76489SoundDeviceAdapter::kChannelCount; ++c) {
        std::vector<float>& samples = pending_psg_[c];
        if (sink != nullptr) sink->WriteSamples(channels_[kPsgFirstChannel + c].Id, samples.data(), samples.size());
        samples.clear();
    }
}

void MegaDriveMachine::Render(EmuDuration duration, LogicalChannelSink* sink) {
    if (sink != nullptr && !channels_registered_) {
        for (const LogicalChannel& channel : channels_) sink->RegisterChannel(channel.Id);
        channels_registered_ = true;
    }
    if (cartridge_.empty()) return;
    cumulative_ticks_ += duration.ticks;
    const std::uint64_t target = origin_cycles_ + clock_converter_.TicksToCycles(EmuDuration(cumulative_ticks_));
    RunZ80To(target, true);
    SyncYm(cycles_done_);
    SyncPsg(cycles_done_);
    UpdateRouting();
    WriteChannels(sink);
}
