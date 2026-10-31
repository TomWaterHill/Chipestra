// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Namco 163 tests. The synthetic program is hand-written; what each routine must leave behind is worked out here
// from the instructions and the banks the Machine is asked to map, not read back from the Machine.

#include "nes_namco163_tests.h"

#include "default_machine_catalog.h"
#include "definition_xml_locator.h"
#include "devices/nintendo/nes/nes_cartridge.h"
#include "devices/nintendo/nes/nes_driver_config.h"
#include "devices/nintendo/nes/nes_machine.h"
#include "devices/nintendo/nes/nes_namco163.h"
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

// 16 banks of 8 KB (128 KB). Bank k holds the marker 0x40+k in bytes $100-$1FF.
//   frame routine, at $8000 of bank 4 (the first call bank): record X, Y and what $A000+$100, $C000+$100 and
//   $E000+$100 show; the first time, program the chip (two channels: the waveform byte 0 = $FF, volume 3 for slot 6,
//   volume 5 for slot 7) and read the sound port back; every time, if the request variable $0020 is not 0, write
//   it to the volume of slot 6; leave X = 5, Y = 6.
constexpr std::uint16_t kFrameRoutine = 0x8000;
constexpr std::uint8_t kCallBank0 = 4, kCallBank1 = 5, kCallBank2 = 6;
constexpr std::uint8_t kIdleBank0 = 1, kIdleBank1 = 2, kIdleBank2 = 3;
constexpr std::size_t kBankCount = 16;

void Store(std::vector<std::uint8_t>& code, std::uint16_t address, std::uint8_t value) {
    const std::uint8_t bytes[] = {0xA9, value, 0x8D, static_cast<std::uint8_t>(address & 0xFF), static_cast<std::uint8_t>(address >> 8)};
    code.insert(code.end(), std::begin(bytes), std::end(bytes));
}
void Copy(std::vector<std::uint8_t>& code, std::uint16_t from, std::uint16_t to) {  // LDA from; STA to
    const std::uint8_t bytes[] = {0xAD, static_cast<std::uint8_t>(from & 0xFF), static_cast<std::uint8_t>(from >> 8),
                                  0x8D, static_cast<std::uint8_t>(to & 0xFF), static_cast<std::uint8_t>(to >> 8)};
    code.insert(code.end(), std::begin(bytes), std::end(bytes));
}

NesCartridge BuildN163Cartridge() {
    NesCartridge cart;
    cart.mapper = 19;
    cart.prg.assign(kBankCount * 0x2000, 0xEA);
    for (std::size_t bank = 0; bank < kBankCount; ++bank)
        for (std::size_t i = 0x100; i < 0x200; ++i) cart.prg[bank * 0x2000 + i] = static_cast<std::uint8_t>(0x40 + bank);

    std::vector<std::uint8_t> program;
    Store(program, 0xF800, 0x80);  // address 0, auto-increment
    Store(program, 0x4800, 0xFF);  // waveform byte 0
    Store(program, 0xF800, 0x77);
    Store(program, 0x4800, 0x03);  // slot 6: volume 3
    Store(program, 0xF800, 0x7F);
    Store(program, 0x4800, 0x15);  // slot 7: 2 channels, volume 5
    Store(program, 0xF800, 0x90);  // address $10, auto-increment
    Store(program, 0x4800, 0x21);
    Store(program, 0x4800, 0x32);
    Store(program, 0xF800, 0x90);
    Copy(program, 0x4800, 0x0305);  // reads advance with auto-increment: $21, then $32
    Copy(program, 0x4800, 0x0306);
    Store(program, 0xF800, 0x10);  // address $10, no auto-increment
    Copy(program, 0x4800, 0x0307);
    Copy(program, 0x4800, 0x0308);

    std::vector<std::uint8_t> frame = {
        0x8E, 0x03, 0x03,  // STX $0303
        0x8C, 0x04, 0x03,  // STY $0304
    };
    Copy(frame, 0xA100, 0x0300);
    Copy(frame, 0xC100, 0x0301);
    Copy(frame, 0xE100, 0x0302);
    frame.insert(frame.end(), {0xAD, 0x10, 0x03,                                      // LDA $0310
                               0xD0, static_cast<std::uint8_t>(3 + program.size()),   // BNE past the programming
                               0xEE, 0x10, 0x03});                                    // INC $0310
    frame.insert(frame.end(), program.begin(), program.end());
    // a request: LDA $0020; BEQ over; LDA #$77; STA $F800; LDA $0020; STA $4800
    std::vector<std::uint8_t> request;
    Store(request, 0xF800, 0x77);
    Copy(request, 0x0020, 0x4800);
    frame.insert(frame.end(), {0xAD, 0x20, 0x00, 0xF0, static_cast<std::uint8_t>(request.size())});
    frame.insert(frame.end(), request.begin(), request.end());
    frame.insert(frame.end(), {0xA2, 0x05, 0xA0, 0x06, 0x60});  // LDX #5; LDY #6; RTS
    std::copy(frame.begin(), frame.end(), cart.prg.begin() + kCallBank0 * 0x2000);
    return cart;
}

NesDriverConfig BuildN163Driver(std::uint8_t channels = 2, std::uint8_t volume = 4) {
    NesDriverConfig cfg;
    cfg.ramClear = std::pair<std::uint16_t, std::uint16_t>{0x0000, 0x07FF};
    cfg.frame = {kFrameRoutine};
    cfg.bgmVariable = std::uint16_t{0x0020};
    cfg.warmupFrames = 2;
    cfg.prgBanksCall = NesDriverConfig::PrgBanks{kCallBank0, kCallBank1, kCallBank2, 3};
    cfg.prgBanksIdle = NesDriverConfig::PrgBanks{kIdleBank0, kIdleBank1, kIdleBank2, 3};
    cfg.n163Channels = channels;
    cfg.n163Volume = volume;
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

void Namco163PrgMapsBanksAndDecodesTheSoundRegisters() {
    std::printf("Namco163PrgMapsBanksAndDecodesTheSoundRegisters\n");
    Namco163Prg prg(64);
    std::uint8_t soundAddress = 0;
    auto bank = [&](std::uint16_t address) { return prg.Offset(address) / 0x2000; };
    Check(bank(0x8000) == 0 && bank(0xA000) == 1 && bank(0xC000) == 62 && bank(0xE000) == 63, "power-on: banks 0 and 1, the one before the last, the last");
    Check(prg.Write(0xE000, 0x05, soundAddress) == Namco163Prg::Sound::Enable && bank(0x8000) == 5, "$E000 selects the bank at $8000 and leaves the sound on");
    Check(prg.Write(0xE800, 0x07, soundAddress) == Namco163Prg::Sound::None && bank(0xA000) == 7, "$E800 selects the bank at $A000");
    Check(prg.Write(0xF000, 0x7F, soundAddress) == Namco163Prg::Sound::None && bank(0xC000) == 0x3F, "$F000 selects the bank at $C000 and keeps 6 bits (0x7F -> 0x3F)");
    Check(bank(0xE000) == 63, "$E000-$FFFF stays the last bank");
    Check(prg.Offset(0x8123) == 5 * 0x2000 + 0x123, "an offset inside a bank is the address low 13 bits");
    Check(prg.Write(0xE7FF, 0x46, soundAddress) == Namco163Prg::Sound::Disable && bank(0x8000) == 6,
          "any address of $E000-$E7FF is the first register; bit 6 stops the sound and is not part of the bank");
    Check(prg.Write(0xE000, 0x06, soundAddress) == Namco163Prg::Sound::Enable, "clearing bit 6 starts the sound again");
    Check(prg.Write(0xF800, 0xC3, soundAddress) == Namco163Prg::Sound::Address && soundAddress == 0xC3 && prg.LastSoundAddressWrite() == 0xC3,
          "$F800 is the sound address register and gives the whole value (bit 7 = auto-increment)");
    Check(prg.Write(0xFFFF, 0x12, soundAddress) == Namco163Prg::Sound::Address && soundAddress == 0x12, "so does the rest of $F800-$FFFF");
    const std::uint8_t b0 = prg.Bank(0), b1 = prg.Bank(1), b2 = prg.Bank(2);
    for (std::uint16_t a : std::initializer_list<std::uint16_t>{0x8000, 0x8800, 0xB000, 0xB800, 0xC000, 0xC800, 0xD000, 0xD800})
        Check(prg.Write(a, 0x33, soundAddress) == Namco163Prg::Sound::None, "CHR and nametable registers do nothing here");
    Check(prg.Bank(0) == b0 && prg.Bank(1) == b1 && prg.Bank(2) == b2, "and change no PRG bank");
    prg.Reset();
    Check(bank(0x8000) == 0 && bank(0xC000) == 62, "Reset returns to the power-on state");
    prg.SetState(0x3C, 0x3D, 0x3E);
    Check(bank(0x8000) == 60 && bank(0xA000) == 61 && bank(0xC000) == 62, "SetState sets the three registers");
    Namco163Prg small(16);
    small.SetState(0x3C, 0x3D, 0x3E);
    Check(small.Offset(0x8000) / 0x2000 == 12 && small.Offset(0xA000) / 0x2000 == 13 && small.Offset(0xC000) / 0x2000 == 14 &&
              small.Offset(0xE000) / 0x2000 == 15,
          "a register value beyond the ROM wraps around its size (the address lines above are not connected)");
}

void NesNamco163CartridgeAndDriverSettings() {
    std::printf("NesNamco163CartridgeAndDriverSettings\n");
    auto mapperAndSize = [](const std::vector<std::uint8_t>& file) {
        try {
            const NesCartridge cart = ParseInesCartridge(file.data(), file.size());
            return std::make_pair(static_cast<int>(cart.mapper), cart.prg.size());
        } catch (const std::runtime_error&) {
            return std::make_pair(-1, std::size_t{0});
        }
    };
    // mapper 19: flags6 high nibble 3, flags7 high nibble 1
    Check(mapperAndSize(InesImage(8, 0x30, 0x10)) == std::make_pair(19, std::size_t{8 * 16384}), "mapper 19 with 128 KB parses");
    Check(mapperAndSize(InesImage(32, 0x30, 0x10)).first == 19, "mapper 19 with 512 KB parses");
    Check(mapperAndSize(InesImage(2, 0x30, 0x10)).first == 19, "mapper 19 with 32 KB parses");
    Check(mapperAndSize(InesImage(1, 0x30, 0x10)).first == -1, "a 16 KB Namco 163 image is rejected");
    Check(mapperAndSize(InesImage(6, 0x30, 0x10)).first == -1, "a Namco 163 PRG size that is not a power of two is rejected");
    Check(mapperAndSize(InesImage(64, 0x30, 0x10)).first == -1, "a Namco 163 PRG size above 512 KB is rejected");
    Check(mapperAndSize(InesImage(8, 0x20, 0x10)).first == -1, "mapper 18 is not supported");

    NesDriverConfig cfg;
    std::string error;
    Check(ParseNesDriverConfig("<driver init=\"0xB307\" frame=\"0x8000\" prg_banks_call=\"0x3C 0x3D 0x3E\" prg_banks_idle=\"0x3C 0x3D 0x3E\" "
                               "n163_channels=\"4\" n163_volume=\"4\"/>",
                               cfg, &error),
          "the Namco 163 driver settings parse");
    Check(cfg.n163Channels && *cfg.n163Channels == 4 && cfg.n163Volume && *cfg.n163Volume == 4, "the channel count and the volume number");
    Check(cfg.prgBanksCall && cfg.prgBanksCall->count == 3 && cfg.prgBanksCall->r8 == 0x3E, "three banks");
    NesDriverConfig plain;
    ParseNesDriverConfig("<driver init=\"0x8000\"/>", plain, &error);
    Check(!plain.n163Channels && !plain.n163Volume, "both settings are optional in the parser");
    NesDriverConfig edge;
    ParseNesDriverConfig("<driver n163_channels=\"1\" n163_volume=\"0\"/>", edge, &error);
    Check(edge.n163Channels && *edge.n163Channels == 1 && edge.n163Volume && *edge.n163Volume == 0, "1 channel and volume number 0 are valid");
    NesDriverConfig edge2;
    ParseNesDriverConfig("<driver n163_channels=\"8\" n163_volume=\"5\"/>", edge2, &error);
    Check(edge2.n163Channels && *edge2.n163Channels == 8 && edge2.n163Volume && *edge2.n163Volume == 5, "8 channels and volume number 5 are valid");
    auto rejects = [](const std::string& xml) {
        NesDriverConfig c;
        std::string e;
        return !ParseNesDriverConfig(xml, c, &e) && !e.empty();
    };
    Check(rejects("<driver n163_channels=\"0\"/>"), "0 channels is rejected");
    Check(rejects("<driver n163_channels=\"9\"/>"), "9 channels is rejected");
    Check(rejects("<driver n163_channels=\"two\"/>"), "a channel count that is not a number is rejected");
    Check(rejects("<driver n163_volume=\"6\"/>"), "a volume number above 5 is rejected");
    Check(rejects("<driver n163_volume=\"-1\"/>"), "a negative volume number is rejected");
}

void NesNamco163MachineInputsAreChecked() {
    std::printf("NesNamco163MachineInputsAreChecked\n");
    const std::string good =
        "<driver init=\"0xB307\" frame=\"0x8000\" prg_banks_call=\"0x3C 0x3D 0x3E\" prg_banks_idle=\"0x3C 0x3D 0x3E\" "
        "n163_channels=\"4\" n163_volume=\"4\"/>";
    auto resources = [&](const std::vector<std::uint8_t>& cart, const std::string& driver) {
        MachineResources r;
        r.Add(ResolvedResource("cart", cart));
        r.Add(ResolvedResource("driver_xml", std::vector<std::uint8_t>(driver.begin(), driver.end())));
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
    const std::vector<std::uint8_t> n163 = InesImage(8, 0x30, 0x10);
    bool ok = false;
    NesMachineInputs inputs;
    try {
        inputs = AssembleNesMachineInputs(resources(n163, good));
        ok = true;
    } catch (...) {
    }
    Check(ok && inputs.cartridge.mapper == 19, "a Namco 163 game assembles");
    Check(throwsWith(resources(n163, "<driver init=\"0xB307\" frame=\"0x8000\" n163_volume=\"4\"/>"), "n163_channels"),
          "a Namco 163 game without n163_channels is refused");
    Check(throwsWith(resources(n163, "<driver init=\"0xB307\" frame=\"0x8000\" n163_channels=\"4\"/>"), "n163_volume"),
          "a Namco 163 game without n163_volume is refused");
    Check(throwsWith(resources(n163, "<driver init=\"0xB307\" frame=\"0x8000\" prg_banks_call=\"1 2\" prg_banks_idle=\"1 2\" "
                                     "n163_channels=\"4\" n163_volume=\"4\"/>"),
                     "three banks"),
          "a Namco 163 game with two banks is refused");
    const std::vector<std::uint8_t> nrom = InesImage(2, 0x00, 0x00);
    Check(throwsWith(resources(nrom, "<driver init=\"0x8000\" frame=\"0x8100\" n163_channels=\"4\" n163_volume=\"4\"/>"), "Namco 163"),
          "the settings on another mapper are refused");
    Check(throwsWith(resources(nrom, "<driver init=\"0x8000\" frame=\"0x8100\" prg_banks_call=\"1 2 3\" prg_banks_idle=\"1 2 3\"/>"), "three banks"),
          "three banks on a mapper without them are refused");
    bool built = false;
    try {
        NesDriverConfig cfg = BuildN163Driver();
        cfg.n163Volume.reset();
        NesMachine machine(BuildN163Cartridge(), cfg);
    } catch (const std::invalid_argument&) {
        built = true;
    }
    Check(built, "a Machine for a Namco 163 cartridge cannot be built without the settings");
}

void NesMachineNamco163BankingSoundPortAndChannels() {
    std::printf("NesMachineNamco163BankingSoundPortAndChannels\n");
    auto machine = std::make_unique<NesMachine>(BuildN163Cartridge(), BuildN163Driver(2, 4));
    std::vector<std::tuple<std::uint64_t, unsigned, unsigned>> writes;
    machine->SetN163WriteObserverForTest([&](std::uint64_t frame, std::uint16_t address, std::uint8_t value) { writes.emplace_back(frame, address, value); });
    machine->Initialize();

    // The start-up ran the frame routine twice with the call banks mapped (4, 5, 6 at $8000/$A000/$C000, the last
    // bank 15 at $E000).
    Check(machine->RamForTest(0x0300) == 0x45, "during the call $A000 shows the second call bank (5)");
    Check(machine->RamForTest(0x0301) == 0x46, "during the call $C000 shows the third call bank (6)");
    Check(machine->RamForTest(0x0302) == 0x4F, "the last bank is fixed at $E000");
    Check(machine->RamForTest(0x0303) == 0 && machine->RamForTest(0x0304) == 0,
          "X and Y are 0 when the routine is called (the routine left them 5 and 6 the time before)");
    Check(machine->PrgOffsetForTest(0x8000) / 0x2000 == 1 && machine->PrgOffsetForTest(0xA000) / 0x2000 == 2 &&
              machine->PrgOffsetForTest(0xC000) / 0x2000 == 3 && machine->PrgOffsetForTest(0xE000) / 0x2000 == 15,
          "between the calls the idle banks are mapped");
    Check(writes.empty(), "the start-up writes to the chip are not reported to the observer");

    // The sound port: the routine wrote RAM $10 and $11 with auto-increment, then read them back.
    Check(machine->RamForTest(0x0305) == 0x21 && machine->RamForTest(0x0306) == 0x32, "reads of the data port advance with auto-increment");
    Check(machine->RamForTest(0x0307) == 0x21 && machine->RamForTest(0x0308) == 0x21, "without auto-increment a read returns the same byte again");
    const auto& ram = machine->N163ForTest()->RamForTest();
    Check(ram[0] == 0xFF && ram[0x10] == 0x21 && ram[0x11] == 0x32 && ram[0x77] == 3 && ram[0x7F] == 0x15, "the chip RAM holds what the routine wrote");

    // Channels: the APU's five, then the last two slots.
    Check(machine->LogicalChannels().size() == 7, "the Machine has 7 channels: 5 of the APU and 2 of the Namco 163");
    Check(machine->LogicalChannels()[5].DeviceName == "N163" && machine->LogicalChannels()[5].ChannelName == "Wave 7" &&
              machine->LogicalChannels()[6].ChannelName == "Wave 8",
          "the Namco 163 channels are the last two slots, named by slot number + 1");
    Check(machine->LogicalChannels()[0].RoutingGain == 1.8f && machine->LogicalChannels()[5].RoutingGain == 1.17f,
          "the APU is mixed at 0.90 x 2 and the chip at 0.5 x 2 times submapper 4's gain 1.17");
    Check(machine->LogicalChannels()[5].NativeSampleRate == 119318, "the chip makes 119318 samples a second");
    Check(NesMachine::N163RoutingGain(0) == 1.0f && NesMachine::N163RoutingGain(1) == 1.0f && NesMachine::N163RoutingGain(2) == 0.0f &&
              NesMachine::N163RoutingGain(3) == 1.125f && NesMachine::N163RoutingGain(4) == 1.17f && NesMachine::N163RoutingGain(5) == 1.19f,
          "the gain of each submapper");
    NesMachine one(BuildN163Cartridge(), BuildN163Driver(1, 3));
    Check(one.LogicalChannels().size() == 6 && one.LogicalChannels()[5].ChannelName == "Wave 8" && one.LogicalChannels()[5].RoutingGain == 1.125f,
          "one channel is the last slot");
    NesMachine eight(BuildN163Cartridge(), BuildN163Driver(8, 0));
    Check(eight.LogicalChannels().size() == 13 && eight.LogicalChannels()[5].ChannelName == "Wave 1" && eight.LogicalChannels()[5].RoutingGain == 1.0f,
          "eight channels start at slot 0");

    auto peak = [](LogicalChannelSink& s, std::size_t channel) {
        const LogicalChannelBuffer& b = s.ChannelBuffer(static_cast<std::uint32_t>(channel));
        float p = 0.0f;
        for (std::size_t i = 0; i < b.Size(); ++i) p = std::max(p, std::fabs(b[i]));
        return p;
    };
    LogicalChannelSink sink;
    machine->Render(EmuDuration(3300), &sink);
    // Waveform sample 15 gives (15 - 8) * volume / 128; the first sample of a block is not changed by the DC filter.
    Check(std::fabs(peak(sink, 6) - 7.0f * 5.0f / 128.0f) < 1e-5f, "slot 7 (volume 5) makes 7 * 5 / 128");
    Check(std::fabs(peak(sink, 5) - 7.0f * 3.0f / 128.0f) < 1e-5f, "slot 6 (volume 3) makes 7 * 3 / 128");
    Check(peak(sink, 0) == 0.0f, "the APU is silent");
    // A mute holds from the start (a Stop resets the Machine, and with it the DC filters).
    machine->LogicalChannels()[5].Mute = true;
    MusicCommand stop;
    stop.Kind = MusicCommandKind::Stop;
    machine->SendCommand(stop);
    sink.ClearAll();
    machine->Render(EmuDuration(3000), &sink);
    Check(sink.ChannelBuffer(5).Size() > 0 && peak(sink, 5) == 0.0f, "a muted Namco 163 channel makes silence");
    Check(peak(sink, 6) > 0.0f, "the other Namco 163 channel still sounds");
    machine->LogicalChannels()[5].Mute = false;

    // A request is a write of the number to the driver variable; the routine passes it on to the chip.
    MusicCommand song;
    song.Kind = MusicCommandKind::Raw;
    song.Value = 9;
    writes.clear();
    machine->SendCommand(song);
    sink.ClearAll();
    machine->Render(EmuDuration(3000), &sink);  // one frame starts
    const std::vector<std::tuple<std::uint64_t, unsigned, unsigned>> expected = {{1, 0xF800, 0x77}, {1, 0x4800, 9}};
    Check(writes == expected, "the routine sound register writes reach the observer in order, in frame 1 (the mute test rendered frame 0)");
    Check(machine->N163ForTest()->RamForTest()[0x77] == 9, "the chip got the value");
    machine->Render(EmuDuration(3000), &sink);
    Check(machine->N163ForTest()->RamForTest()[0x77] == 9 && std::get<0>(writes.back()) == 2, "the variable stays, so the routine writes again in the next frame");

    // Switching resets the whole Machine, the chip RAM included.
    MusicCommand next = song;
    next.Value = 1;
    machine->SendCommand(next);
    Check(machine->N163ForTest()->RamForTest()[0x77] == 3 && machine->N163ForTest()->RamForTest()[0x10] == 0x21,
          "a second command first resets the Machine: the chip is programmed as after start-up again");
    machine->SendCommand(stop);
    Check(machine->RamForTest(0x0020) == 0 && machine->PrgOffsetForTest(0xA000) / 0x2000 == 2, "Music Stop (reset) cancels the request and returns to the idle banks");
}

void RollingThunderViaDefinitionXmlRealRomMatchesTheRealGame() {
    std::printf("RollingThunderViaDefinitionXmlRealRomMatchesTheRealGame\n");
    const auto path = FindDefinitionXml("rthunder.xml");
    Check(path.has_value(), "rthunder: rthunder.xml is present (run from the repo root)");
    if (!path) return;
    const auto text = ReadFileText(*path);
    Check(text.has_value(), "rthunder: file is readable");
    if (!text) return;
    const XmlParseResult parsed = ParseGameDefinition(*text);
    Check(parsed.Status == XmlParseStatus::Ok, "rthunder: rthunder.xml parses cleanly");
    if (parsed.Status != XmlParseStatus::Ok) return;
    const GameDefinition& def = parsed.Definition;
    Check(def.Machine == "NINTENDO_NES" && def.Variant == "NES_BASE", "rthunder: machine NINTENDO_NES / variant NES_BASE");

    const char* romRootEnv = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRootEnv == nullptr || romRootEnv[0] == '\0') {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is not set - real-ROM bring-up not exercised\n");
        return;
    }
    const ResourceResolver resolver{std::filesystem::path(romRootEnv)};
    ResourceResolutionResult res = resolver.Resolve(def.BuildRequiredResources());
    Check(res.Ok, "rthunder: the cartridge resolves under CHIPESTRA_ROM_ROOT");
    if (!res.Ok) return;
    const std::string driverXml = ExtractDriverElementXml(*text);
    Check(!driverXml.empty(), "rthunder: the <driver> element is present");
    res.Resources.Add(ResolvedResource("driver_xml", std::vector<std::uint8_t>(driverXml.begin(), driverXml.end())));

    auto create = [&]() {
        std::unique_ptr<IMachine> m = DefaultMachineRegistry().Create(MachineId(def.Machine), VariantId(def.Variant), res.Resources);
        if (m) m->Initialize();
        return m;
    };

    // The requests of the sound driver: 0x80-0xA5 (0xFE and 0xFF are not sounds: the first depends on the Y register
    // the game was interrupted with, the second is the game start-up request). The recordings of the real game
    // live in nes/rthunder/real_game below CHIPESTRA_REFERENCE_DIR: one file per request (req_<number>.txt:
    // "<update> <address> <value>", updates counted from the request; $4800 is the data port of the chip and $F800
    // its address register). The XML tracks are not what is compared (a track can be removed or renamed), but each
    // track must be one of the requests.
    std::vector<unsigned> requests;
    for (unsigned v = 0x80; v <= 0xA5; ++v) requests.push_back(v);
    const auto stopTrack = std::find_if(def.Tracks.begin(), def.Tracks.end(), [](const TrackDefinition& t) { return t.IsControlCommand; });
    Check(stopTrack != def.Tracks.end(), "rthunder: the XML has a Music Stop control track");
    for (const TrackDefinition& track : def.Tracks) {
        if (track.IsControlCommand) continue;
        const unsigned value = static_cast<unsigned>(std::strtoul(track.Command.c_str(), nullptr, 0));
        Check(std::find(requests.begin(), requests.end(), value) != requests.end(), (track.Id + ": is one of the requests the driver plays").c_str());
    }

    const char* referenceRoot = GetEnvVar("CHIPESTRA_REFERENCE_DIR");
    const bool haveReference = referenceRoot != nullptr && referenceRoot[0] != '\0';
    const std::filesystem::path referenceDir =
        haveReference ? std::filesystem::path(referenceRoot) / "nes" / "rthunder" / "real_game" : std::filesystem::path();
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
        machine.SetN163WriteObserverForTest([&](std::uint64_t frame, std::uint16_t address, std::uint8_t value) {
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
                std::printf("      recording update %llu %04X=%02X\n", static_cast<unsigned long long>(expected[i].frame), expected[i].address,
                            expected[i].value);
            if (i < writes.size())
                std::printf("      Chipestra update %llu %04X=%02X\n", static_cast<unsigned long long>(writes[i].frame), writes[i].address,
                            writes[i].value);
        }
        Check(same, (label + ": all APU and Namco 163 writes of the first 600 updates equal the real game's (update, address, value)").c_str());
        ++compared;
    }
    if (haveReference) {
        std::printf("  %zu requests compared with the recording of the real game\n", compared);
        Check(compared == requests.size(), "rthunder: every request was compared with a recording");
    }
}
