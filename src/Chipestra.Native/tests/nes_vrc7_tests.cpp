// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// VRC7 tests. The synthetic program is hand-written; what each routine must leave behind is worked out here from
// the instructions and the banks the Machine is asked to map, not read back from the Machine.

#include "nes_vrc7_tests.h"

#include "default_machine_catalog.h"
#include "definition_xml_locator.h"
#include "devices/nintendo/nes/nes_cartridge.h"
#include "devices/nintendo/nes/nes_driver_config.h"
#include "devices/nintendo/nes/nes_machine.h"
#include "devices/nintendo/nes/nes_vrc7.h"
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
#include <tuple>
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

void WriteBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// A made-up 144-byte instrument table (not a real instrument set).
std::vector<std::uint8_t> MadeUpTable() {
    std::vector<std::uint8_t> t(Vrc7SoundDeviceAdapter::kInstrumentTableSize);
    for (std::size_t i = 0; i < t.size(); ++i) t[i] = static_cast<std::uint8_t>(i * 37 + 11);
    return t;
}

// 64 banks of 8 KB (512 KB). Bank k holds the marker 0x40+k in bytes $100-$1FF.
//   frame routine, at $8000 of bank 4 (the first call bank): record what $A000+$100, $C000+$100 and $E000+$100
//   show, then (once) program the user instrument and start channel 0.
//   request routine, at $8200 of the same bank: record A, count the calls, put A into WRAM $6000 and start
//   channel 3.
constexpr std::uint16_t kFrameRoutine = 0x8000;
constexpr std::uint16_t kRequestRoutine = 0x8200;
constexpr std::uint8_t kCallBank0 = 4, kCallBank1 = 5, kCallBank2 = 6;
constexpr std::uint8_t kIdleBank0 = 1, kIdleBank1 = 2, kIdleBank2 = 3;
constexpr std::size_t kBankCount = 64;

void Sound(std::vector<std::uint8_t>& code, std::uint8_t reg, std::uint8_t value) {
    const std::uint8_t bytes[] = {0xA9, reg, 0x8D, 0x10, 0x90, 0xA9, value, 0x8D, 0x30, 0x90};  // LDA/STA $9010, LDA/STA $9030
    code.insert(code.end(), std::begin(bytes), std::end(bytes));
}

NesCartridge BuildVrc7Cartridge() {
    NesCartridge cart;
    cart.mapper = 85;
    cart.prg.assign(kBankCount * 0x2000, 0xEA);
    for (std::size_t bank = 0; bank < kBankCount; ++bank)
        for (std::size_t i = 0x100; i < 0x200; ++i) cart.prg[bank * 0x2000 + i] = static_cast<std::uint8_t>(0x40 + bank);
    std::vector<std::uint8_t> sound;
    const std::uint8_t patch[8] = {0x21, 0x21, 0x20, 0x00, 0xF0, 0xF0, 0x0F, 0x07};
    for (std::uint8_t i = 0; i < 8; ++i) Sound(sound, i, patch[i]);
    Sound(sound, 0x30, 0x02);  // channel 0: user instrument, volume 2
    Sound(sound, 0x10, 0x80);
    Sound(sound, 0x20, 0x18);  // key on, block 4
    std::vector<std::uint8_t> frame = {
        0xAD, 0x00, 0xA1, 0x8D, 0x00, 0x03,  // LDA $A100; STA $0300
        0xAD, 0x00, 0xC1, 0x8D, 0x01, 0x03,  // LDA $C100; STA $0301
        0xAD, 0x00, 0xE1, 0x8D, 0x02, 0x03,  // LDA $E100; STA $0302
        0x8E, 0x03, 0x03,                    // STX $0303
        0xAD, 0x10, 0x03,                    // LDA $0310
        0xD0, static_cast<std::uint8_t>(3 + sound.size()),  // BNE done
        0xEE, 0x10, 0x03};                   // INC $0310
    frame.insert(frame.end(), sound.begin(), sound.end());
    frame.push_back(0x60);  // done: RTS
    std::vector<std::uint8_t> request = {
        0x8D, 0x04, 0x03,  // STA $0304
        0xEE, 0x05, 0x03,  // INC $0305
        0x8D, 0x00, 0x60};  // STA $6000 (WRAM)
    Sound(request, 0x33, 0x02);
    Sound(request, 0x13, 0x80);
    Sound(request, 0x23, 0x18);
    request.push_back(0x60);
    std::copy(frame.begin(), frame.end(), cart.prg.begin() + kCallBank0 * 0x2000);
    std::copy(request.begin(), request.end(), cart.prg.begin() + kCallBank0 * 0x2000 + 0x200);
    return cart;
}

NesDriverConfig BuildVrc7Driver(std::uint8_t line = 4) {
    NesDriverConfig cfg;
    cfg.ramClear = std::pair<std::uint16_t, std::uint16_t>{0x0000, 0x07FF};
    cfg.frame = {kFrameRoutine};
    cfg.requestCall = kRequestRoutine;
    cfg.prgBanksCall = NesDriverConfig::PrgBanks{kCallBank0, kCallBank1, kCallBank2, 3};
    cfg.prgBanksIdle = NesDriverConfig::PrgBanks{kIdleBank0, kIdleBank1, kIdleBank2, 3};
    cfg.vrc7RegisterLine = line;
    cfg.frameCounter = 0x0022;
    return cfg;
}

std::vector<std::uint8_t> InesImage(std::size_t prgBanks16, std::uint8_t flags6, std::uint8_t flags7) {
    std::vector<std::uint8_t> file(16 + prgBanks16 * 16384 + 8192, 0);
    file[0] = 'N';
    file[1] = 'E';
    file[2] = 'S';
    file[3] = 0x1A;
    file[4] = static_cast<std::uint8_t>(prgBanks16);
    file[5] = 1;
    file[6] = flags6;
    file[7] = flags7;
    return file;
}

}  // namespace

void Vrc7PrgMapsBanksAndDecodesTheWiring() {
    std::printf("Vrc7PrgMapsBanksAndDecodesTheWiring\n");
    Vrc7Prg prg(64, 4);
    auto bank = [&](std::uint16_t address) { return prg.Offset(address) / 0x2000; };
    Check(bank(0x8000) == 60 && bank(0xA000) == 61 && bank(0xC000) == 62 && bank(0xE000) == 63, "power-on: the last four banks");
    Check(prg.Write(0x8000, 0x05) == Vrc7Prg::Sound::None && bank(0x8000) == 5, "$8000 selects the bank at $8000");
    prg.Write(0x8010, 0x07);
    prg.Write(0x9000, 0x7F);
    Check(bank(0xA000) == 7, "$8010 (A4) selects the bank at $A000");
    Check(bank(0xC000) == 0x3F, "$9000 selects the bank at $C000 and keeps 6 bits (0x7F -> 0x3F)");
    Check(bank(0xE000) == 63, "$E000 stays the last bank");
    Check(prg.Offset(0x8123) == 5 * 0x2000 + 0x123, "an offset inside a bank is the address's low 13 bits");
    Check(prg.MismatchedWrites() == 0, "writes on the specified line are not counted as mismatched");

    // Sound registers: $9010 address, $9030 data (A4 wiring).
    Check(prg.Write(0x9010, 0x30) == Vrc7Prg::Sound::Address, "$9010 is the sound address register");
    Check(prg.Write(0x9030, 0x12) == Vrc7Prg::Sound::Data, "$9030 is the sound data register");
    Check(prg.MismatchedWrites() == 0, "the sound registers on A4 are not mismatched");
    // The other wiring's addresses mean something else here and are counted.
    prg.Write(0x8008, 0x01);
    Check(bank(0x8000) == 1 && prg.MismatchedWrites() == 1, "$8008 on an A4 board writes the first bank register and is counted");
    Check(prg.Write(0x9028, 0x00) == Vrc7Prg::Sound::None && prg.MismatchedWrites() == 2, "$9028 on an A4 board is not the data register, and is counted");
    Check(bank(0xC000) == 0, "... it wrote the $C000 bank register");

    // $E000 (mirroring, WRAM enable) is received; CHR and IRQ registers change no PRG bank.
    prg.Write(0xE000, 0x80);
    Check(prg.MirroringAndWram() == 0x80, "$E000 is received");
    const std::uint8_t b0 = prg.Bank(0), b1 = prg.Bank(1), b2 = prg.Bank(2);
    for (std::uint16_t a : std::initializer_list<std::uint16_t>{0xA000, 0xA010, 0xB000, 0xB010, 0xC000, 0xC010, 0xD000, 0xD010, 0xE010, 0xF000, 0xF010})
        prg.Write(a, 0x33);
    Check(prg.Bank(0) == b0 && prg.Bank(1) == b1 && prg.Bank(2) == b2 && prg.MirroringAndWram() == 0x80,
          "CHR and IRQ registers change no PRG bank and not the $E000 register");

    prg.Reset();
    Check(bank(0x8000) == 60 && prg.MismatchedWrites() == 0, "Reset returns to the power-on state");
    prg.SetState(0x0A, 0x1A, 0x1D);
    Check(bank(0x8000) == 10 && bank(0xA000) == 26 && bank(0xC000) == 29 && bank(0xE000) == 63, "SetState sets the three registers");

    // A3 wiring: $8008, $9008 / $9028.
    Vrc7Prg a3(64, 3);
    a3.Write(0x8008, 0x09);
    Check(a3.Offset(0xA000) / 0x2000 == 9 && a3.MismatchedWrites() == 0, "A3: $8008 selects the bank at $A000");
    Check(a3.Write(0x9008, 0x01) == Vrc7Prg::Sound::Address, "A3: $9008 is the sound address register");
    Check(a3.Write(0x9028, 0x01) == Vrc7Prg::Sound::Data, "A3: $9028 is the sound data register");
    Check(a3.MismatchedWrites() == 0, "A3: those writes are not mismatched");
    a3.Write(0x8010, 0x21);
    Check(a3.Offset(0x8000) / 0x2000 == 0x21 && a3.MismatchedWrites() == 1, "A3: $8010 is the first bank register, and is counted");

    Vrc7Prg tiny(2, 4);
    Check(tiny.Offset(0xE000) / 0x2000 == 3 && tiny.Offset(0x8000) / 0x2000 == 0, "fewer than four banks are treated as four");
}

void NesVrc7CartridgeAndDriverSettings() {
    std::printf("NesVrc7CartridgeAndDriverSettings\n");
    auto mapperAndSize = [](const std::vector<std::uint8_t>& file) {
        try {
            const NesCartridge cart = ParseInesCartridge(file.data(), file.size());
            return std::make_pair(static_cast<int>(cart.mapper), cart.prg.size());
        } catch (const std::runtime_error&) {
            return std::make_pair(-1, std::size_t{0});
        }
    };
    Check(mapperAndSize(InesImage(32, 0x50, 0x50)) == std::make_pair(85, std::size_t{32 * 16384}), "mapper 85 with 512 KB parses");
    Check(mapperAndSize(InesImage(16, 0x50, 0x50)).first == 85, "mapper 85 with 256 KB parses");
    Check(mapperAndSize(InesImage(2, 0x50, 0x50)).first == 85, "mapper 85 with 32 KB parses");
    Check(mapperAndSize(InesImage(1, 0x50, 0x50)).first == -1, "a 16 KB VRC7 image is rejected");
    Check(mapperAndSize(InesImage(3, 0x50, 0x50)).first == -1, "a VRC7 PRG size that is not a power of two is rejected");
    Check(mapperAndSize(InesImage(64, 0x50, 0x50)).first == -1, "a VRC7 PRG size above 512 KB is rejected");
    Check(mapperAndSize(InesImage(2, 0x40, 0x50)).first == -1, "mapper 84 is not supported");

    NesDriverConfig cfg;
    std::string error;
    Check(ParseNesDriverConfig("<driver init=\"0x8066\" frame=\"0x8304\" request_call=\"0x80CB\" prg_banks_call=\"0 1 2\" "
                               "prg_banks_idle=\"3 4 5\" vrc7_register_line=\"A4\" frame_counter=\"0x0022\"/>",
                               cfg, &error),
          "the VRC7 driver settings parse");
    Check(cfg.prgBanksCall && cfg.prgBanksCall->count == 3 && cfg.prgBanksCall->r6 == 0 && cfg.prgBanksCall->r7 == 1 && cfg.prgBanksCall->r8 == 2,
          "three call banks");
    Check(cfg.prgBanksIdle && cfg.prgBanksIdle->r6 == 3 && cfg.prgBanksIdle->r7 == 4 && cfg.prgBanksIdle->r8 == 5, "three idle banks");
    Check(cfg.vrc7RegisterLine && *cfg.vrc7RegisterLine == 4, "the register line A4");
    Check(cfg.frameCounter && *cfg.frameCounter == 0x22, "the per-frame RAM byte");
    NesDriverConfig a3;
    ParseNesDriverConfig("<driver vrc7_register_line=\"A3\"/>", a3, &error);
    Check(a3.vrc7RegisterLine && *a3.vrc7RegisterLine == 3, "the register line A3");
    NesDriverConfig two;
    ParseNesDriverConfig("<driver prg_banks_call=\"0x0C 0x1E\" prg_banks_idle=\"0x0C 0x1E\"/>", two, &error);
    Check(two.prgBanksCall && two.prgBanksCall->count == 2 && !two.vrc7RegisterLine && !two.frameCounter,
          "two banks still parse; the line and the byte are optional");
    auto rejects = [](const std::string& xml) {
        NesDriverConfig c;
        std::string e;
        return !ParseNesDriverConfig(xml, c, &e) && !e.empty();
    };
    Check(rejects("<driver vrc7_register_line=\"A5\"/>"), "a register line other than A3 and A4 is rejected");
    Check(rejects("<driver frame_counter=\"0x2000\"/>"), "a per-frame byte outside the RAM is rejected");
    Check(rejects("<driver frame_counter=\"banana\"/>"), "a bad per-frame byte address is rejected");
    Check(rejects("<driver prg_banks_call=\"0 1 2 3\" prg_banks_idle=\"0 1 2 3\"/>"), "four banks are rejected");
    Check(rejects("<driver prg_banks_call=\"0 1 2\" prg_banks_idle=\"0 1\"/>"), "call and idle with different numbers of banks are rejected");
    Check(rejects("<driver prg_banks_call=\"0 1 256\" prg_banks_idle=\"0 1 2\"/>"), "a bank number above 255 is rejected");
}

void GameDefinitionRomArchiveResolvesFromTwoArchives() {
    std::printf("GameDefinitionRomArchiveResolvesFromTwoArchives\n");
    const char* xml =
        "<chipestra><game id=\"g\" title=\"G\" machine=\"M\" variant=\"V\" archive=\"Main Set\">"
        "<resources><rom name=\"cart\" file=\"cart.bin\" size=\"4\"/>"
        "<rom name=\"instruments\" file=\"inst.bin\" archive=\"Other Set\" size=\"3\"/></resources>"
        "<tracks><track id=\"1\" title=\"t\" command=\"0x01\"/></tracks></game></chipestra>";
    const XmlParseResult parsed = ParseGameDefinition(xml);
    Check(parsed.Status == XmlParseStatus::Ok, "a <rom> with its own archive parses");
    if (parsed.Status != XmlParseStatus::Ok) return;
    const GameDefinition& def = parsed.Definition;
    Check(def.Resources.size() == 2 && def.Resources[0].Archive.empty() && def.Resources[1].Archive == "Other Set",
          "the <rom> archive is kept; a <rom> without one has none of its own");
    const std::vector<RequiredResource> required = def.BuildRequiredResources();
    Check(required.size() == 2 && required[0].Archive == std::optional<std::string>("Main Set") &&
              required[1].Archive == std::optional<std::string>("Other Set"),
          "a resource without an archive takes the game's; one with its own keeps it");

    const std::filesystem::path root = std::filesystem::temp_directory_path() / "chipestra_vrc7_archive_test";
    std::filesystem::remove_all(root);
    WriteBytes(root / "Main Set" / "cart.bin", {1, 2, 3, 4});
    WriteBytes(root / "Main Set" / "inst.bin", {7, 7, 7});  // a same-named file in the game's own archive must not be taken
    WriteBytes(root / "Other Set" / "inst.bin", {9, 9, 9});
    {
        const ResourceResolver resolver{root};
        const ResourceResolutionResult res = resolver.Resolve(required);
        Check(res.Ok, "both resources resolve, each from its own archive");
        const ResolvedResource* inst = res.Resources.Find("instruments");
        Check(inst != nullptr && inst->Size() == 3 && inst->Data()[0] == 9, "the table comes from the other archive, not the same-named file of the game's archive");
        const ResolvedResource* cart = res.Resources.Find("cart");
        Check(cart != nullptr && cart->Size() == 4 && cart->Data()[0] == 1, "the cartridge comes from the game's archive");
    }
    std::filesystem::remove_all(root / "Other Set");
    {
        const ResourceResolver resolver{root};
        const ResourceResolutionResult res = resolver.Resolve(required);
        Check(!res.Ok, "without the other archive the game does not resolve");
        bool named = false;
        for (const ResourceResolutionFailure& f : res.Failures)
            if (f.LogicalName == "instruments" && f.Result == CHIPESTRA_E_NOT_FOUND && f.Detail.find("Other Set") != std::string::npos &&
                f.Detail.find(".zip") != std::string::npos)
                named = true;
        Check(named, "the error says which archive (Other Set.zip) is needed");
    }
    std::filesystem::remove_all(root);

    // A VRC7 game: the error for the instrument table names ds1001.
    const XmlParseResult real = ParseGameDefinition(
        "<chipestra><game id=\"g\" title=\"G\" machine=\"M\" variant=\"V\" archive=\"Game\">"
        "<resources><rom name=\"cart\" file=\"cart.bin\" size=\"4\"/>"
        "<rom name=\"instruments\" file=\"ds1001_instruments.bin\" archive=\"ds1001\" size=\"144\"/></resources>"
        "<tracks><track id=\"1\" title=\"t\" command=\"0x01\"/></tracks></game></chipestra>");
    WriteBytes(root / "Game" / "cart.bin", {1, 2, 3, 4});
    const ResourceResolver resolver{root};
    const ResourceResolutionResult res = resolver.Resolve(real.Definition.BuildRequiredResources());
    bool mentionsDs1001 = false;
    for (const ResourceResolutionFailure& f : res.Failures)
        if (f.LogicalName == "instruments" && f.Detail.find("ds1001.zip") != std::string::npos) mentionsDs1001 = true;
    Check(!res.Ok && mentionsDs1001, "a missing ds1001 is reported as such (ds1001.zip is needed)");
    std::filesystem::remove_all(root);
}

void NesVrc7MachineInputsAreChecked() {
    std::printf("NesVrc7MachineInputsAreChecked\n");
    const std::string goodDriver =
        "<driver init=\"0x8066\" frame=\"0x8304\" request_call=\"0x80CB\" prg_banks_call=\"0 1 2\" prg_banks_idle=\"0 1 2\" "
        "vrc7_register_line=\"A4\" frame_counter=\"0x0022\"/>";
    auto resources = [&](const std::vector<std::uint8_t>& cart, const std::string& driver, bool withTable) {
        MachineResources r;
        r.Add(ResolvedResource("cart", cart));
        r.Add(ResolvedResource("driver_xml", std::vector<std::uint8_t>(driver.begin(), driver.end())));
        if (withTable) r.Add(ResolvedResource("instruments", MadeUpTable()));
        return r;
    };
    auto throwsWith = [&](const MachineResources& r, const char* text) {
        try {
            AssembleNesMachineInputs(r);
        } catch (const std::runtime_error& e) {
            return std::string(e.what()).find(text) != std::string::npos;
        }
        return false;
    };
    const std::vector<std::uint8_t> vrc7 = InesImage(32, 0x50, 0x50);
    bool ok = false;
    NesMachineInputs inputs;
    try {
        inputs = AssembleNesMachineInputs(resources(vrc7, goodDriver, true));
        ok = true;
    } catch (...) {
    }
    Check(ok && inputs.cartridge.mapper == 85 && inputs.vrc7Instruments == MadeUpTable(), "a VRC7 game with its table assembles; the table is passed on");
    Check(throwsWith(resources(vrc7, goodDriver, false), "ds1001"), "a VRC7 game without the instrument table is refused, naming ds1001");
    Check(throwsWith(resources(vrc7, "<driver init=\"0x8066\" frame=\"0x8304\" prg_banks_call=\"0 1 2\" prg_banks_idle=\"0 1 2\"/>", true), "vrc7_register_line"),
          "a VRC7 game without the register line is refused");
    Check(throwsWith(resources(vrc7, "<driver init=\"0x8066\" frame=\"0x8304\" vrc7_register_line=\"A4\" prg_banks_call=\"0 1\" prg_banks_idle=\"0 1\"/>", true), "three banks"),
          "a VRC7 game with two banks is refused");
    const std::vector<std::uint8_t> nrom = InesImage(2, 0x00, 0x00);
    Check(throwsWith(resources(nrom, "<driver init=\"0x8000\" frame=\"0x8100\" vrc7_register_line=\"A4\"/>", false), "VRC7"),
          "the register line on another mapper is refused");
    bool built = false;
    try {
        NesMachine machine(std::move(inputs.cartridge), std::move(inputs.driver), {});
    } catch (const std::invalid_argument&) {
        built = true;
    }
    Check(built, "a Machine for a VRC7 cartridge cannot be built without the table");
}

void NesMachineVrc7BankingWramFrameCounterAndSound() {
    std::printf("NesMachineVrc7BankingWramFrameCounterAndSound\n");
    auto machine = std::make_unique<NesMachine>(BuildVrc7Cartridge(), BuildVrc7Driver(), MadeUpTable());
    std::vector<std::tuple<std::uint64_t, bool, unsigned>> vrc7Writes;
    machine->SetVrc7WriteObserverForTest([&](std::uint64_t frame, bool data, std::uint8_t value) { vrc7Writes.emplace_back(frame, data, value); });
    machine->Initialize();

    // The start-up frame ran the frame routine with the call banks mapped (4, 5, 6 at $8000/$A000/$C000, the last
    // bank 63 at $E000).
    Check(machine->RamForTest(0x0300) == 0x45, "during the call $A000 shows the second call bank (5)");
    Check(machine->RamForTest(0x0301) == 0x46, "during the call $C000 shows the third call bank (6)");
    Check(machine->RamForTest(0x0302) == 0x7F, "the last bank is fixed at $E000");
    Check(machine->RamForTest(0x0305) == 0 && machine->RamForTest(0x0310) == 1, "no request was made at start-up (the frame routine ran once)");
    Check(machine->PrgOffsetForTest(0x8000) / 0x2000 == 1 && machine->PrgOffsetForTest(0xA000) / 0x2000 == 2 &&
              machine->PrgOffsetForTest(0xC000) / 0x2000 == 3 && machine->PrgOffsetForTest(0xE000) / 0x2000 == 63,
          "between the calls the idle banks are mapped");
    Check(vrc7Writes.empty(), "the start-up writes to the VRC7 are not reported to the observer");
    Check(machine->Vrc7PrgForTest().MismatchedWrites() == 0, "the generated code and the routines write only the specified line");
    Check(machine->RamForTest(0x0022) == 1, "the per-frame byte was increased once by the start-up frame");

    Check(machine->LogicalChannels().size() == 11, "the Machine has 11 channels: 5 of the APU and 6 of the VRC7");
    Check(machine->LogicalChannels()[5].DeviceName == "VRC7" && machine->LogicalChannels()[5].ChannelName == "FM1" &&
              machine->LogicalChannels()[10].ChannelName == "FM6",
          "the VRC7 channels are named");
    Check(machine->LogicalChannels()[0].RoutingGain == 1.8f && machine->LogicalChannels()[5].RoutingGain == 2.0f,
          "the APU is mixed at 0.90 x 2 and the VRC7 at 1.0 x 2 (MAME's route gains times the Mixer's centre factor)");
    Check(machine->LogicalChannels()[5].NativeSampleRate == 49715, "the VRC7 makes 49715 samples a second");

    MusicCommand song;
    song.Kind = MusicCommandKind::Raw;
    song.Value = 5;
    MusicCommand withAux = song;
    withAux.Auxiliary = 0x10;
    Check(machine->AcceptsCommand(song) && !machine->AcceptsCommand(withAux), "a request number is accepted, an auxiliary is not");

    machine->SendCommand(song);
    Check(machine->WramForTest(0) == 0 && machine->RamForTest(0x0305) == 0, "the request is not made when the command is sent");
    LogicalChannelSink sink;
    machine->Render(EmuDuration(3300), &sink);  // one frame starts
    Check(machine->RamForTest(0x0304) == 5 && machine->RamForTest(0x0305) == 1, "the first frame after the command calls the request routine once with A = 5");
    Check(machine->WramForTest(0) == 5, "the request routine wrote WRAM $6000");
    const std::vector<std::tuple<std::uint64_t, bool, unsigned>> expected = {
        {0, false, 0x33}, {0, true, 0x02}, {0, false, 0x13}, {0, true, 0x80}, {0, false, 0x23}, {0, true, 0x18}};
    Check(vrc7Writes == expected, "the request routine's sound writes reach the observer in order, in frame 0");
    machine->Render(EmuDuration(3 * 3200), &sink);
    Check(machine->RamForTest(0x0305) == 1, "the request is made once");
    Check(machine->RamForTest(0x0022) == 1 + machine->FramesStartedForTest(),
          "the per-frame byte is increased once for every frame started (start-up frame + frames since)");
    Check(machine->Vrc7PrgForTest().MismatchedWrites() == 0, "still no mismatched write");

    auto peak = [&](std::size_t channel) {
        const LogicalChannelBuffer& b = sink.ChannelBuffer(static_cast<std::uint32_t>(channel));
        float p = 0.0f;
        for (std::size_t i = 0; i < b.Size(); ++i) p = std::max(p, std::fabs(b[i]));
        return p;
    };
    Check(peak(8) > 0.0f, "channel 3 (FM4), started by the request routine, sounds");
    Check(peak(5) > 0.0f, "channel 0 (FM1), started in the start-up frame, sounds");
    Check(peak(6) == 0.0f && peak(10) == 0.0f, "channels nobody started are silent");

    // Switching resets the whole Machine, WRAM included.
    MusicCommand next;
    next.Kind = MusicCommandKind::Raw;
    next.Value = 7;
    machine->SendCommand(next);
    Check(machine->WramForTest(0) == 0 && machine->RamForTest(0x0305) == 0, "a second command first resets the Machine: WRAM is zero again");
    Check(machine->RamForTest(0x0022) == 1, "and the per-frame byte starts again");
    sink.ClearAll();
    machine->Render(EmuDuration(3300), &sink);
    Check(machine->RamForTest(0x0304) == 7 && machine->WramForTest(0) == 7, "the new request is then made once");

    // Mute of a VRC7 channel: only that one.
    machine->SendCommand(song);
    machine->LogicalChannels()[8].Mute = true;
    sink.ClearAll();
    machine->Render(EmuDuration(3300), &sink);
    Check(sink.ChannelBuffer(8).Size() > 0 && peak(8) == 0.0f, "a muted VRC7 channel makes silence");
    Check(peak(5) > 0.0f, "the other VRC7 channel still sounds");
    machine->LogicalChannels()[8].Mute = false;

    // The Music Stop: a Stop resets and requests nothing.
    machine->SendCommand(song);
    MusicCommand stop;
    stop.Kind = MusicCommandKind::Stop;
    machine->SendCommand(stop);
    sink.ClearAll();
    machine->Render(EmuDuration(3 * 3200), &sink);
    Check(machine->RamForTest(0x0305) == 0 && machine->WramForTest(0) == 0, "Music Stop (reset) cancels a pending request");
    Check(machine->PrgOffsetForTest(0xA000) / 0x2000 == 2, "Music Stop returns to the idle banks");

    // A3 wiring: the same program with a board that decodes A3 does not reach the sound registers as intended.
    auto wrong = std::make_unique<NesMachine>(BuildVrc7Cartridge(), BuildVrc7Driver(3), MadeUpTable());
    wrong->Initialize();
    Check(wrong->Vrc7PrgForTest().MismatchedWrites() > 0, "a program written for A4 on an A3 board shows up as mismatched writes");
}

void LagrangeViaDefinitionXmlRealRomMatchesTheRealGame() {
    std::printf("LagrangeViaDefinitionXmlRealRomMatchesTheRealGame\n");
    const auto path = FindDefinitionXml("lagrange.xml");
    Check(path.has_value(), "lagrange: lagrange.xml is present (run from the repo root)");
    if (!path) return;
    const auto text = ReadFileText(*path);
    Check(text.has_value(), "lagrange: file is readable");
    if (!text) return;
    const XmlParseResult parsed = ParseGameDefinition(*text);
    Check(parsed.Status == XmlParseStatus::Ok, "lagrange: lagrange.xml parses cleanly");
    if (parsed.Status != XmlParseStatus::Ok) return;
    const GameDefinition& def = parsed.Definition;
    Check(def.Machine == "NINTENDO_NES" && def.Variant == "NES_BASE", "lagrange: machine NINTENDO_NES / variant NES_BASE");
    Check(def.Resources.size() == 2 && def.Resources[1].Archive == "ds1001" && def.Resources[1].File == "ds1001_instruments.bin",
          "lagrange: the instrument table is a resource of the archive ds1001");

    const char* romRootEnv = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRootEnv == nullptr || romRootEnv[0] == '\0') {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is not set - real-ROM bring-up not exercised\n");
        return;
    }
    const ResourceResolver resolver{std::filesystem::path(romRootEnv)};
    ResourceResolutionResult res = resolver.Resolve(def.BuildRequiredResources());
    Check(res.Ok, "lagrange: the cartridge (archive \"Lagrange Point (Japan)\") and the table (archive ds1001) resolve under CHIPESTRA_ROM_ROOT");
    if (!res.Ok) return;
    const std::string driverXml = ExtractDriverElementXml(*text);
    Check(!driverXml.empty(), "lagrange: the <driver> element is present");
    res.Resources.Add(ResolvedResource("driver_xml", std::vector<std::uint8_t>(driverXml.begin(), driverXml.end())));

    auto create = [&]() {
        std::unique_ptr<IMachine> m = DefaultMachineRegistry().Create(MachineId(def.Machine), VariantId(def.Variant), res.Resources);
        if (m) m->Initialize();
        return m;
    };

    // The requests of the sound driver: 0x01-0x93 (0x00 and 0x94 and above are not sounds). The recordings of the
    // real game live in nes/lagrange/real_game below CHIPESTRA_REFERENCE_DIR: one file per request
    // (req_<number>.txt: "<update> <address> <value>", updates counted from the request; the VRC7 writes are
    // $9010 for the address register and $9030 for the data register). The XML's tracks are not what is compared (a
    // track can be removed or renamed), but each track must be one of the requests.
    std::vector<unsigned> requests;
    for (unsigned v = 0x01; v <= 0x93; ++v) requests.push_back(v);
    const auto stopTrack = std::find_if(def.Tracks.begin(), def.Tracks.end(), [](const TrackDefinition& t) { return t.IsControlCommand; });
    Check(stopTrack != def.Tracks.end(), "lagrange: the XML has a Music Stop control track");
    for (const TrackDefinition& track : def.Tracks) {
        if (track.IsControlCommand) continue;
        const unsigned value = static_cast<unsigned>(std::strtoul(track.Command.c_str(), nullptr, 0));
        Check(std::find(requests.begin(), requests.end(), value) != requests.end(), (track.Id + ": is one of the requests the driver plays").c_str());
    }

    const char* referenceRoot = GetEnvVar("CHIPESTRA_REFERENCE_DIR");
    const bool haveReference = referenceRoot != nullptr && referenceRoot[0] != '\0';
    const std::filesystem::path referenceDir =
        haveReference ? std::filesystem::path(referenceRoot) / "nes" / "lagrange" / "real_game" : std::filesystem::path();
    if (!haveReference) std::printf("  SKIP: CHIPESTRA_REFERENCE_DIR is not set - the write sequences are not compared with the real game\n");

    constexpr std::uint64_t kFrames = 600;
    const std::uint64_t ticks = kFrames * 59561ull / 2 * 192000ull / NesMachine::kCpuClockHz + 1;
    struct W {
        std::uint64_t frame;
        unsigned address, value;
        bool operator==(const W& o) const { return frame == o.frame && address == o.address && value == o.value; }
    };

    std::size_t compared = 0;
    for (unsigned request : requests) {
        char name[16];
        std::snprintf(name, sizeof(name), "request %02X", request);
        const std::string label = name;
        std::unique_ptr<IMachine> base = create();
        Check(base != nullptr, (label + ": the Machine is created from the XML's own settings").c_str());
        if (!base) return;
        NesMachine& machine = static_cast<NesMachine&>(*base);
        std::vector<W> writes;
        machine.SetApuWriteObserverForTest([&](std::uint64_t frame, std::uint8_t reg, std::uint8_t value) {
            if (frame < kFrames) writes.push_back({frame, 0x4000u + reg, value});
        });
        machine.SetVrc7WriteObserverForTest([&](std::uint64_t frame, bool data, std::uint8_t value) {
            if (frame < kFrames) writes.push_back({frame, data ? 0x9030u : 0x9010u, value});
        });
        MusicCommand command;
        command.Kind = MusicCommandKind::Raw;
        command.Value = static_cast<std::uint8_t>(request);
        Check(machine.AcceptsCommand(command), (label + ": accepted by the Machine").c_str());
        machine.SendCommand(command);
        LogicalChannelSink sink;
        // Rendered in blocks of 60 frames, as the player does, so the buffers stay small.
        for (std::uint64_t done = 0; done < ticks;) {
            const std::uint64_t block = std::min<std::uint64_t>(ticks - done, 60 * 3200);
            machine.Render(EmuDuration(block), &sink);
            sink.ClearAll();
            done += block;
        }
        const NesMachine::BusStats stats = machine.BusStatsForTest();
        Check(stats.ppuReads + stats.ppuWrites + stats.inputReads == 0,
              (label + ": the sound driver does not touch the picture chip or the controllers").c_str());
        Check(machine.Vrc7PrgForTest().MismatchedWrites() == 0, (label + ": every write to the cartridge is on the specified register line").c_str());

        if (!haveReference) continue;
        char fileName[32];
        std::snprintf(fileName, sizeof(fileName), "req_%02X.txt", request);
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
                std::printf("      recording update %llu %04X=%02X\n", static_cast<unsigned long long>(expected[i].frame), expected[i].address,
                            expected[i].value);
            if (i < writes.size())
                std::printf("      Chipestra update %llu %04X=%02X\n", static_cast<unsigned long long>(writes[i].frame), writes[i].address,
                            writes[i].value);
        }
        Check(same, (label + ": all APU and VRC7 writes of the first 600 updates equal the real game's (update, address, value)").c_str());
        ++compared;
    }
    if (haveReference) {
        std::printf("  %zu requests compared with the recording of the real game\n", compared);
        Check(compared == requests.size(), "lagrange: every request was compared with a recording");
    }
}
