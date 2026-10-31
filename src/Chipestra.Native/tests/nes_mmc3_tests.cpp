// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// MMC3 tests. The synthetic program is hand-written; what each routine must leave behind is worked out here from
// the instructions and the banks the Machine is asked to map, not read back from the Machine.

#include "nes_mmc3_tests.h"

#include "default_machine_catalog.h"
#include "definition_xml_locator.h"
#include "devices/nintendo/nes/nes_cartridge.h"
#include "devices/nintendo/nes/nes_driver_config.h"
#include "devices/nintendo/nes/nes_machine.h"
#include "devices/nintendo/nes/nes_mmc3.h"
#include "machine_registry.h"
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
#include <vector>

namespace {

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

// 8 banks of 8 KB. Bank k holds the marker 0xB0+k in bytes $100-$1FF; the routines are in bank 5 ($A000 when R7 = 5).
constexpr std::uint16_t kRoutineA = 0xA000;
constexpr std::uint16_t kRoutineB = 0xA080;

NesCartridge BuildMmc3Cartridge() {
    NesCartridge cart;
    cart.mapper = 4;
    cart.prg.assign(8 * 0x2000, 0xEA);
    for (std::size_t bank = 0; bank < 8; ++bank)
        for (std::size_t i = 0x100; i < 0x200; ++i) cart.prg[bank * 0x2000 + i] = static_cast<std::uint8_t>(0xB0 + bank);
    // Routine A: record what the banks look like during the call, use the WRAM, record X.
    //   LDA $C100; STA $0300; STX $0302; LDA #$5A; STA $6000; LDA $6000; STA $0301
    //   LDA $A100; STA $0303; LDA $E100; STA $0304; RTS
    const std::vector<std::uint8_t> a = {0xAD, 0x00, 0xC1, 0x8D, 0x00, 0x03, 0x8E, 0x02, 0x03, 0xA9, 0x5A, 0x8D, 0x00, 0x60,
                                         0xAD, 0x00, 0x60, 0x8D, 0x01, 0x03, 0xAD, 0x00, 0xA1, 0x8D, 0x03, 0x03, 0xAD, 0x00,
                                         0xE1, 0x8D, 0x04, 0x03, 0x60};
    // Routine B: when $0310 is set (once), start the DMC on $C100 (241 bytes, the fastest rate) and then burn
    // about 3800 CPU cycles, still inside the call.
    const std::vector<std::uint8_t> b = {0xAD, 0x10, 0x03, 0xF0, 0x26, 0xAD, 0x11, 0x03, 0xD0, 0x21, 0xEE, 0x11, 0x03,
                                         0xA9, 0x0F, 0x8D, 0x10, 0x40, 0xA9, 0x04, 0x8D, 0x12, 0x40, 0xA9, 0x0F, 0x8D,
                                         0x13, 0x40, 0xA9, 0x1F, 0x8D, 0x15, 0x40, 0xA2, 0x03, 0xA0, 0x00, 0x88, 0xD0,
                                         0xFD, 0xCA, 0xD0, 0xF8, 0x60};
    std::copy(a.begin(), a.end(), cart.prg.begin() + 5 * 0x2000);
    std::copy(b.begin(), b.end(), cart.prg.begin() + 5 * 0x2000 + 0x80);
    return cart;
}

NesDriverConfig BuildMmc3Driver() {
    NesDriverConfig cfg;
    cfg.ramClear = std::pair<std::uint16_t, std::uint16_t>{0x0000, 0x07FF};
    cfg.frame = {kRoutineA, kRoutineB};
    cfg.requests = {0x0310};
    cfg.prgBanksCall = NesDriverConfig::PrgBanks{4, 5};
    cfg.prgBanksIdle = NesDriverConfig::PrgBanks{2, 3};
    return cfg;
}

}  // namespace

void Mmc3PrgMapsBanksLikeTheOriginal() {
    std::printf("Mmc3PrgMapsBanksLikeTheOriginal\n");
    Mmc3Prg prg(16);
    auto bank = [&](std::uint16_t address) { return prg.Offset(address) / 0x2000; };
    Check(bank(0x8000) == 0 && bank(0xA000) == 1 && bank(0xC000) == 14 && bank(0xE000) == 15, "power-on: R6 = 0, R7 = 1, mode 0");
    prg.Write(0x8000, 0x06);
    prg.Write(0x8001, 5);
    prg.Write(0x8000, 0x07);
    prg.Write(0x8001, 9);
    Check(bank(0x8000) == 5 && bank(0xA000) == 9 && bank(0xC000) == 14 && bank(0xE000) == 15, "mode 0: R6 at $8000, R7 at $A000");
    prg.Write(0x8000, 0x46);  // select R6 and switch to mode 1
    Check(bank(0x8000) == 14 && bank(0xA000) == 9 && bank(0xC000) == 5 && bank(0xE000) == 15, "mode 1: second-to-last bank at $8000, R6 at $C000");
    prg.Write(0x9FFF, 0x0C);  // an odd address in $8000-$9FFF is the bank data register too
    Check(bank(0xC000) == 12, "any odd address of $8000-$9FFF writes the bank data");
    prg.Write(0x8000, 0x47);
    prg.Write(0x9FFF, 0x23);  // 35 wraps in 16 banks
    Check(bank(0xA000) == 3, "a bank number wraps around the number of banks");
    prg.Write(0xA000, 0x00);
    prg.Write(0xA001, 0x00);
    prg.Write(0xC000, 0x00);
    prg.Write(0xE001, 0x00);
    Check(bank(0xC000) == 12 && bank(0xA000) == 3, "$A000-$FFFF registers (mirroring, WRAM protect, IRQ) change no PRG bank");
    prg.Write(0x8000, 0x40);  // CHR select (register 0, PRG mode 1 kept): not a PRG bank
    prg.Write(0x8001, 0x33);
    Check(bank(0xC000) == 12 && bank(0xA000) == 3, "CHR registers change no PRG bank");
    prg.Reset();
    Check(bank(0x8000) == 0 && bank(0xC000) == 14, "Reset returns to the power-on state");
    prg.SetState(0x0A, 0x0D, true);
    Check(bank(0xC000) == 10 && bank(0xA000) == 13 && bank(0x8000) == 14, "SetState sets R6, R7 and the mode");
}

void NesMmc3CartridgeAndDriverSettings() {
    std::printf("NesMmc3CartridgeAndDriverSettings\n");
    auto image = [](std::size_t prgBanks16, std::uint8_t flags6) {
        std::vector<std::uint8_t> file(16 + prgBanks16 * 16384 + 8192, 0);
        file[0] = 'N';
        file[1] = 'E';
        file[2] = 'S';
        file[3] = 0x1A;
        file[4] = static_cast<std::uint8_t>(prgBanks16);
        file[5] = 1;
        file[6] = flags6;
        return file;
    };
    auto parses = [](const std::vector<std::uint8_t>& file) {
        try {
            const NesCartridge cart = ParseInesCartridge(file.data(), file.size());
            return cart.mapper == 4 ? cart.prg.size() : std::size_t{0};
        } catch (const std::runtime_error&) {
            return std::size_t{0};
        }
    };
    Check(parses(image(4, 0x40)) == 4 * 16384, "an MMC3 image of 64 KB parses");
    Check(parses(image(16, 0x40)) == 16 * 16384, "an MMC3 image of 256 KB parses");
    Check(parses(image(3, 0x40)) == 0, "an MMC3 PRG size that is not a power of two is rejected");
    Check(parses(image(33, 0x40)) == 0, "an MMC3 PRG size above 512 KB is rejected");

    NesDriverConfig cfg;
    std::string error;
    Check(ParseNesDriverConfig("<driver frame=\"0xA000\" prg_banks_call=\"0x1D 0x1C\" prg_banks_idle=\"10, 26\" "
                               "requests=\"0x04F1 0x04F5\"/>",
                               cfg, &error),
          "prg_banks_call and prg_banks_idle parse");
    Check(cfg.prgBanksCall && cfg.prgBanksCall->r6 == 0x1D && cfg.prgBanksCall->r7 == 0x1C, "the call banks");
    Check(cfg.prgBanksIdle && cfg.prgBanksIdle->r6 == 10 && cfg.prgBanksIdle->r7 == 26, "the idle banks (decimal)");
    auto rejects = [](const std::string& xml) {
        NesDriverConfig c;
        std::string e;
        return !ParseNesDriverConfig(xml, c, &e) && !e.empty();
    };
    Check(rejects("<driver prg_banks_call=\"0x1D 0x1C\"/>"), "prg_banks_call without prg_banks_idle is rejected");
    Check(rejects("<driver prg_banks_call=\"0x1D\" prg_banks_idle=\"1 2\"/>"), "one bank number is rejected");
    Check(rejects("<driver prg_banks_call=\"0x1D 0x100\" prg_banks_idle=\"1 2\"/>"), "a bank number above 255 is rejected");
}

void NesMachineMmc3BankingWramAndDmcReads() {
    std::printf("NesMachineMmc3BankingWramAndDmcReads\n");
    auto machine = std::make_unique<NesMachine>(BuildMmc3Cartridge(), BuildMmc3Driver());
    std::vector<std::pair<std::uint64_t, std::uint8_t>> dmc;  // frame, byte
    machine->SetDmcReadObserverForTest([&](std::uint64_t frame, std::uint16_t address, std::uint8_t value) {
        if (address >= 0xC100 && address < 0xC200) dmc.emplace_back(frame, value);
    });
    machine->Initialize();

    // The start-up frame ran the routines once with the call banks mapped and X = 0.
    Check(machine->RamForTest(0x0300) == 0xB4, "during the call $C000 shows the call R6 bank (4)");
    Check(machine->RamForTest(0x0303) == 0xB5, "during the call $A000 shows the call R7 bank (5)");
    Check(machine->RamForTest(0x0304) == 0xB7, "the last bank is fixed at $E000");
    Check(machine->RamForTest(0x0302) == 0, "the routines are called with X = 0");
    Check(machine->RamForTest(0x0301) == 0x5A && machine->WramForTest(0) == 0x5A, "the WRAM at $6000 holds what is written to it");
    // Between the calls the idle banks are mapped.
    Check(machine->PrgOffsetForTest(0xC000) / 0x2000 == 2 && machine->PrgOffsetForTest(0xA000) / 0x2000 == 3 &&
              machine->PrgOffsetForTest(0x8000) / 0x2000 == 6 && machine->PrgOffsetForTest(0xE000) / 0x2000 == 7,
          "between the calls the idle banks are mapped (mode 1)");

    // Commands: a request number selects the address; there is no bgm variable here.
    MusicCommand none;
    none.Kind = MusicCommandKind::Raw;
    none.Value = 1;
    MusicCommand first = none;
    first.Auxiliary = 0x10;
    MusicCommand second = none;
    second.Auxiliary = 0x20;
    MusicCommand withSet = none;
    withSet.Auxiliary = 0x11;
    Check(!machine->AcceptsCommand(none), "a raw command without a request is refused when the driver has no bgm variable");
    Check(machine->AcceptsCommand(first), "a raw command with request 1 is accepted");
    Check(!machine->AcceptsCommand(second), "a request number beyond the list is refused");
    Check(!machine->AcceptsCommand(withSet), "a set number is refused (disk games only)");

    machine->SendCommand(first);
    Check(machine->RamForTest(0x0310) == 1, "the request value is written to the request address");
    LogicalChannelSink sink;
    machine->Render(EmuDuration(5 * 3200 + 1600), &sink);  // 5.5 frames (a frame is about 3200 ticks)

    // The DMC started inside the call: the bytes read during the ~3800 cycles still inside the call come from the
    // call bank (4), all the others from the idle bank (2). 241 bytes at 432 cycles each need about 3.5 frames.
    std::size_t fromCall = 0, fromIdle = 0, other = 0, firstIdle = dmc.size();
    for (std::size_t i = 0; i < dmc.size(); ++i) {
        const std::uint8_t value = dmc[i].second;
        if (value == 0xB4) {
            ++fromCall;
        } else if (value == 0xB2) {
            ++fromIdle;
            firstIdle = std::min(firstIdle, i);
        } else {
            ++other;
        }
    }
    std::printf("  DMC bytes: %zu in total, %zu through the call banks, %zu through the idle banks, %zu other\n", dmc.size(), fromCall,
                fromIdle, other);
    Check(dmc.size() == 241 && other == 0, "the DMC read all 241 bytes of its sample");
    Check(fromCall >= 6 && fromCall <= 12, "the bytes the DMC read while the call banks were mapped come from bank 4 (about 3800 cycles of them, plus a few in later calls)");
    Check(firstIdle >= 6 && fromIdle == dmc.size() - fromCall, "the first bytes come from the call bank and every other byte from the idle bank 2");

    // Reset puts the RAM and the mapper back.
    MusicCommand stop;
    stop.Kind = MusicCommandKind::Stop;
    machine->SendCommand(stop);
    Check(machine->RamForTest(0x0310) == 0 && machine->RamForTest(0x0311) == 0, "Music Stop (reset) clears the RAM");
    Check(machine->PrgOffsetForTest(0xC000) / 0x2000 == 2, "Music Stop returns to the idle banks");
}

void Smb3ViaDefinitionXmlRealRomMatchesTheRealGame() {
    std::printf("Smb3ViaDefinitionXmlRealRomMatchesTheRealGame\n");
    const char* romRootEnv = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRootEnv == nullptr || romRootEnv[0] == '\0') {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is not set - real-ROM bring-up not exercised\n");
        return;
    }
    const auto path = FindDefinitionXml("smb3.xml");
    Check(path.has_value(), "smb3: smb3.xml is present (run from the repo root)");
    if (!path) return;
    const auto text = ReadFileText(*path);
    Check(text.has_value(), "smb3: file is readable");
    if (!text) return;
    const XmlParseResult parsed = ParseGameDefinition(*text);
    Check(parsed.Status == XmlParseStatus::Ok, "smb3: smb3.xml parses cleanly");
    if (parsed.Status != XmlParseStatus::Ok) return;
    const GameDefinition& def = parsed.Definition;
    Check(def.Machine == "NINTENDO_NES" && def.Variant == "NES_BASE", "smb3: machine NINTENDO_NES / variant NES_BASE");

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
    Check(res.Ok, "smb3: the cartridge resolves under CHIPESTRA_ROM_ROOT (archive \"Super Mario Bros. 3 (Japan)\")");
    if (!res.Ok) return;
    const std::string driverXml = ExtractDriverElementXml(*text);
    Check(!driverXml.empty(), "smb3: the <driver> element is present");
    res.Resources.Add(ResolvedResource("driver_xml", std::vector<std::uint8_t>(driverXml.begin(), driverXml.end())));

    auto create = [&]() {
        std::unique_ptr<IMachine> m = DefaultMachineRegistry().Create(MachineId(def.Machine), VariantId(def.Variant), res.Resources);
        if (m) m->Initialize();
        return m;
    };

    const auto stopTrack = std::find_if(def.Tracks.begin(), def.Tracks.end(), [](const TrackDefinition& t) { return t.IsControlCommand; });
    Check(stopTrack != def.Tracks.end(), "smb3: the XML has a Music Stop control track");

    NesDriverConfig driverConfig;
    std::string driverError;
    Check(ParseNesDriverConfig(driverXml, driverConfig, &driverError), "smb3: the <driver> element parses");
    // The auxiliary byte of a command for a request address: its position in <driver requests>, from 1, in the high nibble.
    auto auxiliaryOf = [&](unsigned address) -> std::uint8_t {
        for (std::size_t i = 0; i < driverConfig.requests.size(); ++i)
            if (driverConfig.requests[i] == address) return static_cast<std::uint8_t>((i + 1) << 4);
        return 0;
    };

    // The recordings of the real game live in nes/smb3/real_game below CHIPESTRA_REFERENCE_DIR: one file per request
    // (req_<address>_<value>.txt: "<frame> <address> <value>", frames counted from the request, the sound engine's APU
    // writes) and the DMC reads of the world-map song. The requests below are every value the engine plays; the XML's
    // tracks are not what is compared (a track can be removed or renamed), but each track must be one of them.
    struct Request {
        unsigned address;
        unsigned value;
    };
    std::vector<Request> requests;
    auto addRequests = [&](unsigned address, std::initializer_list<unsigned> values) {
        for (unsigned v : values) requests.push_back({address, v});
    };
    for (unsigned v = 0x01; v <= 0x0F; ++v) requests.push_back({0x04F5, v});
    for (unsigned v = 0x10; v <= 0xD0; v += 0x10) requests.push_back({0x04F5, v});
    addRequests(0x04F4, {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40});
    addRequests(0x04F1, {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x80});
    addRequests(0x04F2, {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80});
    addRequests(0x04F3, {0x01, 0x02, 0x04, 0x08, 0x10, 0x80});
    addRequests(0x04F6, {0x01, 0x02, 0x04, 0x08, 0x10, 0x80});
    for (const TrackDefinition& track : def.Tracks) {
        if (track.IsControlCommand) continue;
        const unsigned value = static_cast<unsigned>(std::strtoul(track.Command.c_str(), nullptr, 0));
        const bool known = std::any_of(requests.begin(), requests.end(), [&](const Request& r) {
            return auxiliaryOf(r.address) == track.Auxiliary && r.value == value;
        });
        Check(known, (track.Id + ": is one of the requests the engine plays").c_str());
    }

    const char* referenceRoot = GetEnvVar("CHIPESTRA_REFERENCE_DIR");
    const bool haveReference = referenceRoot != nullptr && referenceRoot[0] != '\0';
    const std::filesystem::path referenceDir =
        haveReference ? std::filesystem::path(referenceRoot) / "nes" / "smb3" / "real_game" : std::filesystem::path();
    if (!haveReference) std::printf("  SKIP: CHIPESTRA_REFERENCE_DIR is not set - the APU write sequences are not compared with the real game\n");

    constexpr std::uint64_t kFrames = 600;
    const std::uint64_t ticks = kFrames * 59561ull / 2 * 192000ull / NesMachine::kCpuClockHz + 1;
    struct W {
        std::uint64_t frame;
        unsigned address, value;
        bool operator==(const W& o) const { return frame == o.frame && address == o.address && value == o.value; }
    };

    std::size_t compared = 0;
    for (const Request& request : requests) {
        char name[32];
        std::snprintf(name, sizeof(name), "$%04X=%02X", request.address, request.value);
        const std::string label = name;
        std::unique_ptr<IMachine> base = create();
        Check(base != nullptr, (label + ": the Machine is created from the XML's own settings").c_str());
        if (!base) return;
        NesMachine& machine = static_cast<NesMachine&>(*base);
        std::vector<W> writes;
        machine.SetApuWriteObserverForTest([&](std::uint64_t frame, std::uint8_t reg, std::uint8_t value) {
            if (frame < kFrames) writes.push_back({frame, 0x4000u + reg, value});
        });
        MusicCommand command;
        command.Kind = MusicCommandKind::Raw;
        command.Value = static_cast<std::uint8_t>(request.value);
        command.Auxiliary = auxiliaryOf(request.address);
        Check(machine.AcceptsCommand(command), (label + ": accepted by the Machine").c_str());
        machine.SendCommand(command);
        LogicalChannelSink sink;
        machine.Render(EmuDuration(ticks), &sink);
        const NesMachine::BusStats stats = machine.BusStatsForTest();
        Check(stats.ppuReads + stats.ppuWrites + stats.inputReads == 0,
              (label + ": the sound engine does not touch the picture chip or the controllers").c_str());

        if (!haveReference) continue;
        char fileName[48];
        std::snprintf(fileName, sizeof(fileName), "req_%04X_%02X.txt", request.address, request.value);
        const std::filesystem::path log = referenceDir / fileName;
        if (!std::filesystem::exists(log)) {
            Check(false, (label + ": the reference recording exists").c_str());
            continue;
        }
        std::vector<W> expected;
        std::ifstream file(log);
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            unsigned frame = 0, address = 0, value = 0;
            std::istringstream fields(line);
            fields >> std::dec >> frame >> std::hex >> address >> value;
            if (!fields.fail() && frame < kFrames) expected.push_back({frame, address, value});
        }
        std::size_t i = 0;
        while (i < expected.size() && i < writes.size() && expected[i] == writes[i]) ++i;
        const bool same = i == expected.size() && i == writes.size();
        if (!same) {
            std::printf("    %s: first difference at write %zu of %zu (recording) / %zu (Chipestra)\n", label.c_str(), i, expected.size(),
                        writes.size());
            if (i < expected.size())
                std::printf("      recording frame %llu %04X=%02X\n", static_cast<unsigned long long>(expected[i].frame), expected[i].address,
                            expected[i].value);
            if (i < writes.size())
                std::printf("      Chipestra frame %llu %04X=%02X\n", static_cast<unsigned long long>(writes[i].frame), writes[i].address,
                            writes[i].value);
        }
        Check(same, (label + ": all APU writes of the first 600 frames equal the real game's (frame, address, value)").c_str());
        ++compared;
    }
    if (haveReference) {
        std::printf("  %zu requests compared with the recording of the real game\n", compared);
        Check(compared == requests.size(), "smb3: every request was compared with a recording");
    }

    // The world-map songs read drum samples from the bank that is mapped at $C000 between the engine's calls
    // (bank 10 on the map). The recording is of the real game on the map (song 3, which uses a sample at $DB80).
    // Chipestra reads every DMC byte through the banks mapped at that cycle; the emulator the recording was made
    // with reads all the bytes since its previous APU access through the banks of the moment it catches up (the
    // start of the sound engine's call, bank 29), so about a quarter of the recorded bytes come from another bank.
    if (haveReference) {
        std::vector<std::pair<unsigned, unsigned>> recorded;  // address, value
        std::ifstream file(referenceDir / "dmc_map_song03.txt");
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            unsigned frame = 0, address = 0, value = 0;
            std::istringstream fields(line);
            fields >> std::dec >> frame >> std::hex >> address >> value;
            if (!fields.fail()) recorded.emplace_back(address, value);
        }
        Check(!recorded.empty(), "smb3: the DMC recording is present");
        if (!recorded.empty()) {
            std::unique_ptr<IMachine> base = create();
            NesMachine& machine = static_cast<NesMachine&>(*base);
            std::vector<std::pair<unsigned, unsigned>> ours;
            machine.SetDmcReadObserverForTest([&](std::uint64_t frame, std::uint16_t address, std::uint8_t value) {
                if (frame < 440 && address >= 0xD800 && address <= 0xDFFF) ours.emplace_back(address, value);
            });
            MusicCommand command;
            command.Kind = MusicCommandKind::Raw;
            command.Value = 0x03;  // the song the recording was made with: $04F5 = 03
            command.Auxiliary = auxiliaryOf(0x04F5);
            machine.SendCommand(command);
            LogicalChannelSink sink;
            machine.Render(EmuDuration(440ull * 59561 / 2 * 192000ull / NesMachine::kCpuClockHz + 1), &sink);
            const NesCartridge cart = ParseInesCartridge(res.Resources.Find("cart")->Data(), res.Resources.Find("cart")->Size());
            std::size_t sameAddress = 0, sameValue = 0, fromBank10 = 0;
            const std::size_t n = std::min(recorded.size(), ours.size());
            for (std::size_t i = 0; i < n; ++i) {
                if (recorded[i].first == ours[i].first) ++sameAddress;
                if (recorded[i] == ours[i]) ++sameValue;
                if (ours[i].second == cart.prg[10 * 0x2000 + (ours[i].first - 0xC000)]) ++fromBank10;
            }
            std::printf("  DMC reads in $D800-$DFFF: recording %zu, Chipestra %zu; same address %zu, same address and byte %zu, byte of bank 10 %zu\n",
                        recorded.size(), ours.size(), sameAddress, sameValue, fromBank10);
            Check(ours.size() == recorded.size() && sameAddress == n, "smb3: the DMC reads the same addresses in the same order as the recording");
            Check(sameValue * 100 >= n * 75,
                  "smb3: at least 75% of the DMC bytes equal the recording (the rest are read through another bank by the recording's emulator)");
            Check(fromBank10 * 100 >= n * 98, "smb3: Chipestra's DMC bytes come from the idle bank (bank 10) except for the few read during the call");
        }
    }

    if (stopTrack != def.Tracks.end()) {
        std::unique_ptr<IMachine> m = create();
        LogicalChannelSink s;
        MusicCommand song;
        song.Kind = MusicCommandKind::Raw;
        song.Value = 0x10;
        song.Auxiliary = auxiliaryOf(0x04F5);
        m->SendCommand(song);
        m->Render(EmuDuration(192000), &s);
        s.ClearAll();
        MusicCommand stop;
        stop.Kind = MusicCommandKind::Raw;
        stop.Value = 0;
        stop.Auxiliary = stopTrack->Auxiliary;  // the XML's own Music Stop
        m->SendCommand(stop);
        float peak = 0;
        m->Render(EmuDuration(192000), &s);
        for (const LogicalChannel& channel : m->LogicalChannels()) {
            const LogicalChannelBuffer& b = s.ChannelBuffer(channel.Id);
            for (std::size_t i = 0; i < b.Size(); ++i) peak = std::max(peak, std::abs(b.Data()[i]));
        }
        Check(peak < 1e-6f, "smb3: Music Stop leaves every channel silent");
    }
}
