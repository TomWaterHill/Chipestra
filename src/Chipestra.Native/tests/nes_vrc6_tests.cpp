// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// VRC6 tests. The synthetic program is hand-written; what each routine must leave behind is worked out here from
// the instructions and the banks the Machine is asked to map, not read back from the Machine.

#include "nes_vrc6_tests.h"

#include "default_machine_catalog.h"
#include "definition_xml_locator.h"
#include "devices/nintendo/nes/nes_cartridge.h"
#include "devices/nintendo/nes/nes_driver_config.h"
#include "devices/nintendo/nes/nes_machine.h"
#include "devices/nintendo/nes/nes_vrc6.h"
#include "machine_registry.h"
#include "resource_resolver.h"
#include "test_support.h"
#include "xml_definition_parser.h"

#include <algorithm>
#include <array>
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

// 16 banks of 8 KB. Bank k holds the marker 0xC0+k in bytes $100-$1FF.
//   frame routine, in the 16 KB bank 2 ($8000 while it is mapped there): record what $C000, $A000 and $E000 show
//   and the X register, then (once) start pulse 1 and the sawtooth.
//   request routine, at $8200 of the same bank: record A, count the calls and start pulse 2.
constexpr std::uint16_t kFrameRoutine = 0x8000;
constexpr std::uint16_t kRequestRoutine = 0x8200;
constexpr std::uint8_t kCallBank16 = 2;
constexpr std::uint8_t kCallBank8 = 7;
constexpr std::uint8_t kIdleBank16 = 1;
constexpr std::uint8_t kIdleBank8 = 9;

NesCartridge BuildVrc6Cartridge(std::uint16_t mapper = 24) {
    NesCartridge cart;
    cart.mapper = mapper;
    cart.prg.assign(16 * 0x2000, 0xEA);
    for (std::size_t bank = 0; bank < 16; ++bank)
        for (std::size_t i = 0x100; i < 0x200; ++i) cart.prg[bank * 0x2000 + i] = static_cast<std::uint8_t>(0xC0 + bank);
    const std::vector<std::uint8_t> frame = {
        0xAD, 0x00, 0xC1, 0x8D, 0x00, 0x03,  // LDA $C100; STA $0300
        0xAD, 0x00, 0xA1, 0x8D, 0x01, 0x03,  // LDA $A100; STA $0301
        0xAD, 0x00, 0xE1, 0x8D, 0x02, 0x03,  // LDA $E100; STA $0302
        0x8E, 0x03, 0x03,                    // STX $0303
        0xAD, 0x10, 0x03, 0xD0, 0x1C,        // LDA $0310; BNE done
        0xEE, 0x10, 0x03,                    // INC $0310
        0xA9, 0x8F, 0x8D, 0x00, 0x90,        // LDA #$8F; STA $9000  (pulse 1: constant volume 15)
        0xA9, 0x00, 0x8D, 0x01, 0x90,        // LDA #$00; STA $9001
        0xA9, 0x80, 0x8D, 0x02, 0x90,        // LDA #$80; STA $9002  (enable)
        0xA9, 0x08, 0x8D, 0x00, 0xB0,        // LDA #$08; STA $B000  (sawtooth rate 8)
        0xA9, 0x80, 0x8D, 0x02, 0xB0,        // LDA #$80; STA $B002
        0x60};                               // done: RTS
    const std::vector<std::uint8_t> request = {
        0x8D, 0x04, 0x03,                    // STA $0304
        0xEE, 0x05, 0x03,                    // INC $0305
        0xA9, 0x8A, 0x8D, 0x00, 0xA0,        // LDA #$8A; STA $A000  (pulse 2: constant volume 10)
        0xA9, 0x00, 0x8D, 0x01, 0xA0,        // LDA #$00; STA $A001
        0xA9, 0x80, 0x8D, 0x02, 0xA0,        // LDA #$80; STA $A002
        0x60};
    std::copy(frame.begin(), frame.end(), cart.prg.begin() + 4 * 0x2000);
    std::copy(request.begin(), request.end(), cart.prg.begin() + 4 * 0x2000 + 0x200);
    return cart;
}

NesDriverConfig BuildVrc6Driver() {
    NesDriverConfig cfg;
    cfg.ramClear = std::pair<std::uint16_t, std::uint16_t>{0x0000, 0x07FF};
    cfg.frame = {kFrameRoutine};
    cfg.requestCall = kRequestRoutine;
    cfg.prgBanksCall = NesDriverConfig::PrgBanks{kCallBank16, kCallBank8};
    cfg.prgBanksIdle = NesDriverConfig::PrgBanks{kIdleBank16, kIdleBank8};
    return cfg;
}

}  // namespace

void Vrc6PrgMapsBanksLikeTheOriginal() {
    std::printf("Vrc6PrgMapsBanksLikeTheOriginal\n");
    Vrc6Prg prg(32, false);  // 256 KB
    auto bank = [&](std::uint16_t address) { return prg.Offset(address) / 0x2000; };
    Check(bank(0x8000) == 0 && bank(0xA000) == 1 && bank(0xC000) == 0 && bank(0xE000) == 31, "power-on: both banks 0, the last bank at $E000");
    unsigned sound = 0;
    Check(!prg.Write(0x8000, 0x05, &sound), "$8000 is not a sound register");
    Check(bank(0x8000) == 10 && bank(0xA000) == 11, "the 16 KB bank register selects banks 2n and 2n+1 (5 -> 10, 11)");
    prg.Write(0x8000, 0xF3, nullptr);
    Check(bank(0x8000) == 6 && bank(0xA000) == 7, "the 16 KB register keeps 4 bits (0xF3 -> 3)");
    Check(!prg.Write(0xC000, 0x1B, &sound), "$C000 is not a sound register");
    Check(bank(0xC000) == 0x1B && bank(0x8000) == 6, "the 8 KB register selects the bank at $C000");
    prg.Write(0xC000, 0xFF, nullptr);
    Check(bank(0xC000) == 0x1F, "the 8 KB register keeps 5 bits");
    Check(bank(0xE000) == 31, "$E000 stays the last bank");
    prg.Write(0x8FFF, 0x02, nullptr);
    Check(bank(0x8000) == 4, "any address of $8000-$8FFF writes the 16 KB register");
    prg.Write(0xCFFF, 0x04, nullptr);
    Check(bank(0xC000) == 4, "any address of $C000-$CFFF writes the 8 KB register");
    // Registers that change no PRG bank: mirroring ($B003), CHR ($D000-$E003), the IRQ ($F000-$F002).
    Check(!prg.Write(0xB003, 0xFF, &sound), "$B003 (mirroring and PRG RAM) is received and is not a sound register");
    for (std::uint16_t a : {0xD000, 0xD001, 0xD002, 0xD003, 0xE000, 0xE003, 0xF000, 0xF001, 0xF002}) prg.Write(a, 0x33, nullptr);
    Check(bank(0x8000) == 4 && bank(0xA000) == 5 && bank(0xC000) == 4, "CHR and IRQ registers change no PRG bank");
    Check(prg.Offset(0xA123) == 5 * 0x2000 + 0x123, "an offset inside a bank is the address's low 13 bits");
    prg.Reset();
    Check(bank(0x8000) == 0 && bank(0xC000) == 0, "Reset returns to the power-on state");
    prg.SetState(0x0C, 0x1E);
    Check(bank(0x8000) == 24 && bank(0xA000) == 25 && bank(0xC000) == 30 && bank(0xE000) == 31, "SetState sets both registers");

    // Sound registers (mapper 24: register = address bits 1..0).
    auto offsetOf = [&](std::uint16_t address, bool* isSound = nullptr) {
        unsigned off = 0xFFFF;
        const bool s = prg.Write(address, 0x00, &off);
        if (isSound) *isSound = s;
        return s ? off : 0xFFFFu;
    };
    Check(offsetOf(0x9000) == 0x000 && offsetOf(0x9001) == 0x001 && offsetOf(0x9002) == 0x002 && offsetOf(0x9003) == 0x003,
          "$9000-$9003: pulse 1 control, frequency low and high, the frequency control");
    Check(offsetOf(0xA000) == 0x100 && offsetOf(0xA001) == 0x101 && offsetOf(0xA002) == 0x102, "$A000-$A002: pulse 2");
    Check(offsetOf(0xA003) == 0x103, "$A003 reaches the sound chip as a register the chip does not use");
    Check(offsetOf(0xB000) == 0x200 && offsetOf(0xB001) == 0x201 && offsetOf(0xB002) == 0x202, "$B000-$B002: sawtooth");
    Check(offsetOf(0x9FFC) == 0x000 && offsetOf(0xAFFE) == 0x102, "only the low two address bits choose the register inside a block");
}

void Vrc6PrgMapper26SwapsTheAddressLines() {
    std::printf("Vrc6PrgMapper26SwapsTheAddressLines\n");
    Vrc6Prg prg(32, true);
    auto offsetOf = [&](std::uint16_t address) {
        unsigned off = 0xFFFF;
        return prg.Write(address, 0x00, &off) ? off : 0xFFFFu;
    };
    Check(offsetOf(0x9000) == 0x000 && offsetOf(0x9002) == 0x001 && offsetOf(0x9001) == 0x002 && offsetOf(0x9003) == 0x003,
          "mapper 26: A0 and A1 are exchanged, so $9002 is the frequency low register and $9001 the high one");
    Check(offsetOf(0xA002) == 0x101 && offsetOf(0xA001) == 0x102, "pulse 2 likewise");
    Check(offsetOf(0xB002) == 0x201 && offsetOf(0xB001) == 0x202 && offsetOf(0xB003) == 0xFFFFu, "sawtooth likewise; $B003 stays the mirroring register");
    prg.Write(0x8000, 0x03, nullptr);
    prg.Write(0xC000, 0x09, nullptr);
    Check(prg.Offset(0x8000) / 0x2000 == 6 && prg.Offset(0xC000) / 0x2000 == 9, "the bank registers are at the same addresses on mapper 26");
}

void NesVrc6CartridgeAndDriverSettings() {
    std::printf("NesVrc6CartridgeAndDriverSettings\n");
    auto image = [](std::size_t prgBanks16, std::uint8_t flags6, std::uint8_t flags7) {
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
    };
    auto mapperAndSize = [](const std::vector<std::uint8_t>& file) {
        try {
            const NesCartridge cart = ParseInesCartridge(file.data(), file.size());
            return std::make_pair(static_cast<int>(cart.mapper), cart.prg.size());
        } catch (const std::runtime_error&) {
            return std::make_pair(-1, std::size_t{0});
        }
    };
    Check(mapperAndSize(image(16, 0x80, 0x10)) == std::make_pair(24, std::size_t{16 * 16384}), "mapper 24 (VRC6a), 256 KB, parses");
    Check(mapperAndSize(image(16, 0xA0, 0x10)) == std::make_pair(26, std::size_t{16 * 16384}), "mapper 26 (VRC6b), 256 KB, parses");
    Check(mapperAndSize(image(2, 0x80, 0x10)).first == 24, "a 32 KB VRC6 image parses");
    Check(mapperAndSize(image(1, 0x80, 0x10)).first == -1, "a 16 KB VRC6 image is rejected");
    Check(mapperAndSize(image(3, 0x80, 0x10)).first == -1, "a VRC6 PRG size that is not a power of two is rejected");
    Check(mapperAndSize(image(32, 0x80, 0x10)).first == -1, "a VRC6 PRG size above 256 KB is rejected");
    Check(mapperAndSize(image(2, 0xB0, 0x10)).first == -1, "mapper 27 is still not supported");

    NesDriverConfig cfg;
    std::string error;
    Check(ParseNesDriverConfig("<driver init=\"0xE151 0xE211\" frame=\"0xE238\" request_call=\"0xE249\" "
                               "prg_banks_call=\"0x0C 0x1E\" prg_banks_idle=\"0x0C 0x1E\"/>",
                               cfg, &error),
          "request_call and the VRC6 bank registers parse");
    Check(cfg.requestCall && *cfg.requestCall == 0xE249, "request_call is an address (not limited to RAM)");
    Check(cfg.prgBanksCall && cfg.prgBanksCall->r6 == 0x0C && cfg.prgBanksCall->r7 == 0x1E, "the call register values");
    NesDriverConfig none;
    ParseNesDriverConfig("<driver frame=\"0xA000\"/>", none, &error);
    Check(!none.requestCall.has_value(), "request_call is optional");
    auto rejects = [](const std::string& xml) {
        NesDriverConfig c;
        std::string e;
        return !ParseNesDriverConfig(xml, c, &e) && !e.empty();
    };
    Check(rejects("<driver request_call=\"banana\"/>"), "a bad request_call address is rejected");
    Check(rejects("<driver request_call=\"0x10000\"/>"), "a request_call address above 0xFFFF is rejected");
}

void NesMachineVrc6BankingRequestCallAndSound() {
    std::printf("NesMachineVrc6BankingRequestCallAndSound\n");
    auto machine = std::make_unique<NesMachine>(BuildVrc6Cartridge(), BuildVrc6Driver());
    std::vector<std::tuple<std::uint64_t, unsigned, unsigned>> vrc6Writes;
    machine->SetVrc6WriteObserverForTest([&](std::uint64_t frame, std::uint16_t address, std::uint8_t value) {
        vrc6Writes.emplace_back(frame, address, value);
    });
    machine->Initialize();

    // The start-up frame ran the frame routine with the call banks mapped (16 KB bank 2 = banks 4, 5 at $8000/$A000,
    // 8 KB bank 7 at $C000, the last bank 15 at $E000) and X = 0.
    Check(machine->RamForTest(0x0300) == 0xC7, "during the call $C000 shows the 8 KB call bank (7)");
    Check(machine->RamForTest(0x0301) == 0xC5, "during the call $A000 shows the second half (bank 5) of the 16 KB call bank");
    Check(machine->RamForTest(0x0302) == 0xCF, "the last bank is fixed at $E000");
    Check(machine->RamForTest(0x0303) == 0, "the routines are called with X = 0");
    Check(machine->RamForTest(0x0305) == 0 && machine->RamForTest(0x0310) == 1, "no request was made at start-up (the frame routine ran once)");
    Check(machine->PrgOffsetForTest(0x8000) / 0x2000 == 2 && machine->PrgOffsetForTest(0xA000) / 0x2000 == 3 &&
              machine->PrgOffsetForTest(0xC000) / 0x2000 == 9 && machine->PrgOffsetForTest(0xE000) / 0x2000 == 15,
          "between the calls the idle banks are mapped");
    Check(vrc6Writes.empty(), "the start-up writes to the VRC6 are not reported to the observer");
    Check(machine->LogicalChannels().size() == 8, "the Machine has 8 channels: 5 of the APU and 3 of the VRC6");
    Check(machine->LogicalChannels()[5].DeviceName == "VRC6" && machine->LogicalChannels()[7].ChannelName == "Saw", "the VRC6 channels are named");
    Check(machine->LogicalChannels()[0].RoutingGain == 1.8f && machine->LogicalChannels()[5].RoutingGain == 2.0f,
          "the APU is mixed at 0.90 x 2 and the VRC6 at 1.0 x 2 (MAME's route gains times the Mixer's centre factor)");
    Check(machine->LogicalChannels()[5].NativeSampleRate == 1789772, "the VRC6 makes one sample per CPU cycle");

    // Commands: a request is a call with the value in A; a request number, never a RAM variable.
    MusicCommand song;
    song.Kind = MusicCommandKind::Raw;
    song.Value = 5;
    MusicCommand withAux = song;
    withAux.Auxiliary = 0x10;
    MusicCommand effect = song;
    effect.Kind = MusicCommandKind::Effect;
    Check(machine->AcceptsCommand(song) && machine->AcceptsCommand(effect), "a raw command and an effect are accepted");
    Check(!machine->AcceptsCommand(withAux), "a command with an auxiliary (request variable / set) is refused");

    machine->SendCommand(song);
    Check(machine->RamForTest(0x0304) == 0 && machine->RamForTest(0x0305) == 0, "the request is not made when the command is sent");
    LogicalChannelSink sink;
    machine->Render(EmuDuration(3300), &sink);  // one frame (about 3194 ticks) starts
    Check(machine->RamForTest(0x0304) == 5 && machine->RamForTest(0x0305) == 1, "the first frame after the command calls the request routine once with A = 5");
    Check(machine->RamForTest(0x0302) == 0xCF && machine->RamForTest(0x0303) == 0, "the frame routine ran in that frame too, and the request routine after it");
    machine->Render(EmuDuration(3 * 3200), &sink);
    Check(machine->RamForTest(0x0305) == 1, "the request is made once (the following frames do not repeat it)");
    const std::vector<std::tuple<std::uint64_t, unsigned, unsigned>> expected = {{0, 0xA000, 0x8A}, {0, 0xA001, 0x00}, {0, 0xA002, 0x80}};
    Check(vrc6Writes == expected, "the request routine's writes to $A000-$A002 reach the observer in order, in frame 0");

    // Sound: pulse 2 sounds (constant volume 10 of 128, times nothing: the channel's own samples), the others are
    // silent, and the mute removes it.
    const LogicalChannelBuffer& pulse2 = sink.ChannelBuffer(6);
    float peak = 0.0f;
    for (std::size_t i = 0; i < pulse2.Size(); ++i) peak = std::max(peak, std::fabs(pulse2[i]));
    Check(peak > 0.05f && peak <= 10.0f / 128.0f + 1e-6f, "pulse 2 sounds at up to its volume (10/128) after the request");
    const LogicalChannelBuffer& pulse1 = sink.ChannelBuffer(5);
    Check(pulse1.Size() > 0 && std::fabs(pulse1[pulse1.Size() - 1]) < 0.001f,
          "pulse 1, a constant level since the start-up frame, is blocked to silence by the DC filter after a few frames");

    // Switching resets the whole Machine: the next command starts from the freshly initialized state.
    MusicCommand next;
    next.Kind = MusicCommandKind::Raw;
    next.Value = 7;
    machine->SendCommand(next);
    Check(machine->RamForTest(0x0305) == 0 && machine->RamForTest(0x0304) == 0, "a second command first resets the Machine");
    sink.ClearAll();
    machine->Render(EmuDuration(3300), &sink);
    Check(machine->RamForTest(0x0304) == 7 && machine->RamForTest(0x0305) == 1, "and is then requested once");

    // Mute of a VRC6 channel.
    machine->SendCommand(song);
    machine->LogicalChannels()[6].Mute = true;
    sink.ClearAll();
    machine->Render(EmuDuration(3300), &sink);
    float mutedPeak = 0.0f;
    const LogicalChannelBuffer& muted = sink.ChannelBuffer(6);
    for (std::size_t i = 0; i < muted.Size(); ++i) mutedPeak = std::max(mutedPeak, std::fabs(muted[i]));
    Check(mutedPeak == 0.0f && muted.Size() > 0, "a muted VRC6 channel makes silence");

    // The Music Stop: a control track makes no request (Reset only); the Stop kind resets too.
    machine->LogicalChannels()[6].Mute = false;
    MusicCommand zero;
    zero.Kind = MusicCommandKind::Raw;
    zero.Value = 0;
    zero.Auxiliary = NesDriverConfig::kControlFlag;
    machine->SendCommand(zero);
    Check(machine->RamForTest(0x0305) == 0, "a control track resets and requests nothing");
    sink.ClearAll();
    machine->Render(EmuDuration(3 * 3200), &sink);
    Check(machine->RamForTest(0x0305) == 0, "and nothing is requested in the frames after it");
    machine->SendCommand(song);
    MusicCommand stop;
    stop.Kind = MusicCommandKind::Stop;
    machine->SendCommand(stop);
    sink.ClearAll();
    machine->Render(EmuDuration(3 * 3200), &sink);
    Check(machine->RamForTest(0x0305) == 0, "Music Stop (reset) cancels a pending request");
    Check(machine->PrgOffsetForTest(0xC000) / 0x2000 == 9, "Music Stop returns to the idle banks");
}

void AkumajouViaDefinitionXmlRealRomMatchesTheRealGame() {
    std::printf("AkumajouViaDefinitionXmlRealRomMatchesTheRealGame\n");
    const char* romRootEnv = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRootEnv == nullptr || romRootEnv[0] == '\0') {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is not set - real-ROM bring-up not exercised\n");
        return;
    }
    const auto path = FindDefinitionXml("akumajou.xml");
    Check(path.has_value(), "akumajou: akumajou.xml is present (run from the repo root)");
    if (!path) return;
    const auto text = ReadFileText(*path);
    Check(text.has_value(), "akumajou: file is readable");
    if (!text) return;
    const XmlParseResult parsed = ParseGameDefinition(*text);
    Check(parsed.Status == XmlParseStatus::Ok, "akumajou: akumajou.xml parses cleanly");
    if (parsed.Status != XmlParseStatus::Ok) return;
    const GameDefinition& def = parsed.Definition;
    Check(def.Machine == "NINTENDO_NES" && def.Variant == "NES_BASE", "akumajou: machine NINTENDO_NES / variant NES_BASE");

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
    Check(res.Ok, "akumajou: the cartridge resolves under CHIPESTRA_ROM_ROOT (archive \"Akumajou Densetsu (Japan)\")");
    if (!res.Ok) return;
    const std::string driverXml = ExtractDriverElementXml(*text);
    Check(!driverXml.empty(), "akumajou: the <driver> element is present");
    res.Resources.Add(ResolvedResource("driver_xml", std::vector<std::uint8_t>(driverXml.begin(), driverXml.end())));

    auto create = [&]() {
        std::unique_ptr<IMachine> m = DefaultMachineRegistry().Create(MachineId(def.Machine), VariantId(def.Variant), res.Resources);
        if (m) m->Initialize();
        return m;
    };

    const auto stopTrack = std::find_if(def.Tracks.begin(), def.Tracks.end(), [](const TrackDefinition& t) { return t.IsControlCommand; });
    Check(stopTrack != def.Tracks.end(), "akumajou: the XML has a Music Stop control track");

    // The requests of the sound driver: every number 0x01-0x7D (0x01-0x4F effects, 0x50-0x6E songs, 0x6F-0x7D
    // effects played by the DMC). The recordings of the real game live in nes/akumajou/real_game below
    // CHIPESTRA_REFERENCE_DIR: one file per request (req_<number>.txt: "<frame> <address> <value>", frames counted
    // from the request, the APU and VRC6 writes). The XML's tracks are not what is compared (a track can be removed or
    // renamed), but each track must be one of the requests.
    std::vector<unsigned> requests;
    for (unsigned v = 0x01; v <= 0x7D; ++v) requests.push_back(v);
    for (const TrackDefinition& track : def.Tracks) {
        if (track.IsControlCommand) continue;
        const unsigned value = static_cast<unsigned>(std::strtoul(track.Command.c_str(), nullptr, 0));
        Check(std::find(requests.begin(), requests.end(), value) != requests.end(), (track.Id + ": is one of the requests the driver plays").c_str());
    }

    const char* referenceRoot = GetEnvVar("CHIPESTRA_REFERENCE_DIR");
    const bool haveReference = referenceRoot != nullptr && referenceRoot[0] != '\0';
    const std::filesystem::path referenceDir =
        haveReference ? std::filesystem::path(referenceRoot) / "nes" / "akumajou" / "real_game" : std::filesystem::path();
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
        machine.SetVrc6WriteObserverForTest([&](std::uint64_t frame, std::uint16_t address, std::uint8_t value) {
            if (frame < kFrames) writes.push_back({frame, address, value});
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
                std::printf("      recording frame %llu %04X=%02X\n", static_cast<unsigned long long>(expected[i].frame), expected[i].address,
                            expected[i].value);
            if (i < writes.size())
                std::printf("      Chipestra frame %llu %04X=%02X\n", static_cast<unsigned long long>(writes[i].frame), writes[i].address,
                            writes[i].value);
        }
        Check(same, (label + ": all APU and VRC6 writes of the first 600 frames equal the real game's (frame, address, value)").c_str());
        ++compared;
    }
    if (haveReference) {
        std::printf("  %zu requests compared with the recording of the real game\n", compared);
        Check(compared == requests.size(), "akumajou: every request was compared with a recording");
    }
}

namespace {

// The cartridge of the real game loaded through its XML, with the 8 KB bank at $C000 chosen by the caller.
struct AkumajouRig {
    GameDefinition def;
    ResourceResolutionResult res;
    std::string driverXml;
    bool loaded = false;

    const std::uint8_t* Prg() const { return res.Resources.Find("cart")->Data() + 16; }
};

bool LoadAkumajouRig(AkumajouRig& rig, const char* romRoot, const char* bank) {
    const auto path = FindDefinitionXml("akumajou.xml");
    if (!path) return false;
    const auto text = ReadFileText(*path);
    if (!text) return false;
    const XmlParseResult parsed = ParseGameDefinition(*text);
    if (parsed.Status != XmlParseStatus::Ok) return false;
    rig.def = parsed.Definition;
    std::vector<RequiredResource> required;
    for (const RomResourceDefinition& rom : rig.def.Resources) {
        RequiredResource r;
        r.LogicalName = rom.Name;
        r.AcceptedNames = {rom.File};
        r.SizeBytes = rom.SizeBytes;
        r.Crc32 = rom.Crc32;
        r.Sha1 = rom.Sha1;
        if (!rig.def.Archive.empty()) r.Archive = rig.def.Archive;
        required.push_back(std::move(r));
    }
    rig.res = ResourceResolver{std::filesystem::path(romRoot)}.Resolve(required);
    if (!rig.res.Ok) return false;
    rig.driverXml = ExtractDriverElementXml(*text);
    // The XML maps the bank 0x07 at $C000 for the call and for the idle state; another bank is tried by rewriting both.
    const std::string mapped = "prg_banks_call=\"0x0C 0x07\" prg_banks_idle=\"0x0C 0x07\"";
    const std::size_t at = rig.driverXml.find(mapped);
    if (at == std::string::npos) return false;
    rig.driverXml.replace(at, mapped.size(), std::string("prg_banks_call=\"0x0C ") + bank + "\" prg_banks_idle=\"0x0C " + bank + "\"");
    rig.res.Resources.Add(ResolvedResource("driver_xml", std::vector<std::uint8_t>(rig.driverXml.begin(), rig.driverXml.end())));
    rig.loaded = true;
    return true;
}

std::unique_ptr<IMachine> CreateAkumajou(const AkumajouRig& rig) {
    std::unique_ptr<IMachine> m = DefaultMachineRegistry().Create(MachineId(rig.def.Machine), VariantId(rig.def.Variant), rig.res.Resources);
    if (m) m->Initialize();
    return m;
}

struct DmcRead {
    std::uint16_t address;
    std::uint8_t value;
};

struct Played {
    std::vector<DmcRead> reads;
    std::vector<std::pair<std::uint8_t, std::uint8_t>> registers;  // ($4010-$4013 and $4015 offsets, value)
};

Played PlayAkumajouRequest(const AkumajouRig& rig, unsigned request, int frames) {
    Played out;
    std::unique_ptr<IMachine> base = CreateAkumajou(rig);
    if (!base) return out;
    NesMachine& machine = static_cast<NesMachine&>(*base);
    machine.SetDmcReadObserverForTest([&](std::uint64_t, std::uint16_t address, std::uint8_t value) { out.reads.push_back({address, value}); });
    machine.SetApuWriteObserverForTest([&](std::uint64_t, std::uint8_t reg, std::uint8_t value) {
        if ((reg >= 0x10 && reg <= 0x13) || reg == 0x15) out.registers.emplace_back(reg, value);
    });
    MusicCommand command;
    command.Kind = MusicCommandKind::Raw;
    command.Value = static_cast<std::uint8_t>(request);
    machine.SendCommand(command);
    LogicalChannelSink sink;
    for (int f = 0; f < frames; ++f) {
        machine.Render(EmuDuration(3200), &sink);
        sink.ClearAll();
    }
    return out;
}

// What the original's DMC does with `bytes` read from start to end: the level after each bit, bits taken from the
// least significant bit of each byte, +2 for a 1 and -2 for a 0, neither past the ends of 0..127 (the 7-bit counter
// only moves while it stays in range).
std::vector<int> IdealDmcLevels(const std::vector<std::uint8_t>& bytes, int start) {
    std::vector<int> levels;
    int level = start;
    for (std::uint8_t byte : bytes) {
        for (int bit = 0; bit < 8; ++bit) {
            if ((byte >> bit) & 1) {
                if (level <= 125) level += 2;
            } else if (level >= 2) {
                level -= 2;
            }
            levels.push_back(level);
        }
    }
    return levels;
}

}  // namespace

void AkumajouVoicesDecodeLikeTheRomAndMatchTheNsf() {
    std::printf("AkumajouVoicesDecodeLikeTheRomAndMatchTheNsf\n");
    const char* romRoot = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRoot == nullptr || romRoot[0] == '\0') {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is not set - real-ROM bring-up not exercised\n");
        return;
    }
    AkumajouRig rig;
    Check(LoadAkumajouRig(rig, romRoot, "0x07"), "akumajou: the cartridge loads through the XML");
    if (!rig.loaded) return;
    const std::uint8_t* prg = rig.Prg();
    auto romByte = [&](unsigned bank, std::uint16_t address) { return prg[bank * 0x2000 + (address - 0xC000)]; };

    // The NSF of the game is reference data: its $C000-$DFFF pages are the sample bank. Optional.
    std::vector<std::uint8_t> nsfWindow;  // $C000-$DFFF
    const char* referenceRoot = GetEnvVar("CHIPESTRA_REFERENCE_DIR");
    if (referenceRoot != nullptr && referenceRoot[0] != '\0') {
        const auto nsfText = ReadFileText(std::filesystem::path(referenceRoot) / "nes" / "akumajou" / "AkumajouDensetsu.nsf");
        if (nsfText && nsfText->size() > 0x80 && nsfText->compare(0, 4, "NESM") == 0) {
            const std::uint8_t* d = reinterpret_cast<const std::uint8_t*>(nsfText->data());
            const std::size_t data = 0x80;
            for (unsigned half = 0; half < 2; ++half) {
                const std::size_t page = d[0x70 + 4 + half];  // the initial 4 KB banks of $C000 and $D000
                for (std::size_t i = 0; i < 0x1000; ++i) {
                    const std::size_t at = data + page * 0x1000 + i;
                    nsfWindow.push_back(at < nsfText->size() ? d[at] : std::uint8_t{0});
                }
            }
        }
    }
    if (nsfWindow.empty()) std::printf("  SKIP: the NSF is not in CHIPESTRA_REFERENCE_DIR (nes/akumajou/AkumajouDensetsu.nsf) - the data are not compared with it\n");

    // The voices (0x79 and 0x7B: the laugh at $C900; 0x7C and 0x7D: $D400, looped) and a sample both banks share (0x74).
    for (unsigned request : {0x74u, 0x79u, 0x7Bu, 0x7Cu, 0x7Du}) {
        char name[16];
        std::snprintf(name, sizeof(name), "request %02X", request);
        const std::string label = name;
        const Played played = PlayAkumajouRequest(rig, request, 140);
        Check(played.reads.size() > 1000, (label + ": the DMC reads a sample").c_str());

        std::size_t differsFromRom = 0, differsFromNsf = 0, nsfChecked = 0;
        for (const DmcRead& r : played.reads) {
            if (romByte(0x07, r.address) != r.value) ++differsFromRom;
            // The last 16 bytes of the NSF's window hold its own vectors.
            if (!nsfWindow.empty() && r.address >= 0xC000 && r.address < 0xDFF0) {
                ++nsfChecked;
                if (nsfWindow[r.address - 0xC000] != r.value) ++differsFromNsf;
            }
        }
        Check(differsFromRom == 0, (label + ": every byte the DMC reads is the byte of ROM bank 0x07 at that address").c_str());
        if (!nsfWindow.empty()) Check(nsfChecked > 0 && differsFromNsf == 0, (label + ": every byte the DMC reads equals the NSF's data at that address").c_str());

        // The chip against the ROM: the registers the driver wrote start the sample on a bare chip that reads the same
        // bank; the DAC levels it goes through must be the ones the bits of the ROM bytes give.
        unsigned r10 = 0, r12 = 0, r13 = 0;
        for (const auto& [reg, value] : played.registers) {
            if (reg == 0x10) r10 = value;
            if (reg == 0x12) r12 = value;
            if (reg == 0x13) r13 = value;
        }
        const std::uint16_t start = static_cast<std::uint16_t>(0xC000 + r12 * 64);
        const std::size_t length = r13 * 16u + 1u;
        std::vector<std::uint8_t> sampleBytes;
        for (std::size_t i = 0; i < length; ++i) sampleBytes.push_back(romByte(0x07, static_cast<std::uint16_t>(start + i)));
        std::vector<int> ideal;  // the levels that change, in order
        {
            int previous = 0;
            for (int level : IdealDmcLevels(sampleBytes, 0)) {
                if (level != previous) ideal.push_back(level);
                previous = level;
            }
        }
        NesApuDeviceAdapter apu(false, NesApuDeviceAdapter::Variant::kRp2a03g);
        apu.SetMemoryReadCallback([&](std::uint16_t address) -> std::uint8_t { return romByte(0x07, address); });
        std::array<std::vector<float>, NesApuDeviceAdapter::kChannelCount> scratch;
        apu.Write(0x10, static_cast<std::uint8_t>(r10 & 0x0F));  // no loop, no IRQ: the sample is played once
        apu.Write(0x11, 0);
        apu.Write(0x12, static_cast<std::uint8_t>(r12));
        apu.Write(0x13, static_cast<std::uint8_t>(r13));
        apu.Write(0x15, 0x10);
        std::vector<int> seen;
        int previous = 0;
        const std::size_t samples = length * 8 * 430 / 4 + 100;  // the slowest rate is 428 CPU cycles per bit
        for (std::size_t i = 0; i < samples && seen.size() < ideal.size(); ++i) {
            apu.Render(4, scratch);
            const int level = apu.ChannelLevelForTest(NesApuDeviceAdapter::kDmc);
            if (level != previous) seen.push_back(level);
            previous = level;
            for (auto& c : scratch) c.clear();
        }
        Check(seen == ideal, (label + ": the chip's DMC levels are the levels decoded from the ROM bytes (least significant bit first)").c_str());
    }
}

void AkumajouDmcReadsDependOnTheBankOnlyForTheVoices() {
    std::printf("AkumajouDmcReadsDependOnTheBankOnlyForTheVoices\n");
    const char* romRoot = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRoot == nullptr || romRoot[0] == '\0') {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is not set - real-ROM bring-up not exercised\n");
        return;
    }
    AkumajouRig bank07, bank1e;
    Check(LoadAkumajouRig(bank07, romRoot, "0x07") && LoadAkumajouRig(bank1e, romRoot, "0x1E"), "akumajou: the cartridge loads with either bank at $C000");
    if (!bank07.loaded || !bank1e.loaded) return;

    // Banks 0x07 and 0x1E hold the same bytes up to $C8FF; from $C900 on they differ. So a sample is bank-dependent
    // exactly when it reaches $C900: the voices of requests 0x79, 0x7B, 0x7C and 0x7D, and request 0x77, whose
    // sample (193 bytes from $C840) ends with the byte at $C900. That one byte (the last eight bits of the sample)
    // is the tolerated difference; every other request must read the same bytes through both banks.
    std::vector<unsigned> differing;
    for (unsigned request = 0x01; request <= 0x7D; ++request) {
        const Played a = PlayAkumajouRequest(bank07, request, 200);
        const Played b = PlayAkumajouRequest(bank1e, request, 200);
        bool sameAddresses = a.reads.size() == b.reads.size();
        std::size_t differences = 0, lowest = 0xFFFF;
        for (std::size_t i = 0; sameAddresses && i < a.reads.size(); ++i) {
            if (a.reads[i].address != b.reads[i].address) sameAddresses = false;
            else if (a.reads[i].value != b.reads[i].value) {
                ++differences;
                lowest = std::min<std::size_t>(lowest, a.reads[i].address);
            }
        }
        char name[16];
        std::snprintf(name, sizeof(name), "request %02X", request);
        Check(sameAddresses, (std::string(name) + ": the same addresses are read through both banks").c_str());
        if (differences == 0) continue;
        differing.push_back(request);
        Check(lowest >= 0xC900, (std::string(name) + ": the bytes differ only from $C900 on").c_str());
        if (request == 0x77) Check(differences == 1, "request 77: the single byte at $C900 (the last byte of its sample) is the only difference");
    }
    const std::vector<unsigned> expected = {0x77, 0x79, 0x7B, 0x7C, 0x7D};
    Check(differing == expected, "only the voices (0x79, 0x7B, 0x7C, 0x7D) and the last byte of request 0x77 read different bytes through the two banks");
}
