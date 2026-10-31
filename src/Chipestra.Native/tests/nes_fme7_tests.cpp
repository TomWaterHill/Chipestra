// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// FME-7 / Sunsoft 5B tests. The synthetic program is hand-written; what each routine must leave behind is worked
// out here from the instructions and the banks the Machine is asked to map, not read back from the Machine.

#include "nes_fme7_tests.h"

#include "default_machine_catalog.h"
#include "definition_xml_locator.h"
#include "devices/nintendo/nes/nes_cartridge.h"
#include "devices/nintendo/nes/nes_driver_config.h"
#include "devices/nintendo/nes/nes_fme7.h"
#include "devices/nintendo/nes/nes_machine.h"
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
#include <iterator>
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

// 32 banks of 8 KB (256 KB). Bank k holds the marker 0x40+k in bytes $100-$1FF.
//   frame routine, at $8000 of bank 4 (the call bank of command 9): record X, Y and what $A000+$100, $C000+$100 and
//   $E000+$100 show; the first time, program the chip (register 8 = 15: volume of tone A, register 7 = $3E: tone A
//   only, register 0 = 1: the shortest period) and choose the bank at $C000 for itself (command B = 9); every time,
//   if the request variable $0020 is not 0, write it to the volume of tone B; leave X = 5, Y = 6.
//   request routine, at $8300 of the same bank: store A in $0330 and count the call in $0331.
constexpr std::uint16_t kFrameRoutine = 0x8000;
constexpr std::uint16_t kRequestRoutine = 0x8300;
constexpr std::uint8_t kCallBank9 = 4, kCallBankA = 5;
constexpr std::uint8_t kIdleBank9 = 1, kIdleBankA = 2;
constexpr std::uint8_t kDriverBankB = 9;
constexpr std::size_t kBankCount = 32;

void Store(std::vector<std::uint8_t>& code, std::uint16_t address, std::uint8_t value) {
    const std::uint8_t bytes[] = {0xA9, value, 0x8D, static_cast<std::uint8_t>(address & 0xFF), static_cast<std::uint8_t>(address >> 8)};
    code.insert(code.end(), std::begin(bytes), std::end(bytes));
}
void Copy(std::vector<std::uint8_t>& code, std::uint16_t from, std::uint16_t to) {  // LDA from; STA to
    const std::uint8_t bytes[] = {0xAD, static_cast<std::uint8_t>(from & 0xFF), static_cast<std::uint8_t>(from >> 8),
                                  0x8D, static_cast<std::uint8_t>(to & 0xFF), static_cast<std::uint8_t>(to >> 8)};
    code.insert(code.end(), std::begin(bytes), std::end(bytes));
}

NesCartridge BuildFme7Cartridge() {
    NesCartridge cart;
    cart.mapper = 69;
    cart.prg.assign(kBankCount * 0x2000, 0xEA);
    for (std::size_t bank = 0; bank < kBankCount; ++bank)
        for (std::size_t i = 0x100; i < 0x200; ++i) cart.prg[bank * 0x2000 + i] = static_cast<std::uint8_t>(0x40 + bank);

    std::vector<std::uint8_t> program;
    Store(program, 0xC000, 0x08);
    Store(program, 0xE000, 0x0F);  // tone A: volume 15
    Store(program, 0xC000, 0x07);
    Store(program, 0xE000, 0x3E);  // mixer: tone A only
    Store(program, 0xC000, 0x00);
    Store(program, 0xE000, 0x01);  // tone A: the shortest period
    Store(program, 0x8000, 0x0B);
    Store(program, 0xA000, kDriverBankB);  // command B: the driver's own choice of the bank at $C000

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
    std::vector<std::uint8_t> request;
    Store(request, 0xC000, 0x09);
    Copy(request, 0x0020, 0xE000);
    frame.insert(frame.end(), {0xAD, 0x20, 0x00, 0xF0, static_cast<std::uint8_t>(request.size())});
    frame.insert(frame.end(), request.begin(), request.end());
    frame.insert(frame.end(), {0xA2, 0x05, 0xA0, 0x06, 0x60});  // LDX #5; LDY #6; RTS
    std::copy(frame.begin(), frame.end(), cart.prg.begin() + kCallBank9 * 0x2000);

    const std::uint8_t requestCode[] = {0x8D, 0x30, 0x03, 0xEE, 0x31, 0x03, 0x60};  // STA $0330; INC $0331; RTS
    std::copy(std::begin(requestCode), std::end(requestCode), cart.prg.begin() + kCallBank9 * 0x2000 + (kRequestRoutine - 0x8000));
    return cart;
}

NesDriverConfig BuildFme7Driver(bool requestCall) {
    NesDriverConfig cfg;
    cfg.ramClear = std::pair<std::uint16_t, std::uint16_t>{0x0000, 0x07FF};
    cfg.frame = {kFrameRoutine};
    if (requestCall) cfg.requestCall = kRequestRoutine;
    else cfg.bgmVariable = std::uint16_t{0x0020};
    cfg.warmupFrames = 2;
    cfg.prgBanksCall = NesDriverConfig::PrgBanks{kCallBank9, kCallBankA, 0, 2};
    cfg.prgBanksIdle = NesDriverConfig::PrgBanks{kIdleBank9, kIdleBankA, 0, 2};
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

void Fme7PrgMapsBanksAndRoutesTheSoundRegisters() {
    std::printf("Fme7PrgMapsBanksAndRoutesTheSoundRegisters\n");
    Fme7Prg prg(32);
    auto bank = [&](std::uint16_t address) { return prg.Offset(address) / 0x2000; };
    Check(bank(0x8000) == 0 && bank(0xA000) == 1 && bank(0xC000) == 30 && bank(0xE000) == 31, "power-on: banks 0 and 1, the one before the last, the last");
    Check(prg.Write(0x8000, 0x09) == Fme7Prg::Sound::None && prg.Command() == 9, "$8000 selects a command");
    Check(prg.Write(0xA000, 0x07) == Fme7Prg::Sound::None && bank(0x8000) == 7, "command 9 data ($A000) selects the bank at $8000");
    prg.Write(0x9FFF, 0x0A);
    prg.Write(0xBFFF, 0x05);
    Check(prg.Command() == 0x0A && bank(0xA000) == 5, "any address of $8000-$9FFF is the command register, of $A000-$BFFF the data; command A: the bank at $A000");
    prg.Write(0x8000, 0x1B);
    prg.Write(0xA000, 0x12);
    Check(prg.Command() == 0x0B && bank(0xC000) == 0x12, "the command keeps its low four bits (0x1B -> command B): the bank at $C000");
    Check(bank(0xE000) == 31, "$E000-$FFFF stays the last bank");
    Check(prg.Offset(0x8123) == 7 * 0x2000 + 0x123, "an offset inside a bank is the address low 13 bits");
    prg.Write(0xA000, 0xFF);
    Check(prg.Offset(0xC000) / 0x2000 == 31, "a bank number beyond the ROM wraps around its size");
    const std::uint8_t b0 = prg.Bank(0), b1 = prg.Bank(1), b2 = prg.Bank(2);
    for (std::uint8_t command : {0, 1, 2, 3, 4, 5, 6, 7, 8, 0x0C, 0x0D, 0x0E, 0x0F}) {
        prg.Write(0x8000, command);
        Check(prg.Write(0xA000, 0x33) == Fme7Prg::Sound::None, "the CHR, RAM, mirroring and IRQ commands do nothing here");
    }
    Check(prg.Bank(0) == b0 && prg.Bank(1) == b1 && prg.Bank(2) == b2, "and change no PRG bank");
    Check(prg.Write(0xC000, 0x08) == Fme7Prg::Sound::Address, "$C000 is the sound register select");
    Check(prg.Write(0xDFFF, 0x08) == Fme7Prg::Sound::Address, "so is the rest of $C000-$DFFF");
    Check(prg.Write(0xE000, 0x0F) == Fme7Prg::Sound::Data, "$E000 is the sound data");
    Check(prg.Write(0xFFFF, 0x0F) == Fme7Prg::Sound::Data, "so is the rest of $E000-$FFFF");
    prg.Reset();
    Check(bank(0x8000) == 0 && bank(0xC000) == 30 && prg.Command() == 0, "Reset returns to the power-on state");
    prg.SetState(7, 0, 15);
    Check(bank(0x8000) == 7 && bank(0xA000) == 0 && bank(0xC000) == 15, "SetState sets the three bank registers");
    Fme7Prg small(8);
    small.SetState(0x0C, 0x0D, 0x0E);
    Check(small.Offset(0x8000) / 0x2000 == 4 && small.Offset(0xA000) / 0x2000 == 5 && small.Offset(0xC000) / 0x2000 == 6 &&
              small.Offset(0xE000) / 0x2000 == 7,
          "a register value beyond a small ROM wraps around its size");
}

void NesFme7CartridgeAndDriverSettings() {
    std::printf("NesFme7CartridgeAndDriverSettings\n");
    auto mapperAndSize = [](const std::vector<std::uint8_t>& file) {
        try {
            const NesCartridge cart = ParseInesCartridge(file.data(), file.size());
            return std::make_pair(static_cast<int>(cart.mapper), cart.prg.size());
        } catch (const std::runtime_error&) {
            return std::make_pair(-1, std::size_t{0});
        }
    };
    // mapper 69: flags6 high nibble 5, flags7 high nibble 4
    Check(mapperAndSize(InesImage(16, 0x50, 0x40)) == std::make_pair(69, std::size_t{16 * 16384}), "mapper 69 with 256 KB parses");
    Check(mapperAndSize(InesImage(32, 0x50, 0x40)).first == 69, "mapper 69 with 512 KB parses");
    Check(mapperAndSize(InesImage(2, 0x50, 0x40)).first == 69, "mapper 69 with 32 KB parses");
    Check(mapperAndSize(InesImage(1, 0x50, 0x40)).first == -1, "a 16 KB FME-7 image is rejected");
    Check(mapperAndSize(InesImage(6, 0x50, 0x40)).first == -1, "an FME-7 PRG size that is not a power of two is rejected");
    Check(mapperAndSize(InesImage(64, 0x50, 0x40)).first == -1, "an FME-7 PRG size above 512 KB is rejected");
    Check(mapperAndSize(InesImage(16, 0x60, 0x40)).first == -1, "mapper 70 is not supported");

    NesDriverConfig cfg;
    std::string error;
    Check(ParseNesDriverConfig("<driver init=\"0x8000\" frame=\"0x8009\" request_call=\"0x8006\" prg_banks_call=\"0x07 0x00\" prg_banks_idle=\"0x07 0x00\"/>", cfg, &error),
          "the FME-7 driver settings parse");
    Check(cfg.prgBanksCall && cfg.prgBanksCall->count == 2 && cfg.prgBanksCall->r6 == 7 && cfg.prgBanksCall->r7 == 0, "two banks (commands 9 and A)");

    // The Auxiliary byte of a track: the request number is 3 bits, bit 7 marks a control track.
    Check(NesDriverConfig::IsControl(0x80) && !NesDriverConfig::IsControl(0x7F), "bit 7 of the Auxiliary byte is the control flag");
    Check(NesDriverConfig::RequestNumberOf(0xF5) == 7 && NesDriverConfig::SetNumberOf(0xF5) == 5, "the control flag is not part of the request number");
    Check(NesDriverConfig::PackSelector(3, 6) == 0x63, "a selector is (request << 4) | set");
    NesDriverConfig tooMany;
    Check(!ParseNesDriverConfig("<driver requests=\"0x01 0x02 0x03 0x04 0x05 0x06 0x07 0x08\"/>", tooMany, &error), "more than 7 request addresses are refused");
    NesDriverConfig seven;
    Check(ParseNesDriverConfig("<driver requests=\"0x01 0x02 0x03 0x04 0x05 0x06 0x07\"/>", seven, &error), "7 request addresses are accepted");

    // The parser sets the control flag.
    const std::string xml =
        "<chipestra><game id=\"t\" title=\"T\" machine=\"NINTENDO_NES\" variant=\"NES_BASE\">"
        "<resources><rom name=\"cart\" file=\"t.nes\" size=\"16\"/></resources>"
        "<tracks><track id=\"STOP\" title=\"Music Stop\" command=\"0x00\" type=\"control\"/>"
        "<track id=\"A\" title=\"A\" command=\"0x00\"/></tracks></game></chipestra>";
    const XmlParseResult parsed = ParseGameDefinition(xml);
    Check(parsed.Status == XmlParseStatus::Ok && parsed.Definition.Tracks.size() == 2, "a small game definition parses");
    if (parsed.Status == XmlParseStatus::Ok && parsed.Definition.Tracks.size() == 2) {
        Check(parsed.Definition.Tracks[0].Auxiliary == 0x80, "a control track gets the control flag");
        Check(parsed.Definition.Tracks[1].Auxiliary == 0x00, "any other track does not (its command 0 is a request)");
    }

    // Machine inputs.
    const std::string good = "<driver init=\"0x8000\" frame=\"0x8009\" request_call=\"0x8006\" prg_banks_call=\"0x07 0x00\" prg_banks_idle=\"0x07 0x00\"/>";
    auto resources = [&](const std::vector<std::uint8_t>& cart, const std::string& driver) {
        MachineResources r;
        r.Add(ResolvedResource("cart", cart));
        r.Add(ResolvedResource("driver_xml", std::vector<std::uint8_t>(driver.begin(), driver.end())));
        return r;
    };
    bool ok = false;
    NesMachineInputs inputs;
    try {
        inputs = AssembleNesMachineInputs(resources(InesImage(16, 0x50, 0x40), good));
        ok = true;
    } catch (...) {
    }
    Check(ok && inputs.cartridge.mapper == 69, "an FME-7 game assembles");
    bool refused = false;
    try {
        AssembleNesMachineInputs(resources(InesImage(16, 0x50, 0x40), "<driver init=\"0x8000\" frame=\"0x8009\" prg_banks_call=\"1 2 3\" prg_banks_idle=\"1 2 3\"/>"));
    } catch (const std::runtime_error&) {
        refused = true;
    }
    Check(refused, "an FME-7 game with three banks is refused");
}

void NesMachineFme7BankingSoundAndChannels() {
    std::printf("NesMachineFme7BankingSoundAndChannels\n");
    auto machine = std::make_unique<NesMachine>(BuildFme7Cartridge(), BuildFme7Driver(false));
    std::vector<std::tuple<std::uint64_t, unsigned, unsigned>> writes;
    machine->SetSunsoft5bWriteObserverForTest([&](std::uint64_t frame, std::uint16_t address, std::uint8_t value) { writes.emplace_back(frame, address, value); });
    machine->Initialize();

    // The start-up ran the frame routine twice with the call banks mapped (4 at $8000, 5 at $A000). The first run saw
    // the starting bank 0 at $C000 (NesMachine::kFme7InitialBankB) and then chose bank 9; the second run saw that.
    Check(machine->RamForTest(0x0300) == 0x45, "during the call $A000 shows the second call bank (5)");
    Check(machine->RamForTest(0x0301) == 0x40 + kDriverBankB, "$C000 shows the bank the driver chose: the generated code never writes command B");
    Check(machine->RamForTest(0x0302) == 0x5F, "the last bank (31) is fixed at $E000");
    Check(machine->RamForTest(0x0303) == 0 && machine->RamForTest(0x0304) == 0,
          "X and Y are 0 when the routine is called (the routine left them 5 and 6 the time before)");
    Check(machine->PrgOffsetForTest(0x8000) / 0x2000 == kIdleBank9 && machine->PrgOffsetForTest(0xA000) / 0x2000 == kIdleBankA &&
              machine->PrgOffsetForTest(0xC000) / 0x2000 == kDriverBankB && machine->PrgOffsetForTest(0xE000) / 0x2000 == 31,
          "between the calls the idle banks of commands 9 and A are mapped and the driver's bank B is kept");
    Check(writes.empty(), "the start-up writes to the chip are not reported to the observer");
    Check(machine->Sunsoft5bForTest()->RegisterForTest(8) == 0x0F && machine->Sunsoft5bForTest()->RegisterForTest(7) == 0x3E &&
              machine->Sunsoft5bForTest()->RegisterForTest(0) == 0x01,
          "the chip holds what the routine wrote through $C000 and $E000");

    NesMachine fresh(BuildFme7Cartridge(), BuildFme7Driver(false));
    fresh.Initialize();
    Check(NesMachine::kFme7InitialBankB == 0, "the bank at $C000 starts as bank 0");

    // Channels: the APU's five, then the three tones.
    Check(machine->LogicalChannels().size() == 8, "the Machine has 8 channels: 5 of the APU and 3 of the 5B");
    Check(machine->LogicalChannels()[5].DeviceName == "5B" && machine->LogicalChannels()[5].ChannelName == "Tone 1" &&
              machine->LogicalChannels()[7].ChannelName == "Tone 3",
          "the 5B channels are Tone 1-3");
    Check(machine->LogicalChannels()[0].RoutingGain == 1.8f && machine->LogicalChannels()[5].RoutingGain == 1.0f,
          "the APU is mixed at 0.90 x 2 and the chip at 0.50 x 2");
    Check(machine->LogicalChannels()[5].NativeSampleRate == 111860, "the chip's stream is 111860 samples a second");

    auto peak = [](LogicalChannelSink& s, std::size_t channel) {
        const LogicalChannelBuffer& b = s.ChannelBuffer(static_cast<std::uint32_t>(channel));
        float p = 0.0f;
        for (std::size_t i = 0; i < b.Size(); ++i) p = std::max(p, std::fabs(b[i]));
        return p;
    };
    LogicalChannelSink sink;
    machine->Render(EmuDuration(192000), &sink);  // one second
    const std::size_t made = sink.ChannelBuffer(5).Size();
    Check(made + 3 >= 111860 && made <= 111860 + 3, "a second of the chip is 111860 samples (the original's stream rate, not the CPU clock / 16)");
    Check(sink.ChannelBuffer(6).Size() == made && sink.ChannelBuffer(7).Size() == made, "every 5B channel gets every sample");
    Check(peak(sink, 5) > 0.0f, "tone A sounds");
    Check(peak(sink, 0) == 0.0f, "the APU is silent");

    // A mute takes the channel out of the mix: the other channels then carry the chip's output without it, and the
    // muted channel's own buffer keeps what it makes unmuted.
    auto render = [&](bool mute) {
        auto m = std::make_unique<NesMachine>(BuildFme7Cartridge(), BuildFme7Driver(false));
        m->Initialize();
        m->LogicalChannels()[5].Mute = mute;
        LogicalChannelSink s;
        m->Render(EmuDuration(19200), &s);
        std::vector<std::vector<float>> out;
        for (std::uint32_t c = 5; c < 8; ++c) {
            const LogicalChannelBuffer& b = s.ChannelBuffer(c);
            out.emplace_back(b.Size());
            for (std::size_t i = 0; i < b.Size(); ++i) out.back()[i] = b[i];
        }
        return out;
    };
    const auto open = render(false), muted = render(true);
    Check(open[0] == muted[0], "the muted channel's own buffer keeps its unmuted share");
    Check(open[1] != muted[1] || open[2] != muted[2], "the other channels change when a channel is muted");

    // A request is a write of the number to the driver variable; the routine passes it on to the chip.
    std::vector<std::tuple<std::uint64_t, unsigned, unsigned>> requestWrites;
    fresh.SetSunsoft5bWriteObserverForTest([&](std::uint64_t frame, std::uint16_t address, std::uint8_t value) { requestWrites.emplace_back(frame, address, value); });
    MusicCommand song;
    song.Kind = MusicCommandKind::Raw;
    song.Value = 9;
    fresh.SendCommand(song);
    LogicalChannelSink freshSink;
    fresh.Render(EmuDuration(3000), &freshSink);
    const std::vector<std::tuple<std::uint64_t, unsigned, unsigned>> expected = {{0, 0xC000, 9}, {0, 0xE000, 9}};
    Check(requestWrites == expected, "the routine's writes reach the observer in order: $C000 (register select), $E000 (data)");
    Check(fresh.Sunsoft5bForTest()->RegisterForTest(9) == 9, "the chip got the value");
}

void NesMachineControlTracksMakeNoRequestAndZeroIsARequest() {
    std::printf("NesMachineControlTracksMakeNoRequestAndZeroIsARequest\n");
    // A driver called with the number in A.
    {
        NesMachine machine(BuildFme7Cartridge(), BuildFme7Driver(true));
        machine.Initialize();
        LogicalChannelSink sink;
        MusicCommand zero;
        zero.Kind = MusicCommandKind::Raw;
        zero.Value = 0;
        Check(machine.AcceptsCommand(zero), "request_call: a track with command 0 is accepted");
        machine.SendCommand(zero);
        machine.Render(EmuDuration(3300), &sink);
        Check(machine.RamForTest(0x0331) == 1 && machine.RamForTest(0x0330) == 0, "request_call: a track that is not a control track requests number 0 too");
        MusicCommand seven = zero;
        seven.Value = 7;
        machine.SendCommand(seven);
        sink.ClearAll();
        machine.Render(EmuDuration(3300), &sink);
        Check(machine.RamForTest(0x0331) == 1 && machine.RamForTest(0x0330) == 7, "request_call: a second command starts again and requests its number once");
        MusicCommand control = zero;
        control.Auxiliary = NesDriverConfig::kControlFlag;
        Check(machine.AcceptsCommand(control), "request_call: a control track is accepted");
        machine.SendCommand(control);
        sink.ClearAll();
        machine.Render(EmuDuration(6600), &sink);
        Check(machine.RamForTest(0x0331) == 0, "request_call: a control track resets the Machine and makes no request");
        NesMachine first(BuildFme7Cartridge(), BuildFme7Driver(true));
        first.Initialize();
        first.SendCommand(control);
        LogicalChannelSink sink2;
        first.Render(EmuDuration(6600), &sink2);
        Check(first.RamForTest(0x0331) == 0, "request_call: a control track as the first command makes no request either");
    }
    // A driver with a RAM variable.
    {
        NesMachine machine(BuildFme7Cartridge(), BuildFme7Driver(false));
        machine.Initialize();
        MusicCommand song;
        song.Kind = MusicCommandKind::Raw;
        song.Value = 5;
        machine.SendCommand(song);
        Check(machine.RamForTest(0x0020) == 5, "variable: a command writes its number");
        MusicCommand control;
        control.Kind = MusicCommandKind::Raw;
        control.Value = 0;
        control.Auxiliary = NesDriverConfig::kControlFlag;
        machine.SendCommand(control);
        Check(machine.RamForTest(0x0020) == 0, "variable: a control track resets the Machine (the earlier request is gone)");
        song.Value = 0;
        machine.SendCommand(song);
        Check(machine.RamForTest(0x0020) == 0 && machine.AcceptsCommand(song), "variable: command 0 of a track that is not a control track is written like any other");
    }
}

void GimmickViaDefinitionXmlRealRomMatchesTheRealGame() {
    std::printf("GimmickViaDefinitionXmlRealRomMatchesTheRealGame\n");
    const auto path = FindDefinitionXml("gimmick.xml");
    Check(path.has_value(), "gimmick: gimmick.xml is present (run from the repo root)");
    if (!path) return;
    const auto text = ReadFileText(*path);
    Check(text.has_value(), "gimmick: file is readable");
    if (!text) return;
    const XmlParseResult parsed = ParseGameDefinition(*text);
    Check(parsed.Status == XmlParseStatus::Ok, "gimmick: gimmick.xml parses cleanly");
    if (parsed.Status != XmlParseStatus::Ok) return;
    const GameDefinition& def = parsed.Definition;
    Check(def.Machine == "NINTENDO_NES" && def.Variant == "NES_BASE", "gimmick: machine NINTENDO_NES / variant NES_BASE");

    // The requests of the sound driver: 0x00-0x6E (0x6F and 0x70 are past its table, 0xFF stops everything). The
    // XML tracks are not what is compared (a track can be removed or renamed), but each track must be one of them.
    // The recordings count updates from the one that follows the request; this Machine makes the request at the end of
    // the first frame after the start (after that frame's update), so its frame 1 is the recordings' update 0 and its
    // frame 0 (the driver still idle) is left out.
    std::vector<unsigned> requests;
    for (unsigned v = 0x00; v <= 0x6E; ++v) requests.push_back(v);
    const auto stopTrack = std::find_if(def.Tracks.begin(), def.Tracks.end(), [](const TrackDefinition& t) { return t.IsControlCommand; });
    Check(stopTrack != def.Tracks.end(), "gimmick: the XML has a Music Stop control track");
    if (stopTrack != def.Tracks.end()) Check(stopTrack->Auxiliary == 0x80, "gimmick: the Music Stop carries the control flag");
    std::vector<unsigned> seen;
    for (const TrackDefinition& track : def.Tracks) {
        if (track.IsControlCommand) continue;
        const unsigned value = static_cast<unsigned>(std::strtoul(track.Command.c_str(), nullptr, 0));
        Check(std::find(requests.begin(), requests.end(), value) != requests.end(), (track.Id + ": is one of the requests the driver plays").c_str());
        Check(std::find(seen.begin(), seen.end(), value) == seen.end(), (track.Id + ": its request is used once").c_str());
        Check(value != 0x3F, (track.Id + ": 0x3F is the same song as 0x3C and is left out").c_str());
        seen.push_back(value);
    }

    const char* romRootEnv = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRootEnv == nullptr || romRootEnv[0] == '\0') {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is not set - real-ROM bring-up not exercised\n");
        return;
    }
    const ResourceResolver resolver{std::filesystem::path(romRootEnv)};
    ResourceResolutionResult res = resolver.Resolve(def.BuildRequiredResources());
    Check(res.Ok, "gimmick: the cartridge resolves under CHIPESTRA_ROM_ROOT");
    if (!res.Ok) return;
    const std::string driverXml = ExtractDriverElementXml(*text);
    Check(!driverXml.empty(), "gimmick: the <driver> element is present");
    res.Resources.Add(ResolvedResource("driver_xml", std::vector<std::uint8_t>(driverXml.begin(), driverXml.end())));

    auto create = [&]() {
        std::unique_ptr<IMachine> m = DefaultMachineRegistry().Create(MachineId(def.Machine), VariantId(def.Variant), res.Resources);
        if (m) m->Initialize();
        return m;
    };

    const char* referenceRoot = GetEnvVar("CHIPESTRA_REFERENCE_DIR");
    const bool haveReference = referenceRoot != nullptr && referenceRoot[0] != '\0';
    const std::filesystem::path referenceDir =
        haveReference ? std::filesystem::path(referenceRoot) / "nes" / "gimmick" / "real_game" : std::filesystem::path();
    if (!haveReference) std::printf("  SKIP: CHIPESTRA_REFERENCE_DIR is not set - the write sequences are not compared with the real game\n");

    constexpr std::uint64_t kFrames = 600;
    const std::uint64_t ticks = (kFrames + 1) * 59561ull / 2 * 192000ull / NesMachine::kCpuClockHz + 1;
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
            if (frame >= 1 && frame <= kFrames) writes.push_back({frame - 1, 0x4000u + reg, value});
        });
        machine.SetSunsoft5bWriteObserverForTest([&](std::uint64_t frame, std::uint16_t address, std::uint8_t value) {
            if (frame >= 1 && frame <= kFrames) writes.push_back({frame - 1, address, value});
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
        Check(same, (label + ": all APU and 5B writes of the first 600 updates equal the real game's (update, address, value)").c_str());
        ++compared;
    }
    if (haveReference) {
        std::printf("  %zu requests compared with the recording of the real game\n", compared);
        Check(compared == requests.size(), "gimmick: every request was compared with a recording");
    }
}
