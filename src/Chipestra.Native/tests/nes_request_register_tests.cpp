// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Tests of the request register and the effect routine of the NES Machine. The synthetic program is hand-written;
// what each routine must leave behind is worked out here from the instructions, not read back from the Machine.

#include "nes_request_register_tests.h"

#include "default_machine_catalog.h"
#include "definition_xml_locator.h"
#include "devices/nintendo/nes/nes_cartridge.h"
#include "devices/nintendo/nes/nes_driver_code.h"
#include "devices/nintendo/nes/nes_driver_config.h"
#include "devices/nintendo/nes/nes_machine.h"
#include "machine_registry.h"
#include "resource_resolver.h"
#include "test_support.h"
#include "xml_definition_parser.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
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

MusicCommand Cmd(MusicCommandKind kind, std::uint8_t value) {
    MusicCommand c;
    c.Kind = kind;
    c.Value = value;
    return c;
}

using Bytes = std::vector<std::uint8_t>;

void Append(Bytes& out, std::initializer_list<int> values) {
    for (int v : values) out.push_back(static_cast<std::uint8_t>(v));
}

// Routines of the synthetic program, each storing the register it is called with in RAM.
constexpr std::uint16_t kSongX = 0x8000;   // STX $0320; RTS
constexpr std::uint16_t kEffectX = 0x8010; // STX $0321; RTS
constexpr std::uint16_t kSongA = 0x8020;   // STA $0322; RTS
constexpr std::uint16_t kSongY = 0x8030;   // STY $0323; RTS
constexpr std::uint16_t kFrameRts = 0x8100;  // RTS

NesCartridge BuildCartridge() {
    NesCartridge cart;
    cart.prg.assign(32768, 0xEA);
    auto put = [&](std::uint16_t address, std::initializer_list<std::uint8_t> bytes) {
        std::size_t i = address & 0x7FFF;
        for (std::uint8_t b : bytes) cart.prg[i++] = b;
    };
    put(kSongX, {0x8E, 0x20, 0x03, 0x60});
    put(kEffectX, {0x8E, 0x21, 0x03, 0x60});
    put(kSongA, {0x8D, 0x22, 0x03, 0x60});
    put(kSongY, {0x8C, 0x23, 0x03, 0x60});
    put(kFrameRts, {0x60});
    return cart;
}

std::unique_ptr<NesMachine> MakeMachine(char reg, std::uint16_t song, std::optional<std::uint16_t> sfx) {
    NesDriverConfig cfg;
    cfg.ramClear = std::pair<std::uint16_t, std::uint16_t>{std::uint16_t{0x0000}, std::uint16_t{0x07FF}};
    cfg.frame = {kFrameRts};
    cfg.requestCall = song;
    cfg.requestRegister = reg;
    cfg.sfxCall = sfx;
    auto machine = std::make_unique<NesMachine>(BuildCartridge(), std::move(cfg));
    machine->Initialize();
    return machine;
}

// The Machine registers its channels in the first sink it renders into, so one sink serves a Machine for its life.
void Run(NesMachine& machine, LogicalChannelSink& sink, const MusicCommand& command) {
    machine.SendCommand(command);
    machine.Render(EmuDuration(192000 / 10), &sink);
}

}  // namespace

void NesRequestRegisterSettingsParseAndReject() {
    std::printf("NesRequestRegisterSettingsParseAndReject\n");
    NesDriverConfig cfg;
    std::string error;
    Check(ParseNesDriverConfig("<driver request_call=\"0x8003\"/>", cfg, &error) && cfg.requestRegister == 'A' && !cfg.sfxCall,
          "request_register defaults to A and sfx_call to none");
    Check(ParseNesDriverConfig("<driver request_call=\"0x8003\" request_register=\"X\" sfx_call=\"0x8009\"/>", cfg, &error) &&
              cfg.requestRegister == 'X' && cfg.sfxCall == 0x8009 && cfg.requestCall == 0x8003,
          "request_register and sfx_call are read");
    Check(ParseNesDriverConfig("<driver request_call=\"0x8003\" request_register=\"Y\"/>", cfg, &error) && cfg.requestRegister == 'Y',
          "request_register accepts Y");
    Check(!ParseNesDriverConfig("<driver request_call=\"0x8003\" request_register=\"Z\"/>", cfg, &error),
          "request_register rejects a register that is not A, X or Y");
    Check(!ParseNesDriverConfig("<driver request_call=\"0x8003\" request_register=\"x\"/>", cfg, &error),
          "request_register is case sensitive");
    Check(!ParseNesDriverConfig("<driver request_register=\"X\"/>", cfg, &error), "request_register needs request_call");
    Check(!ParseNesDriverConfig("<driver sfx_call=\"0x8009\"/>", cfg, &error), "sfx_call needs request_call");
    Check(!ParseNesDriverConfig("<driver request_call=\"0x8003\" sfx_call=\"bad\"/>", cfg, &error), "sfx_call must be an address");

    Check(ParseNesDriverConfig("<driver/>", cfg, &error) && !cfg.inhibitFrameIrq && !cfg.mmc3PrgMode0,
          "frame_irq and mmc3_prg_mode are off / mode 1 by default");
    Check(ParseNesDriverConfig("<driver frame_irq=\"off\" mmc3_prg_mode=\"0\"/>", cfg, &error) && cfg.inhibitFrameIrq && cfg.mmc3PrgMode0,
          "frame_irq=\"off\" and mmc3_prg_mode=\"0\" are read");
    Check(ParseNesDriverConfig("<driver mmc3_prg_mode=\"1\"/>", cfg, &error) && !cfg.mmc3PrgMode0, "mmc3_prg_mode=\"1\" is mode 1");
    Check(!ParseNesDriverConfig("<driver frame_irq=\"on\"/>", cfg, &error), "frame_irq accepts only \"off\"");
    Check(!ParseNesDriverConfig("<driver mmc3_prg_mode=\"2\"/>", cfg, &error), "mmc3_prg_mode accepts only 0 or 1");
}

// Without request_register and sfx_call the generated frame code is what it was before they existed.
void NesFrameCodeIsUnchangedWithoutTheNewSettings() {
    std::printf("NesFrameCodeIsUnchangedWithoutTheNewSettings\n");
    const std::vector<std::uint16_t> frame = {0x8100};
    const std::uint16_t request = 0x8000;
    Check(BuildFrameCode(frame, nullptr, &request) ==
              Bytes({0x20, 0x00, 0x81,                          // JSR frame
                     0xAD, 0x01, 0x46, 0xF0, 0x06,              // LDA pending; BEQ over the request
                     0xAD, 0x00, 0x46, 0x20, 0x00, 0x80,        // LDA value; JSR request
                     0x40}),
          "no banks: frame routine, pending request with the number in A, RTI");

    const CodeBanks banks{0x1D, 0x1C, 0x0A, 0x1A};
    const Bytes callBanks = {0xA9, 0x46, 0x8D, 0x00, 0x80, 0xA9, 0x1D, 0x8D, 0x01, 0x80,
                             0xA9, 0x47, 0x8D, 0x00, 0x80, 0xA9, 0x1C, 0x8D, 0x01, 0x80};
    const Bytes idleBanks = {0xA9, 0x46, 0x8D, 0x00, 0x80, 0xA9, 0x0A, 0x8D, 0x01, 0x80,
                             0xA9, 0x47, 0x8D, 0x00, 0x80, 0xA9, 0x1A, 0x8D, 0x01, 0x80};
    Bytes expected = callBanks;
    Append(expected, {0xA2, 0x00, 0x20, 0x00, 0x81, 0xAD, 0x01, 0x46, 0xF0, 0x08, 0xAD, 0x00, 0x46, 0xA2, 0x00, 0x20, 0x00, 0x80});
    expected.insert(expected.end(), idleBanks.begin(), idleBanks.end());
    expected.push_back(0x40);
    Check(BuildFrameCode(frame, &banks, &request) == expected, "MMC3 banks: X cleared before each call, A carries the request");
    Check(BuildFrameCode(frame, &banks, &request, nullptr, 'A', nullptr) == expected, "naming A and no effect routine changes nothing");
    Check(BuildFrameCode(frame) == Bytes({0x20, 0x00, 0x81, 0x40}), "no request: just the frame routine and RTI");

    // frame_irq="off" puts LDA #$40; STA $4017 in front of the start-up code; without it nothing changes.
    const std::vector<std::uint16_t> init = {0x8000};
    Check(BuildStartCode(init, 0x4400) == Bytes({0x20, 0x00, 0x80, 0x4C, 0x03, 0x44}), "start-up code without frame_irq: JSR init; JMP self");
    Check(BuildStartCode(init, 0x4400, nullptr, true) == Bytes({0xA9, 0x40, 0x8D, 0x17, 0x40, 0x20, 0x00, 0x80, 0x4C, 0x08, 0x44}),
          "start-up code with frame_irq=off: LDA #$40; STA $4017 first");

    // mmc3_prg_mode="0" selects R6 and R7 with the PRG mode bit clear (0x06, 0x07 instead of 0x46, 0x47).
    CodeBanks mode0 = banks;
    mode0.mmc3PrgMode0 = true;
    const Bytes mode0Call = {0xA9, 0x06, 0x8D, 0x00, 0x80, 0xA9, 0x1D, 0x8D, 0x01, 0x80,
                             0xA9, 0x07, 0x8D, 0x00, 0x80, 0xA9, 0x1C, 0x8D, 0x01, 0x80};
    Check(BuildFrameCode(frame, &mode0) == Bytes([&] {
              Bytes out = mode0Call;
              Append(out, {0xA2, 0x00, 0x20, 0x00, 0x81});
              out.insert(out.end(), {0xA9, 0x06, 0x8D, 0x00, 0x80, 0xA9, 0x0A, 0x8D, 0x01, 0x80,
                                     0xA9, 0x07, 0x8D, 0x00, 0x80, 0xA9, 0x1A, 0x8D, 0x01, 0x80, 0x40});
              return out;
          }()),
          "PRG mode 0: the bank registers are selected with 0x06 and 0x07");

    // The new settings: X is loaded, not cleared; the effect routine has its own pending flag and value.
    const std::uint16_t effect = 0x8009;
    Bytes withX = callBanks;
    Append(withX, {0xA2, 0x00, 0x20, 0x00, 0x81,
                   0xAD, 0x01, 0x46, 0xF0, 0x06, 0xAE, 0x00, 0x46, 0x20, 0x00, 0x80,
                   0xAD, 0x03, 0x46, 0xF0, 0x06, 0xAE, 0x02, 0x46, 0x20, 0x09, 0x80});
    withX.insert(withX.end(), idleBanks.begin(), idleBanks.end());
    withX.push_back(0x40);
    Check(BuildFrameCode(frame, &banks, &request, nullptr, 'X', &effect) == withX,
          "register X with an effect routine: LDX value (no LDX #0), then the same for the effect");
}

void NesMachinePassesTheRequestInTheChosenRegister() {
    std::printf("NesMachinePassesTheRequestInTheChosenRegister\n");
    {
        LogicalChannelSink sink;
        auto machine = MakeMachine('X', kSongX, kEffectX);
        Check(machine->AcceptsCommand(Cmd(MusicCommandKind::Effect, 1)), "an effect is accepted with sfx_call");
        Run(*machine, sink, Cmd(MusicCommandKind::Raw, 5));
        Check(machine->RamForTest(0x0320) == 5, "X: a song reaches the song routine in X");
        Check(machine->RamForTest(0x0321) == 0, "X: the effect routine is not called for a song");
        Run(*machine, sink, Cmd(MusicCommandKind::Effect, 9));
        Check(machine->RamForTest(0x0321) == 9, "X: an effect reaches the effect routine in X");
        Check(machine->RamForTest(0x0320) == 0, "X: the song routine is not called for an effect (the Machine started again)");
    }
    {
        LogicalChannelSink sink;
        auto machine = MakeMachine('A', kSongA, std::nullopt);
        Run(*machine, sink, Cmd(MusicCommandKind::Effect, 6));
        Check(machine->RamForTest(0x0322) == 6, "A, no sfx_call: an effect goes to the request routine in A");
    }
    {
        LogicalChannelSink sink;
        auto machine = MakeMachine('Y', kSongY, std::nullopt);
        Run(*machine, sink, Cmd(MusicCommandKind::Raw, 3));
        Check(machine->RamForTest(0x0323) == 3, "Y: a song reaches the routine in Y");
    }
    {
        // A request is made once: the routine is not called again in later frames.
        LogicalChannelSink sink;
        auto machine = MakeMachine('X', kSongX, kEffectX);
        Run(*machine, sink, Cmd(MusicCommandKind::Raw, 7));
        machine->WriteRamForTest(0x0320, 0);
        machine->Render(EmuDuration(192000 / 10), &sink);
        Check(machine->RamForTest(0x0320) == 0, "a request is made once");
    }
}

namespace {

struct Write {
    std::uint64_t frame;
    unsigned reg, value;
    bool operator==(const Write& o) const { return frame == o.frame && reg == o.reg && value == o.value; }
};

}  // namespace

// Real ROM (CHIPESTRA_ROM_ROOT): silversurfer.xml is read, resolved and played as the application does. With
// CHIPESTRA_REFERENCE_DIR also set (nes/silversurfer/real_game holds one file per request of a MAME run of the real game:
// song_NN.txt and sfx_NN.txt, "<frame> <address> <value>", frames counted from the request), every song and effect's APU
// writes of the first 600 frames must equal the recording. The recording is of the game's driver reset and one update
// followed by the request, which is how the Machine starts.
void SilverSurferViaDefinitionXmlRealRomMatchesTheRealGame() {
    std::printf("SilverSurferViaDefinitionXmlRealRomMatchesTheRealGame\n");
    const char* romRootEnv = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRootEnv == nullptr || romRootEnv[0] == '\0') {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is not set - real-ROM bring-up not exercised\n");
        return;
    }
    const auto path = FindDefinitionXml("silversurfer.xml");
    Check(path.has_value(), "silversurfer: silversurfer.xml is present (run from the repo root)");
    if (!path) return;
    const auto text = ReadFileText(*path);
    Check(text.has_value(), "silversurfer: file is readable");
    if (!text) return;
    const XmlParseResult parsed = ParseGameDefinition(*text);
    Check(parsed.Status == XmlParseStatus::Ok, "silversurfer: silversurfer.xml parses cleanly");
    if (parsed.Status != XmlParseStatus::Ok) return;
    const GameDefinition& def = parsed.Definition;
    Check(def.Machine == "NINTENDO_NES" && def.Variant == "NES_BASE", "silversurfer: machine NINTENDO_NES / variant NES_BASE");

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
    Check(res.Ok, "silversurfer: the cartridge resolves under CHIPESTRA_ROM_ROOT (archive \"Silver Surfer\")");
    if (!res.Ok) return;
    const std::string driverXml = ExtractDriverElementXml(*text);
    Check(!driverXml.empty(), "silversurfer: the <driver> element is present");
    res.Resources.Add(ResolvedResource("driver_xml", std::vector<std::uint8_t>(driverXml.begin(), driverXml.end())));

    auto create = [&]() {
        std::unique_ptr<IMachine> m = DefaultMachineRegistry().Create(MachineId(def.Machine), VariantId(def.Variant), res.Resources);
        if (m) m->Initialize();
        return m;
    };

    // Every track is one of the driver's requests: songs 0-8, effects 0-13.
    std::size_t songs = 0, effects = 0, controls = 0;
    for (const TrackDefinition& track : def.Tracks) {
        if (track.IsControlCommand) {
            ++controls;
            continue;
        }
        const unsigned number = static_cast<unsigned>(std::strtoul(track.Command.c_str(), nullptr, 0));
        if (track.IsEffect) {
            ++effects;
            Check(number <= 13, (track.Id + ": effect number is 0-13").c_str());
        } else {
            ++songs;
            Check(number <= 8, (track.Id + ": song number is 0-8").c_str());
        }
    }
    Check(songs == 9 && effects == 14 && controls == 1, "silversurfer: 9 songs, 14 effects and a Music Stop");

    const char* referenceRoot = GetEnvVar("CHIPESTRA_REFERENCE_DIR");
    const bool haveReference = referenceRoot != nullptr && referenceRoot[0] != '\0';
    const std::filesystem::path referenceDir =
        haveReference ? std::filesystem::path(referenceRoot) / "nes" / "silversurfer" / "real_game" : std::filesystem::path();
    if (!haveReference) std::printf("  SKIP: CHIPESTRA_REFERENCE_DIR is not set - the APU write sequences are not compared with the real game\n");

    constexpr std::uint64_t kFrames = 600;
    const std::uint64_t ticks = (kFrames + 2) * 59561ull / 2 * 192000ull / NesMachine::kCpuClockHz + 1;
    std::size_t compared = 0;
    for (const TrackDefinition& track : def.Tracks) {
        if (track.IsControlCommand) continue;
        const unsigned number = static_cast<unsigned>(std::strtoul(track.Command.c_str(), nullptr, 0));
        char name[32];
        std::snprintf(name, sizeof(name), "%s %02u", track.IsEffect ? "sfx" : "song", number);
        const std::string label = name;
        std::unique_ptr<IMachine> base = create();
        Check(base != nullptr, (label + ": the Machine is created from the XML's own settings").c_str());
        if (!base) return;
        NesMachine& machine = static_cast<NesMachine&>(*base);
        std::vector<Write> writes;
        machine.SetApuWriteObserverForTest([&](std::uint64_t frame, std::uint8_t reg, std::uint8_t value) {
            if (reg < 0x16) writes.push_back({frame, 0x4000u + reg, value});
        });
        MusicCommand command;
        command.Kind = track.IsEffect ? MusicCommandKind::Effect : MusicCommandKind::Raw;
        command.Value = static_cast<std::uint8_t>(number);
        command.Auxiliary = track.Auxiliary;
        Check(machine.AcceptsCommand(command), (label + ": accepted by the Machine").c_str());
        machine.SendCommand(command);
        LogicalChannelSink sink;
        machine.Render(EmuDuration(ticks), &sink);
        const NesMachine::BusStats stats = machine.BusStatsForTest();
        Check(stats.ppuReads + stats.ppuWrites + stats.inputReads == 0,
              (label + ": the sound driver does not touch the picture chip or the controllers").c_str());
        Check(std::none_of(writes.begin(), writes.end(), [](const Write& w) { return w.reg >= 0x4010 && w.reg <= 0x4013; }),
              (label + ": the driver does not use the DMC").c_str());

        if (!haveReference) continue;
        char fileName[32];
        std::snprintf(fileName, sizeof(fileName), "%s_%02u.txt", track.IsEffect ? "sfx" : "song", number);
        const std::filesystem::path log = referenceDir / fileName;
        if (!std::filesystem::exists(log)) {
            Check(false, (label + ": the reference recording exists").c_str());
            continue;
        }
        std::vector<Write> expected;
        std::ifstream file(log);
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            unsigned frame = 0, address = 0, value = 0;
            std::istringstream fields(line);
            fields >> std::dec >> frame >> std::hex >> address >> value;
            if (!fields.fail() && frame < kFrames && address >= 0x4000 && address < 0x4016) expected.push_back({frame, address, value});
        }
        std::vector<Write> ours;
        for (const Write& w : writes)
            if (w.frame < kFrames) ours.push_back(w);
        std::size_t i = 0;
        while (i < expected.size() && i < ours.size() && expected[i] == ours[i]) ++i;
        const bool same = i == expected.size() && i == ours.size();
        if (!same) {
            std::printf("    %s: first difference at write %zu of %zu (recording) / %zu (Chipestra)\n", label.c_str(), i, expected.size(),
                        ours.size());
            if (i < expected.size())
                std::printf("      recording frame %llu %04X=%02X\n", static_cast<unsigned long long>(expected[i].frame), expected[i].reg,
                            expected[i].value);
            if (i < ours.size())
                std::printf("      Chipestra frame %llu %04X=%02X\n", static_cast<unsigned long long>(ours[i].frame), ours[i].reg, ours[i].value);
        }
        Check(same, (label + ": all APU writes of the first 600 frames equal the real game's (frame, address, value)").c_str());
        ++compared;
    }
    if (haveReference) {
        std::printf("  %zu requests compared with the recording of the real game\n", compared);
        Check(compared == songs + effects, "silversurfer: every request was compared with a recording");
    }

    // The first request after a Music Stop (a reset) is the same as the first request after the start-up, and a
    // request after other requests starts from the freshly initialized driver.
    auto render = [&](const std::vector<MusicCommand>& commands) {
        std::unique_ptr<IMachine> m = create();
        LogicalChannelSink s;
        std::vector<std::vector<float>> out;
        for (std::size_t n = 0; n < commands.size(); ++n) {
            m->SendCommand(commands[n]);
            const bool last = n + 1 == commands.size();
            // A Music Stop only resets the Machine; the next command follows at once.
            if (NesDriverConfig::IsControl(commands[n].Auxiliary)) continue;
            m->Render(EmuDuration(last ? 192000 * 4 : 96000), &s);
            if (!last) s.ClearAll();
        }
        for (const LogicalChannel& channel : m->LogicalChannels())
            out.emplace_back(s.ChannelBuffer(channel.Id).Data(), s.ChannelBuffer(channel.Id).Data() + s.ChannelBuffer(channel.Id).Size());
        return out;
    };
    MusicCommand stop;
    stop.Kind = MusicCommandKind::Raw;
    stop.Auxiliary = NesDriverConfig::kControlFlag;
    const auto fresh = render({Cmd(MusicCommandKind::Raw, 3)});
    Check(render({Cmd(MusicCommandKind::Raw, 1), Cmd(MusicCommandKind::Effect, 2), Cmd(MusicCommandKind::Raw, 3)}) == fresh,
          "song 3 after song 1 and effect 2 equals song 3 on a fresh machine, sample for sample");
    Check(render({Cmd(MusicCommandKind::Raw, 4), stop, Cmd(MusicCommandKind::Raw, 3)}) == fresh,
          "song 3 after a Music Stop equals song 3 on a fresh machine, sample for sample");
}
