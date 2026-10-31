// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Famicom Disk System tests (fds_disk and FdsMachine). The synthetic disk and driver are hand-written; what each
// routine must leave behind is worked out here from the instructions, not read back from the Machine. The test
// with the real disk compares the Machine with recordings of the real game.

#include "fds_machine_tests.h"

#include "default_machine_catalog.h"
#include "definition_xml_locator.h"
#include "devices/nintendo/fds/fds_disk.h"
#include "devices/nintendo/fds/fds_machine.h"
#include "machine_registry.h"
#include "mixer.h"
#include "resource_hash.h"
#include "resource_resolver.h"
#include "test_support.h"
#include "xml_definition_parser.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace {

struct FileSpec {
    std::string name;
    std::uint8_t id = 0;
    std::uint8_t kind = 0;
    std::uint16_t address = 0;
    std::vector<std::uint8_t> data;
};

// Builds a disk image: each side is a 65500-byte area holding block 1 (disk information), block 2 (file count),
// and a block 3 / block 4 pair per file; the rest is zero. `header` puts the 16-byte "FDS" 1A header in front.
std::vector<std::uint8_t> BuildDisk(const std::vector<std::vector<FileSpec>>& sides, bool header) {
    std::vector<std::uint8_t> image;
    if (header) {
        const std::uint8_t h[16] = {'F', 'D', 'S', 0x1A, static_cast<std::uint8_t>(sides.size())};
        image.insert(image.end(), h, h + 16);
    }
    for (const auto& files : sides) {
        std::vector<std::uint8_t> side(65500, 0);
        side[0] = 0x01;
        const char* tag = "*NINTENDO-HVC*";
        for (int i = 0; i < 14; ++i) side[1 + i] = static_cast<std::uint8_t>(tag[i]);
        std::size_t pos = 56;
        side[pos++] = 0x02;
        side[pos++] = static_cast<std::uint8_t>(files.size());
        std::uint8_t number = 0;
        for (const FileSpec& f : files) {
            side[pos++] = 0x03;
            side[pos++] = number++;
            side[pos++] = f.id;
            for (std::size_t i = 0; i < 8; ++i) side[pos++] = i < f.name.size() ? static_cast<std::uint8_t>(f.name[i]) : std::uint8_t{0};
            side[pos++] = static_cast<std::uint8_t>(f.address & 0xFF);
            side[pos++] = static_cast<std::uint8_t>(f.address >> 8);
            side[pos++] = static_cast<std::uint8_t>(f.data.size() & 0xFF);
            side[pos++] = static_cast<std::uint8_t>(f.data.size() >> 8);
            side[pos++] = f.kind;
            side[pos++] = 0x04;
            for (std::uint8_t b : f.data) side[pos++] = b;
        }
        image.insert(image.end(), side.begin(), side.end());
    }
    return image;
}

// The synthetic sound driver at $D000. Two frame routines (called every frame, in this order):
//   $D000  if $0680 != 0: write it to $4000 (APU) and $4080 (disk adapter volume), clear it
//          if $0681 != 0: read $4030, write $4025 (disk registers), read $E000, write $E001 (BIOS area), clear it
//   $D030  if $0682 != 0: play a note on the disk adapter's wavetable (entry 0 = 63, the rest 0; volume 32;
//          frequency 0x0800), clear it
std::vector<std::uint8_t> BuildDriverCode() {
    std::vector<std::uint8_t> code(0x59, 0xEA);
    auto put = [&](std::size_t at, std::initializer_list<std::uint8_t> bytes) {
        for (std::uint8_t b : bytes) code[at++] = b;
    };
    put(0x00, {0xAD, 0x80, 0x06, 0xF0, 0x0B, 0x8D, 0x00, 0x40, 0x8D, 0x80, 0x40, 0xA9, 0x00, 0x8D, 0x80, 0x06});
    put(0x10, {0xAD, 0x81, 0x06, 0xF0, 0x11, 0xAD, 0x30, 0x40, 0x8D, 0x25, 0x40, 0xAD, 0x00, 0xE0, 0x8D, 0x01, 0xE0, 0xA9, 0x00, 0x8D, 0x81, 0x06, 0x60});
    put(0x30, {0xAD, 0x82, 0x06, 0xF0, 0x23, 0xA9, 0x80, 0x8D, 0x89, 0x40, 0xA9, 0x3F, 0x8D, 0x40, 0x40, 0xA9, 0x00, 0x8D, 0x89, 0x40,
               0xA9, 0xA0, 0x8D, 0x80, 0x40, 0xA9, 0x00, 0x8D, 0x82, 0x40, 0xA9, 0x08, 0x8D, 0x83, 0x40, 0xA9, 0x00, 0x8D, 0x82, 0x06, 0x60});
    return code;
}

std::vector<FileSpec> SyntheticFiles() {
    return {
        {"DRV", 1, 0, 0xD000, BuildDriverCode()},
        {"PIC", 2, 1, 0x0000, std::vector<std::uint8_t>(16, 0x77)},
        {"SETA", 3, 0, 0x7000, {0xA1, 0xA2, 0xA3, 0xA4}},
        {"SETB", 4, 0, 0x7000, {0xB1, 0xB2}},
        {"SAVE", 5, 0, 0x6000, {0x55, 0x55}},
    };
}

std::string Hex8(std::uint32_t v) {
    char text[16];
    std::snprintf(text, sizeof(text), "%08x", v);
    return text;
}

std::string SyntheticDriverXml(const std::vector<FileSpec>& files) {
    auto crcOf = [&](const char* name) {
        for (const FileSpec& f : files)
            if (f.name == name) return Hex8(Crc32(f.data.data(), f.data.size()));
        return std::string("00000000");
    };
    return std::string("<driver ram_clear=\"0x0000-0x07FF\" frame=\"0xD000 0xD030\" warmup_frames=\"2\" base=\"DRV\" "
                       "requests=\"0x0680 0x0681 0x0682\">") +
           "<file name=\"DRV\" crc32=\"" + crcOf("DRV") + "\"/>" + "<file name=\"SETA\" crc32=\"" + crcOf("SETA") + "\"/>" +
           "<file name=\"SETB\" crc32=\"" + crcOf("SETB") + "\"/>" + "<set id=\"a\" files=\"SETA\"/><set id=\"b\" files=\"SETB\"/>" +
           "<set id=\"ab\" files=\"SETA SETB\"/><set id=\"r\" ram_set=\"0x0700=0x5A 0x0701=0xA5\"/></driver>";
}

constexpr std::uint8_t kSetA = 1, kSetB = 2, kSetAB = 3, kSetRam = 4;
constexpr std::uint8_t kReq0680 = 1, kReq0681 = 2, kReq0682 = 3;

MachineResources MakeResources(const std::vector<std::uint8_t>& disk, const std::string& driverXml) {
    MachineResources r;
    r.Add(ResolvedResource("disk", disk));
    r.Add(ResolvedResource("driver_xml", std::vector<std::uint8_t>(driverXml.begin(), driverXml.end())));
    return r;
}

std::unique_ptr<FdsMachine> MakeMachine(bool header = false, const std::vector<FileSpec>& files = SyntheticFiles()) {
    FdsMachineInputs inputs = AssembleFdsMachineInputs(MakeResources(BuildDisk({files}, header), SyntheticDriverXml(files)));
    auto machine = std::make_unique<FdsMachine>(std::move(inputs.disk), std::move(inputs.driver));
    machine->Initialize();
    return machine;
}

MusicCommand Cmd(std::uint8_t value, std::uint8_t setNumber, std::uint8_t requestNumber) {
    MusicCommand c;
    c.Kind = MusicCommandKind::Raw;
    c.Value = value;
    c.Auxiliary = NesDriverConfig::PackSelector(setNumber, requestNumber);
    return c;
}

using Channels = std::vector<std::vector<float>>;

// The Machine registers its channels with the sink on its first Render, so one sink serves one Machine.
Channels RenderChannels(FdsMachine& machine, LogicalChannelSink& sink, std::uint64_t ticks) {
    machine.Render(EmuDuration(ticks), &sink);
    Channels out;
    for (const LogicalChannel& channel : machine.LogicalChannels()) {
        const LogicalChannelBuffer& buffer = sink.ChannelBuffer(channel.Id);
        out.emplace_back(buffer.Data(), buffer.Data() + buffer.Size());
    }
    sink.ClearAll();
    return out;
}

float PeakOf(const std::vector<float>& samples) {
    float peak = 0.0f;
    for (float s : samples) peak = std::max(peak, std::fabs(s));
    return peak;
}

template <typename F>
bool Throws(F f) {
    try {
        f();
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

template <typename F>
std::string ThrownMessage(F f) {
    try {
        f();
    } catch (const std::exception& e) {
        return e.what();
    }
    return {};
}

const char* GetEnvVar(const char* name) {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    return std::getenv(name);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
}

std::optional<std::string> ReadFileText(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::nullopt;
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

constexpr std::uint64_t kTicksPerFrame = 3195;  // one frame of 29780.5 cycles, in 192 kHz ticks

}  // namespace

void FdsDiskReadsFilesWithAndWithoutHeader() {
    std::printf("FdsDiskReadsFilesWithAndWithoutHeader\n");
    const std::vector<FileSpec> side0 = {{"AB", 7, 0, 0x6100, {1, 2, 3}}, {"", 8, 1, 0x0000, {9}}};
    const std::vector<FileSpec> side1 = {{"EIGHTCHR", 9, 2, 0x2800, {4, 5}}};
    for (bool header : {false, true}) {
        const std::vector<std::uint8_t> image = BuildDisk({side0, side1}, header);
        const FdsDisk disk = ParseFdsDisk(image.data(), image.size());
        const std::string how = header ? " (with the header)" : " (without the header)";
        Check(disk.sideCount == 2 && disk.files.size() == 3, ("two sides and three files" + how).c_str());
        const FdsFile& a = disk.files[0];
        Check(a.name == "AB" && a.id == 7 && a.kind == 0 && a.loadAddress == 0x6100 && a.side == 0 && a.number == 0 &&
                  a.data == std::vector<std::uint8_t>({1, 2, 3}),
              ("a short name is read without its padding, with id, kind, load address and data" + how).c_str());
        Check(a.crc32 == Crc32(a.data.data(), a.data.size()), ("the file's CRC-32 is that of its data" + how).c_str());
        Check(disk.files[1].name.empty() && disk.files[1].kind == 1 && disk.files[1].number == 1, ("an unnamed character-data file" + how).c_str());
        Check(disk.files[2].name == "EIGHTCHR" && disk.files[2].side == 1 && disk.files[2].number == 0 && disk.files[2].kind == 2,
              ("an eight-character name fills the field; the second side's numbering starts again" + how).c_str());
    }
}

void FdsDiskRejectsBrokenImages() {
    std::printf("FdsDiskRejectsBrokenImages\n");
    const std::vector<std::uint8_t> good = BuildDisk({SyntheticFiles()}, false);
    Check(!Throws([&] { ParseFdsDisk(good.data(), good.size()); }), "the unbroken image reads");
    Check(Throws([&] { ParseFdsDisk(good.data(), good.size() - 1); }), "a size that is not a whole number of sides is rejected");
    Check(Throws([&] { ParseFdsDisk(good.data(), 0); }), "an empty image is rejected");
    std::vector<std::uint8_t> badTag = good;
    badTag[3] = 'X';
    Check(Throws([&] { ParseFdsDisk(badTag.data(), badTag.size()); }), "a side without the disk-information block is rejected");
    std::vector<std::uint8_t> badBlock = good;
    badBlock[56] = 0x09;
    Check(Throws([&] { ParseFdsDisk(badBlock.data(), badBlock.size()); }), "a missing file-count block is rejected");
    std::vector<std::uint8_t> badHeaderBlock = good;
    badHeaderBlock[58] = 0x00;  // the first file's block 3 marker
    Check(Throws([&] { ParseFdsDisk(badHeaderBlock.data(), badHeaderBlock.size()); }), "a missing file-header block is rejected");
    std::vector<std::uint8_t> tooLong = good;
    tooLong[58 + 13] = 0xFF;
    tooLong[58 + 14] = 0xFF;  // the first file claims 65535 bytes
    Check(Throws([&] { ParseFdsDisk(tooLong.data(), tooLong.size()); }), "a file that runs past the end of its side is rejected");
    const std::vector<std::uint8_t> headered = BuildDisk({SyntheticFiles(), SyntheticFiles()}, true);
    std::vector<std::uint8_t> wrongSides = headered;
    wrongSides[4] = 1;
    Check(Throws([&] { ParseFdsDisk(wrongSides.data(), wrongSides.size()); }), "a header that announces another number of sides is rejected");
}

void FdsMachineInputsAreCheckedByNameAndCrc() {
    std::printf("FdsMachineInputsAreCheckedByNameAndCrc\n");
    std::vector<FileSpec> files = SyntheticFiles();
    const std::string xml = SyntheticDriverXml(files);

    // Not playable without resources, but registered.
    Check(DefaultMachineRegistry().Create(MachineId("NINTENDO_FDS"), VariantId("FDS_BASE"), MachineResources{}) != nullptr,
          "the Machine exists in the catalog and can be created without resources");
    Check(!Throws([&] { AssembleFdsMachineInputs(MakeResources(BuildDisk({files}, false), xml)); }), "the matching disk is accepted");

    // The save-data file is not named by the XML: changing it changes the whole image but not the match.
    std::vector<FileSpec> changedSave = files;
    changedSave[4].data = {0x01, 0x02};
    const std::vector<std::uint8_t> imageA = BuildDisk({files}, false);
    const std::vector<std::uint8_t> imageB = BuildDisk({changedSave}, false);
    Check(Crc32(imageA.data(), imageA.size()) != Crc32(imageB.data(), imageB.size()), "the two images differ as a whole");
    Check(!Throws([&] { AssembleFdsMachineInputs(MakeResources(imageB, xml)); }), "a different save file does not stop the disk matching");

    std::vector<FileSpec> changedDriver = files;
    changedDriver[0].data[5] ^= 0xFF;
    const std::string message = ThrownMessage([&] { AssembleFdsMachineInputs(MakeResources(BuildDisk({changedDriver}, false), xml)); });
    Check(message.find("DRV") != std::string::npos && message.find("CRC-32") != std::string::npos,
          "a changed file named by the XML is rejected, naming the file and the CRC-32");

    std::vector<FileSpec> missing = files;
    missing.erase(missing.begin() + 3);  // SETB
    const std::string missingMessage = ThrownMessage([&] { AssembleFdsMachineInputs(MakeResources(BuildDisk({missing}, false), xml)); });
    Check(missingMessage.find("SETB") != std::string::npos && missingMessage.find("not on the disk") != std::string::npos,
          "a file named by the XML that is not on the disk is rejected by name");

    std::vector<FileSpec> badRange = files;
    badRange[3].address = 0x4000;  // SETB below the adapter's RAM
    Check(Throws([&] { AssembleFdsMachineInputs(MakeResources(BuildDisk({badRange}, false), xml)); }),
          "a file that does not fit in the adapter RAM ($6000-$DFFF) is rejected");

    std::vector<FileSpec> pictureData = files;
    pictureData[2].kind = 1;  // SETA is character data
    Check(Throws([&] { AssembleFdsMachineInputs(MakeResources(BuildDisk({pictureData}, false), xml)); }), "a file named by the XML that is not program data is rejected");

    Check(Throws([&] { AssembleFdsMachineInputs(MakeResources(imageA, "<driver base=\"NOPE\"/>")); }), "a base file that no <file> declares is rejected");
    MachineResources noDisk;
    noDisk.Add(ResolvedResource("driver_xml", std::vector<std::uint8_t>(xml.begin(), xml.end())));
    Check(Throws([&] { AssembleFdsMachineInputs(noDisk); }), "resources without the disk are rejected");
}

void FdsMachineLoadsFilesAndHandlesRequests() {
    std::printf("FdsMachineLoadsFilesAndHandlesRequests\n");
    auto machine = MakeMachine();
    FdsMachine& m = *machine;
    Check(m.AdapterRamForTest(0xD000) == 0xAD && m.AdapterRamForTest(0xD030) == 0xAD, "the base file is loaded at its header's address");
    Check(m.AdapterRamForTest(0x7000) == 0 && m.AdapterRamForTest(0x6000) == 0, "other files (a set's, the save file) are not loaded");
    Check(m.FdsChipForTest().SoundIoEnabled(), "the Machine switches the disk adapter's sound registers on, as the BIOS would");
    Check(m.FramesStartedForTest() == 0 && m.RamForTest(0x680) == 0, "after start-up no frame of the real run has started and no request is pending");
    Check(m.LoadedSetForTest() == 0, "the base files alone are loaded at start");

    std::vector<std::tuple<std::uint64_t, std::uint16_t, std::uint8_t>> writes;
    m.SetRegisterWriteObserverForTest([&](std::uint64_t frame, std::uint16_t address, std::uint8_t value) { writes.emplace_back(frame, address, value); });
    const MusicCommand first = Cmd(0x85, 0, kReq0680);
    Check(m.AcceptsCommand(first), "a request on the first listed address is accepted");
    m.SendCommand(first);
    Check(m.RamForTest(0x680) == 0x85 && m.LoadedSetForTest() == 0, "the first command writes its value to the request address without a reset");
    LogicalChannelSink loadSink;
    RenderChannels(m, loadSink, kTicksPerFrame);
    Check(writes.size() == 2 && writes[0] == std::make_tuple(std::uint64_t{0}, std::uint16_t{0x4000}, std::uint8_t{0x85}) &&
              writes[1] == std::make_tuple(std::uint64_t{0}, std::uint16_t{0x4080}, std::uint8_t{0x85}),
          "frame 0 runs the driver: one APU register write and one disk-adapter register write, in order");
    Check(m.RamForTest(0x680) == 0, "the driver cleared the request");
    Check(m.FdsChipForTest().VolumeEnvelopeOutForTest() == 5, "the disk-adapter write reached the chip (direct gain 5)");

    // A set: the base file stays, the set's file appears, and the value is written after the reset.
    m.SendCommand(Cmd(0x42, kSetA, kReq0680));
    Check(m.LoadedSetForTest() == kSetA && m.AdapterRamForTest(0x7000) == 0xA1 && m.AdapterRamForTest(0x7003) == 0xA4 &&
              m.AdapterRamForTest(0xD000) == 0xAD,
          "a set is loaded on top of the base files");
    Check(m.RamForTest(0x680) == 0x42, "the value is written to the request address after the files are in place");
    Check(m.FdsChipForTest().VolumeEnvelopeOutForTest() == 0, "the reset cleared the chip");

    // A request with no address only loads the set (Music Stop is one): nothing is written.
    m.SendCommand(Cmd(0x00, 0, 0));
    Check(m.LoadedSetForTest() == 0 && m.AdapterRamForTest(0x7000) == 0 && m.RamForTest(0x680) == 0,
          "a command with no request address resets to the base files and writes nothing");

    MusicCommand tooFar = Cmd(1, 0, 4);
    Check(!m.AcceptsCommand(tooFar), "a request number beyond the declared addresses is refused");
    Check(!m.AcceptsCommand(Cmd(1, 5, 1)), "a set number beyond the declared sets is refused");
    MusicCommand effect = Cmd(1, 0, 1);
    effect.Kind = MusicCommandKind::Effect;
    Check(!m.AcceptsCommand(effect), "the effect kind (a cartridge's second variable) is not used by a disk game");
    MusicCommand stop;
    stop.Kind = MusicCommandKind::Stop;
    Check(m.AcceptsCommand(stop), "the Stop kind is accepted");
}

void FdsMachineSetsOverlayInOrderAndSwitchingResets() {
    std::printf("FdsMachineSetsOverlayInOrderAndSwitchingResets\n");
    {
        auto machine = MakeMachine();
        machine->SendCommand(Cmd(0x01, kSetAB, kReq0680));
        // SETA = A1 A2 A3 A4, then SETB = B1 B2 over its first two bytes.
        Check(machine->AdapterRamForTest(0x7000) == 0xB1 && machine->AdapterRamForTest(0x7001) == 0xB2 &&
                  machine->AdapterRamForTest(0x7002) == 0xA3 && machine->AdapterRamForTest(0x7003) == 0xA4,
              "files of a set are loaded in the order listed: a later file overwrites an earlier one");
        machine->SendCommand(Cmd(0x01, kSetB, kReq0680));
        Check(machine->AdapterRamForTest(0x7000) == 0xB1 && machine->AdapterRamForTest(0x7002) == 0 && machine->AdapterRamForTest(0x7003) == 0,
              "switching to another set clears what the previous set left in the adapter RAM");
    }
    {
        // A set's ram_set (a set may have no files at all) is written after the driver's, and goes with the set only.
        auto machine = MakeMachine();
        machine->SendCommand(Cmd(0x01, kSetRam, kReq0680));
        Check(machine->RamForTest(0x0700) == 0x5A && machine->RamForTest(0x0701) == 0xA5, "the RAM values of a set are written when the set is used");
        machine->SendCommand(Cmd(0x01, kSetA, kReq0680));
        Check(machine->RamForTest(0x0700) == 0 && machine->RamForTest(0x0701) == 0, "another set does not keep the previous set's RAM values");
        NesDriverConfig bad;
        std::string error;
        Check(!ParseNesDriverConfig("<driver><set id=\"x\" ram_set=\"0x0700=0x100\"/></driver>", bad, &error) && error.find("ram_set") != std::string::npos,
              "a set's ram_set with a value above 0xFF is rejected");
    }
    // Song A (with the wavetable note) then song B equals song B on a fresh machine, sample for sample.
    auto fresh = MakeMachine();
    fresh->SendCommand(Cmd(0x01, kSetB, kReq0682));
    LogicalChannelSink freshSink, switchedSink;
    const Channels freshOut = RenderChannels(*fresh, freshSink, kTicksPerFrame * 20);
    auto switched = MakeMachine();
    switched->SendCommand(Cmd(0x01, kSetA, kReq0682));
    RenderChannels(*switched, switchedSink, kTicksPerFrame * 10);
    switched->SendCommand(Cmd(0x01, kSetB, kReq0682));
    const Channels switchedOut = RenderChannels(*switched, switchedSink, kTicksPerFrame * 20);
    Check(freshOut == switchedOut, "a song after another song equals the same song on a fresh machine, every channel, sample for sample");
    Check(PeakOf(freshOut[FdsMachine::kFdsChannelId]) > 0.01f, "the note was audible (the comparison is not between two silences)");
    // Music Stop (no request) after a song: silent from the first frame, equal to a fresh machine's silence.
    switched->SendCommand(Cmd(0x00, 0, 0));
    const Channels stopped = RenderChannels(*switched, switchedSink, kTicksPerFrame * 5);
    bool silent = true;
    for (const auto& c : stopped) silent = silent && PeakOf(c) < 1e-6f;
    Check(silent, "after a command with no request every channel is silent");
}

void FdsMachineHeaderDoesNotChangeTheSound() {
    std::printf("FdsMachineHeaderDoesNotChangeTheSound\n");
    auto plain = MakeMachine(false);
    auto headered = MakeMachine(true);
    plain->SendCommand(Cmd(0x01, kSetA, kReq0682));
    headered->SendCommand(Cmd(0x01, kSetA, kReq0682));
    LogicalChannelSink plainSink, headeredSink;
    const Channels a = RenderChannels(*plain, plainSink, kTicksPerFrame * 10);
    const Channels b = RenderChannels(*headered, headeredSink, kTicksPerFrame * 10);
    Check(a == b && PeakOf(a[FdsMachine::kFdsChannelId]) > 0.01f, "an image with the 16-byte header sounds exactly like the same image without it");
}

void FdsMachineCountsBiosAndDiskAccesses() {
    std::printf("FdsMachineCountsBiosAndDiskAccesses\n");
    auto machine = MakeMachine();
    FdsMachine& m = *machine;
    LogicalChannelSink busSink;
    RenderChannels(m, busSink, kTicksPerFrame * 5);
    Check(m.BusStatsForTest().diskReads == 0 && m.BusStatsForTest().diskWrites == 0 && m.BusStatsForTest().biosReads == 0 &&
              m.BusStatsForTest().biosWrites == 0 && m.BusStatsForTest().ppuReads == 0 && m.BusStatsForTest().inputReads == 0,
          "a driver that does not touch them leaves every counter at 0 (the vector fetches are not counted)");
    m.SendCommand(Cmd(0x01, 0, kReq0681));
    RenderChannels(m, busSink, kTicksPerFrame * 5);
    const FdsMachine::BusStats& s = m.BusStatsForTest();
    Check(s.diskReads == 1 && s.diskWrites == 1 && s.biosReads == 1 && s.biosWrites == 1,
          "one read and one write of the disk registers and one read and one write of the BIOS area are counted");
    Check(s.ppuReads == 0 && s.ppuWrites == 0 && s.inputReads == 0 && s.inputWrites == 0, "no picture-chip or controller access is counted");
}

void FdsMachineSoundChannelsAndMute() {
    std::printf("FdsMachineSoundChannelsAndMute\n");
    auto machine = MakeMachine();
    FdsMachine& m = *machine;
    const std::vector<LogicalChannel>& channels = m.LogicalChannels();
    Check(channels.size() == 6, "six logical channels: the APU's five and the wavetable chip");
    Check(channels[0].DeviceName == "RP2A03" && channels[5].DeviceName == "RP2C33" && channels[5].ChannelName == "Wave" && channels[5].Id == 5,
          "the wavetable chip is channel 5, device RP2C33");
    Check(channels[0].RoutingGain == 1.8f && channels[4].RoutingGain == 1.8f && channels[5].RoutingGain == 0.4f,
          "route gains: 0.90 x 2 for the APU and 0.2 x 2 for the disk adapter (mono speaker, centred in the Mixer)");
    Check(channels[0].NativeSampleRate == 447443 && channels[5].NativeSampleRate == 1789772, "native sample rates: CPU clock / 4 and the CPU clock");

    m.SendCommand(Cmd(0x01, 0, kReq0682));
    LogicalChannelSink sink;
    m.Render(EmuDuration(kTicksPerFrame * 10), &sink);
    const LogicalChannelBuffer& wave = sink.ChannelBuffer(FdsMachine::kFdsChannelId);
    const std::vector<float> waveSamples(wave.Data(), wave.Data() + wave.Size());
    Check(PeakOf(waveSamples) > 0.01f, "the wavetable chip's channel carries the note");
    bool apuSilent = true;
    for (std::uint32_t c = 0; c < 5; ++c) {
        const LogicalChannelBuffer& b = sink.ChannelBuffer(c);
        apuSilent = apuSilent && PeakOf(std::vector<float>(b.Data(), b.Data() + b.Size())) < 1e-6f;
    }
    Check(apuSilent, "the APU channels stay silent");
    // 10 frames of 29780.5 cycles, one sample per cycle.
    const double expectedCycles = static_cast<double>(kTicksPerFrame * 10) * 1789772.0 / 192000.0;
    Check(std::fabs(static_cast<double>(wave.Size()) - expectedCycles) < 64.0, "the wavetable chip makes one sample per CPU cycle");

    // The Mixer's mute applies to the wavetable chip's channel like any other.
    const std::size_t n = std::min<std::size_t>(waveSamples.size(), 4000);
    LogicalChannelSink canonical;
    canonical.RegisterChannel(FdsMachine::kFdsChannelId);
    canonical.WriteSamples(FdsMachine::kFdsChannelId, waveSamples.data() + waveSamples.size() - n, n);
    Mixer mixer;
    std::vector<StereoFrame> out;
    mixer.MixBlock(canonical, m.LogicalChannels(), n, out);
    float unmuted = 0;
    for (const StereoFrame& f : out) unmuted = std::max(unmuted, std::max(std::fabs(f.left), std::fabs(f.right)));
    m.LogicalChannels()[FdsMachine::kFdsChannelId].Mute = true;
    mixer.MixBlock(canonical, m.LogicalChannels(), n, out);
    float muted = 0;
    for (const StereoFrame& f : out) muted = std::max(muted, std::max(std::fabs(f.left), std::fabs(f.right)));
    Check(unmuted > 0.001f && muted == 0.0f, "muting the wavetable chip's channel silences it in the mix");
}

// ---- the real disk ----

namespace {

struct LoggedWrite {
    std::uint64_t frame;
    std::uint16_t address;
    std::uint8_t value;
    bool operator==(const LoggedWrite& o) const { return frame == o.frame && address == o.address && value == o.value; }
};

// A record of the real game: lines starting with '#' describe it, then one "<frame> <address hex> <value hex>" line
// per write to $4000-$4013, $4015 and $4040-$4092 made by the sound driver, the frame counted from the one that
// handles the request.
struct ReferenceLog {
    std::vector<LoggedWrite> writes;
};

std::optional<ReferenceLog> ReadReferenceLog(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) return std::nullopt;
    ReferenceLog log;
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line[0] == '#') continue;
        std::istringstream fields(line);
        unsigned frame = 0, address = 0, value = 0;
        if (fields >> frame >> std::hex >> address >> value)
            log.writes.push_back({frame, static_cast<std::uint16_t>(address), static_cast<std::uint8_t>(value)});
    }
    return log;
}

}  // namespace

void FdsMachineViaDefinitionXmlRealDisk() {
    std::printf("FdsMachineViaDefinitionXmlRealDisk\n");
    const char* romRootEnv = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRootEnv == nullptr || romRootEnv[0] == '\0') {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is not set - real-disk bring-up not exercised\n");
        return;
    }
    const auto path = FindDefinitionXml("metroid.xml");
    Check(path.has_value(), "xml bring-up: metroid.xml is present (run from the repo root)");
    if (!path) return;
    const auto text = ReadFileText(*path);
    Check(text.has_value(), "xml bring-up: file is readable");
    if (!text) return;
    const XmlParseResult parsed = ParseGameDefinition(*text);
    Check(parsed.Status == XmlParseStatus::Ok, "xml bring-up: metroid.xml parses cleanly");
    if (parsed.Status != XmlParseStatus::Ok) {
        std::printf("    %s\n", parsed.Detail.c_str());
        return;
    }
    const GameDefinition& def = parsed.Definition;
    Check(def.Machine == "NINTENDO_FDS" && def.Variant == "FDS_BASE", "xml bring-up: machine NINTENDO_FDS / variant FDS_BASE");

    std::vector<RequiredResource> required;
    for (const RomResourceDefinition& rom : def.Resources) {
        RequiredResource r;
        r.LogicalName = rom.Name;
        r.AcceptedNames = {rom.File};
        r.SizeBytes = rom.SizeBytes;
        r.Crc32 = rom.Crc32;
        r.Sha1 = rom.Sha1;
        if (!def.Archive.empty()) r.Archive = def.Archive;
        required.push_back(std::move(r));
    }
    const ResourceResolver resolver{std::filesystem::path(romRootEnv)};
    ResourceResolutionResult res = resolver.Resolve(required);
    Check(res.Ok, "xml bring-up: the disk resolves under CHIPESTRA_ROM_ROOT (archive \"Metroid\")");
    if (!res.Ok) {
        for (const auto& f : res.Failures)
            std::printf("    resolve fail '%s' code %d (%s)\n", f.LogicalName.c_str(), static_cast<int>(f.Result), f.Detail.c_str());
        return;
    }
    const std::string driverXml = ExtractDriverElementXml(*text);
    Check(!driverXml.empty(), "xml bring-up: the <driver> element is present");
    res.Resources.Add(ResolvedResource("driver_xml", std::vector<std::uint8_t>(driverXml.begin(), driverXml.end())));

    auto create = [&]() {
        std::unique_ptr<IMachine> m = DefaultMachineRegistry().Create(MachineId(def.Machine), VariantId(def.Variant), res.Resources);
        if (m) m->Initialize();
        return m;
    };

    // Every track of the XML: it must sound, and the sound routines must not need the BIOS or the disk registers.
    std::uint64_t totalDisk = 0, totalBios = 0, totalPicture = 0, totalController = 0;
    std::size_t tracksChecked = 0, silentTracks = 0;
    for (const TrackDefinition& track : def.Tracks) {
        if (track.IsControlCommand) continue;
        std::unique_ptr<IMachine> base = create();
        if (!base) {
            Check(false, "xml bring-up: the Machine is created from the XML's own settings");
            return;
        }
        FdsMachine& machine = static_cast<FdsMachine&>(*base);
        MusicCommand command;
        command.Kind = MusicCommandKind::Raw;
        command.Value = static_cast<std::uint8_t>(std::strtoul(track.Command.c_str(), nullptr, 0));
        command.Auxiliary = track.Auxiliary;
        if (!machine.AcceptsCommand(command)) {
            Check(false, ("track " + track.Id + ": accepted by the Machine").c_str());
            continue;
        }
        machine.SendCommand(command);
        LogicalChannelSink sink;
        machine.Render(EmuDuration(kTicksPerFrame * 150), &sink);  // 2.5 seconds
        float peak = 0.0f;
        for (const LogicalChannel& channel : machine.LogicalChannels()) {
            const LogicalChannelBuffer& b = sink.ChannelBuffer(channel.Id);
            peak = std::max(peak, PeakOf(std::vector<float>(b.Data(), b.Data() + b.Size())));
        }
        const FdsMachine::BusStats stats = machine.BusStatsForTest();
        totalDisk += stats.diskReads + stats.diskWrites;
        totalBios += stats.biosReads + stats.biosWrites;
        totalPicture += stats.ppuReads + stats.ppuWrites;
        totalController += stats.inputReads + stats.inputWrites;
        ++tracksChecked;
        if (peak <= 0.001f) {
            ++silentTracks;
            std::printf("    silent in the first 2.5 s: %s (%s)\n", track.Id.c_str(), track.Title.c_str());
        }
    }
    Check(tracksChecked > 30, "xml bring-up: the XML lists the tracks (more than 30 besides Music Stop)");
    std::printf("  %zu tracks run for 2.5 s each: disk-register accesses %llu, BIOS-area accesses %llu, picture-chip accesses %llu, controller accesses %llu\n",
                tracksChecked, static_cast<unsigned long long>(totalDisk), static_cast<unsigned long long>(totalBios),
                static_cast<unsigned long long>(totalPicture), static_cast<unsigned long long>(totalController));
    Check(totalDisk == 0 && totalBios == 0, "the sound routines never touch the disk registers or the BIOS area");
    Check(silentTracks == 0, "every track of the XML sounds within 2.5 seconds");

    // The Machine against the real game: for every track, the sound-register writes after its request must be the
    // writes the real game's own sound driver made (a recording of the game running, see the reference folder).
    const char* referenceEnv = GetEnvVar("CHIPESTRA_REFERENCE_DIR");
    if (referenceEnv == nullptr || referenceEnv[0] == '\0') {
        std::printf("  SKIP: CHIPESTRA_REFERENCE_DIR is not set - the sound-register writes are not compared with the real game's\n");
        return;
    }
    const std::filesystem::path recordDir = std::filesystem::path(referenceEnv) / "fds" / "metroid" / "real_game";
    Check(std::filesystem::is_directory(recordDir), "reference records: <CHIPESTRA_REFERENCE_DIR>/fds/metroid/real_game exists");
    if (!std::filesystem::is_directory(recordDir)) return;

    constexpr std::uint64_t kCompareFrames = 420;
    // Differences found and explained: a frame may differ only in these registers, in at most `maxFrames` frames.
    struct Allowance {
        std::vector<std::uint16_t> registers;
        std::size_t maxFrames;
        const char* reason;
    };
    // The driver keeps each channel's last period in RAM ($0600/$0601 pulse 1, $0604/$0605 pulse 2, $0608/$0609
    // triangle: the values it writes to $4002/3, $4006/7, $400A/B). When a sound starts, the real game still holds the
    // previous sound's periods and writes them in the first frame (and at the start of a loop); a fresh start has zeros.
    const Allowance periods = {{0x4002, 0x4003, 0x4006, 0x4007, 0x400A, 0x400B}, 4,
                               "the previous sound's channel periods are still in the driver's RAM"};
    // The ending's FDS channel modulation/volume variables ($0618-$061D) hold values the previous sound left there.
    const Allowance fdsLeftover = {{0x4080, 0x4085, 0x4086, 0x4087}, 3,
                                   "the FDS channel's variables still hold the previous sound's values"};

    std::size_t compared = 0, exact = 0, toleratedTracks = 0, toleratedFrames = 0;
    bool allMatch = true;
    for (const TrackDefinition& track : def.Tracks) {
        if (track.IsControlCommand) continue;
        auto records = ReadReferenceLog(recordDir / (track.Id + ".txt"));
        Check(records.has_value(), ("reference record exists for track " + track.Id).c_str());
        if (!records) {
            allMatch = false;
            continue;
        }
        std::unique_ptr<IMachine> base = create();
        FdsMachine& machine = static_cast<FdsMachine&>(*base);
        std::vector<LoggedWrite> writes;
        machine.SetRegisterWriteObserverForTest([&](std::uint64_t frame, std::uint16_t address, std::uint8_t v) {
            if (frame < kCompareFrames && (address <= 0x4013 || address == 0x4015 || address >= 0x4040)) writes.push_back({frame, address, v});
        });
        MusicCommand command;
        command.Kind = MusicCommandKind::Raw;
        command.Value = static_cast<std::uint8_t>(std::strtoul(track.Command.c_str(), nullptr, 0));
        command.Auxiliary = track.Auxiliary;
        machine.SendCommand(command);
        machine.Render(EmuDuration(kTicksPerFrame * kCompareFrames), nullptr);
        ++compared;

        // Frame by frame: the writes of one frame in order.
        std::vector<std::vector<std::pair<std::uint16_t, std::uint8_t>>> got(kCompareFrames), want(kCompareFrames);
        for (const LoggedWrite& w : writes) got[w.frame].emplace_back(w.address, w.value);
        for (const LoggedWrite& w : records->writes)
            if (w.frame < kCompareFrames) want[w.frame].emplace_back(w.address, w.value);
        const Allowance& allowance = track.Id == "END_684_20" ? fdsLeftover : periods;
        std::size_t differing = 0;
        bool explained = true;
        for (std::size_t f = 0; f < kCompareFrames; ++f) {
            if (got[f] == want[f]) continue;
            ++differing;
            for (const auto* list : {&got[f], &want[f]}) {
                const auto& other = list == &got[f] ? want[f] : got[f];
                for (const auto& w : *list) {
                    const bool inOther = std::find(other.begin(), other.end(), w) != other.end();
                    if (!inOther && std::find(allowance.registers.begin(), allowance.registers.end(), w.first) == allowance.registers.end()) explained = false;
                }
            }
        }
        if (differing == 0) {
            ++exact;
        } else if (explained && differing <= allowance.maxFrames) {
            ++toleratedTracks;
            toleratedFrames += differing;
            std::printf("    %s: %zu of %llu frames differ, only in period/variable registers (%s)\n", track.Id.c_str(), differing,
                        static_cast<unsigned long long>(kCompareFrames), allowance.reason);
        } else {
            allMatch = false;
            std::printf("    DIFFERENT: track %s: %zu of %llu frames differ%s\n", track.Id.c_str(), differing,
                        static_cast<unsigned long long>(kCompareFrames), explained ? " (more than allowed)" : "");
        }
    }
    std::printf("  %zu tracks compared with the real game over %llu frames: %zu exact, %zu with explained differences (%zu frames)\n", compared,
                static_cast<unsigned long long>(kCompareFrames), exact, toleratedTracks, toleratedFrames);
    Check(compared > 30, "reference records: more than 30 tracks were compared");
    Check(allMatch, "every track writes the sound registers as the real game does (frame, address, value), apart from the explained differences");
}
