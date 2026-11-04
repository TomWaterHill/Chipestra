// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// MegaDriveMachine native tests.

#include "megadrive_machine_tests.h"

#include "default_machine_catalog.h"
#include "definition_xml_locator.h"
#include "devices/nintendo/nes/nes_driver_config.h"
#include "devices/sega/megadrive/megadrive_machine.h"
#include "md_reference_log.h"
#include "machine_registry.h"
#include "mixer.h"
#include "resource_hash.h"
#include "resource_resolver.h"
#include "test_support.h"
#include "xml_definition_parser.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
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

// A small Z80 program image placed at the start of a synthetic cartridge.
struct Program {
    std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(0x100, 0);
    void At(std::size_t address, std::initializer_list<std::uint8_t> code) {
        std::size_t i = address;
        for (std::uint8_t b : code) bytes[i++] = b;
    }
};

MegaDriveDriverConfig SyntheticConfig(std::uint32_t size) {
    MegaDriveDriverConfig config;
    config.music = {0, size};
    config.requestAddress = 0x1804;
    config.releaseCycle = 0;
    config.requestCycle = 0;
    return config;
}

std::unique_ptr<MegaDriveMachine> MakeMachine(const Program& program, std::size_t cartridgeSize = 0x10000,
                                              std::vector<std::pair<std::size_t, std::uint8_t>> extra = {}) {
    std::vector<std::uint8_t> cartridge(cartridgeSize, 0);
    std::copy(program.bytes.begin(), program.bytes.end(), cartridge.begin());
    for (const auto& [address, value] : extra) cartridge[address] = value;
    auto machine = std::make_unique<MegaDriveMachine>(std::move(cartridge), SyntheticConfig(0x100));
    machine->Initialize();
    return machine;
}

}  // namespace

void MegaDriveCartridgeDecodingAndDriverConfig() {
    std::printf("MegaDriveCartridgeDecodingAndDriverConfig\n");
    // A Super Magic Drive file: header (bytes 8, 9 = AA BB) and 16 KB blocks, odd bytes first, even bytes second.
    std::vector<std::uint8_t> smd(512 + 16384, 0);
    smd[8] = 0xAA;
    smd[9] = 0xBB;
    for (std::size_t i = 0; i < 8192; ++i) {
        smd[512 + i] = static_cast<std::uint8_t>(i * 3 + 1);         // goes to the odd byte 2i+1
        smd[512 + 8192 + i] = static_cast<std::uint8_t>(i * 5 + 2);  // goes to the even byte 2i
    }
    const std::vector<std::uint8_t> decoded = DecodeMegaDriveCartridge(smd.data(), smd.size());
    bool ok = decoded.size() == 16384;
    for (std::size_t i = 0; ok && i < 8192; ++i)
        ok = decoded[2 * i + 1] == static_cast<std::uint8_t>(i * 3 + 1) && decoded[2 * i] == static_cast<std::uint8_t>(i * 5 + 2);
    Check(ok, "a Super Magic Drive file is deinterleaved into 68000 order");
    std::vector<std::uint8_t> plain(16384, 7);
    Check(DecodeMegaDriveCartridge(plain.data(), plain.size()) == plain, "a plain image is used as it is");

    MegaDriveDriverConfig config;
    std::string error;
    const std::string good =
        "<driver music=\"0x16A6C:5366\" request=\"0x1804\" release_cycle=\"100\" warmup_frames=\"3\">"
        "<set id=\"v1\" image=\"0x100:64\"/><set id=\"v2\" image=\"512:32\"/></driver>";
    Check(ParseMegaDriveDriverConfig(good, config, &error), "a complete <driver> parses");
    Check(config.music.romOffset == 0x16A6C && config.music.size == 5366 && config.requestAddress == 0x1804 &&
              config.releaseCycle == 100 && config.requestCycle == 3 * MegaDriveMachine::kCyclesPerFrame && config.sets.size() == 2 &&
              config.setIds[1] == "v2" && config.sets[1].romOffset == 512 && config.sets[1].size == 32,
          "the numbers are read (hexadecimal and decimal)");
    Check(!ParseMegaDriveDriverConfig("<driver request=\"0x1804\" release_cycle=\"0\" warmup_frames=\"0\"/>", config, &error),
          "a driver without music is refused");
    Check(!ParseMegaDriveDriverConfig("<driver music=\"0:16\" release_cycle=\"10\"/>", config, &error),
          "a driver without warmup_frames is refused");
    Check(!ParseMegaDriveDriverConfig("<driver music=\"0:16\" request=\"0x3000\" release_cycle=\"0\" warmup_frames=\"0\"/>", config, &error),
          "a request address outside the Z80 RAM is refused");
    Check(!ParseMegaDriveDriverConfig("<driver music=\"0:16\" release_cycle=\"0\" warmup_frames=\"0\"><set image=\"x\"/></driver>", config, &error),
          "a set with a bad image is refused");
}

void MegaDriveInterruptIsOneScanlineLongAndLostWhenDisabled() {
    std::printf("MegaDriveInterruptIsOneScanlineLongAndLostWhenDisabled\n");
    {
        // IM 1 with interrupts enabled: the handler counts and re-enables (EI; RETI) at once, so while the line stays
        // high - one scanline, 228 cycles - the interrupt is taken again.
        Program program;
        program.At(0x0000, {0x31, 0xFF, 0x1F, 0xED, 0x56, 0xFB, 0x18, 0xFE});
        program.At(0x0038, {0x3A, 0x00, 0x10, 0x3C, 0x32, 0x00, 0x10, 0xFB, 0xED, 0x4D});
        auto machine = MakeMachine(program);
        machine->RunToForTest(5 * MegaDriveMachine::kCyclesPerFrame);
        const unsigned count = machine->Z80RamForTest(0x1000);
        Check(count >= 15 && count <= 20, "an enabled Z80 takes the interrupt 3 to 4 times a frame (the line is high for 228 cycles)");
        const auto snapshot = machine->CpuSnapshotForTest();
        Check(snapshot.iff1, "the program left interrupts enabled");
    }
    {
        // Interrupts disabled: the pulse is lost, nothing is kept for later.
        Program program;
        program.At(0x0000, {0xF3, 0x31, 0xFF, 0x1F, 0xED, 0x56, 0x18, 0xFE});
        program.At(0x0038, {0x3A, 0x00, 0x10, 0x3C, 0x32, 0x00, 0x10, 0xFB, 0xED, 0x4D});
        auto machine = MakeMachine(program);
        machine->RunToForTest(5 * MegaDriveMachine::kCyclesPerFrame);
        Check(machine->Z80RamForTest(0x1000) == 0, "a Z80 with interrupts disabled never sees the interrupt");
    }
    {
        // Enabling the interrupts after the pulse is over (the program waits, then EI) takes no interrupt in that frame.
        // The first pulse falls at cycle 51072 of the frame; a wait of 55000 cycles ends well after its 228 cycles.
        Program program;
        // DI; LD SP; IM 1; LD BC,0 ; loop: DEC BC (7 cycles total per pass via the JR) ... then EI; JR $
        // 55000 cycles is about 3437 passes of "DEC BC; LD A,B; OR C; JR NZ" (16 cycles) - BC = 3437 (0x0D6D).
        program.At(0x0000, {0xF3, 0x31, 0xFF, 0x1F, 0xED, 0x56, 0x01, 0x6D, 0x0D, 0x0B, 0x78, 0xB1, 0x20, 0xFB, 0xFB, 0x18, 0xFE});
        program.At(0x0038, {0x3A, 0x00, 0x10, 0x3C, 0x32, 0x00, 0x10, 0xFB, 0xED, 0x4D});
        auto machine = MakeMachine(program);
        machine->RunToForTest(MegaDriveMachine::kCyclesPerFrame);
        Check(machine->Z80RamForTest(0x1000) == 0, "interrupts enabled after the pulse took no interrupt in that frame");
        machine->RunToForTest(2 * MegaDriveMachine::kCyclesPerFrame);
        Check(machine->Z80RamForTest(0x1000) >= 3, "the next frame's pulse is taken");
    }
}

void MegaDriveRegionSetsTheClocksAndTheFrame() {
    std::printf("MegaDriveRegionSetsTheClocksAndTheFrame%s", "\n");
    const MegaDriveTiming ntsc = MegaDriveTiming::For(MegaDriveRegion::Japan);
    const MegaDriveTiming usa = MegaDriveTiming::For(MegaDriveRegion::Usa);
    const MegaDriveTiming pal = MegaDriveTiming::For(MegaDriveRegion::Europe);
    Check(ntsc.masterClockHz == 53'693'175 && ntsc.z80ClockHz == 3'579'545 && ntsc.ymClockHz == 7'670'453 && ntsc.cyclesPerFrame == 59'736 &&
              ntsc.ymStreamRateHz == 53'267 && ntsc.psgStreamRateHz == 1'789'772,
          "an NTSC console: master 53693175 Hz, Z80 and PSG 3579545 Hz, YM2612 7670453 Hz, 262 lines of 228 Z80 cycles");
    Check(usa.masterClockHz == ntsc.masterClockHz && usa.cyclesPerFrame == ntsc.cyclesPerFrame, "North America (Genesis) has the NTSC clocks");
    Check(pal.masterClockHz == 53'203'424 && pal.z80ClockHz == 3'546'894 && pal.ymClockHz == 7'600'489 && pal.cyclesPerFrame == 71'364 &&
              pal.ymStreamRateHz == 52'781 && pal.psgStreamRateHz == 1'773'447,
          "a PAL console: master 53203424 Hz, Z80 and PSG 3546894 Hz, YM2612 7600489 Hz, 313 lines of 228 Z80 cycles");
    Check(MegaDriveVersionRegister(MegaDriveRegion::Japan) == 0x00 && MegaDriveVersionRegister(MegaDriveRegion::Usa) == 0x80 &&
              MegaDriveVersionRegister(MegaDriveRegion::Europe) == 0xC0,
          "the version register: bit 7 outside Japan, bit 6 as well in a PAL console");

    MegaDriveDriverConfig config;
    std::string error;
    const std::string base = "<driver music=" + std::string(1, char(34)) + "0:16" + std::string(1, char(34)) + " release_cycle=" + std::string(1, char(34)) + "100" + std::string(1, char(34)) +
                             " warmup_frames=" + std::string(1, char(34)) + "3" + std::string(1, char(34));
    auto quoted = [](const char* text) { return std::string(1, char(34)) + text + std::string(1, char(34)); };
    Check(ParseMegaDriveDriverConfig(base + "/>", config, &error) && config.region == MegaDriveRegion::Japan, "without region the console is the Japanese one");
    Check(ParseMegaDriveDriverConfig(base + " region=" + quoted("usa") + "/>", config, &error) && config.region == MegaDriveRegion::Usa, "region=usa");
    Check(ParseMegaDriveDriverConfig(base + " region=" + quoted("europe") + "/>", config, &error) && config.region == MegaDriveRegion::Europe &&
              config.requestCycle == 3 * 71'364,
          "region=europe, and warmup_frames counts frames of 313 lines");
    Check(!ParseMegaDriveDriverConfig(base + " region=" + quoted("pal") + "/>", config, &error) && error.find("region") != std::string::npos,
          "an unknown region is refused, naming the attribute");

    // The vertical-blank interrupt: the Z80 takes it in a run of a few passes at the start of every frame; the frames are
    // 262 lines in NTSC and 313 in PAL.
    for (const MegaDriveRegion region : {MegaDriveRegion::Japan, MegaDriveRegion::Europe}) {
        Program program;
        program.At(0x0000, {0x31, 0xFF, 0x1F, 0xED, 0x56, 0xFB, 0x18, 0xFE});
        program.At(0x0038, {0x3A, 0x00, 0x10, 0x3C, 0x32, 0x00, 0x10, 0xFB, 0xED, 0x4D});
        std::vector<std::uint8_t> cartridge(0x10000, 0);
        std::copy(program.bytes.begin(), program.bytes.end(), cartridge.begin());
        MegaDriveDriverConfig synthetic = SyntheticConfig(0x100);
        synthetic.region = region;
        MegaDriveMachine machine(std::move(cartridge), synthetic);
        machine.Initialize();
        const std::uint64_t frame = MegaDriveTiming::For(region).cyclesPerFrame;
        std::vector<std::uint64_t> starts;
        std::uint64_t seen = 0, lastChange = 0;
        for (std::uint64_t cycle = 20; cycle < 4 * frame; cycle += 20) {
            machine.RunToForTest(cycle);
            const std::uint64_t count = machine.CpuSnapshotForTest().irqServiceCount;
            if (count != seen) {
                if (starts.empty() || cycle - lastChange > 1000) starts.push_back(cycle);
                lastChange = cycle;
                seen = count;
            }
        }
        char label[200];
        std::snprintf(label, sizeof(label), "%s: interrupts come once a frame, %zu groups in 4 frames, %llu cycles apart (a frame is %llu)",
                      region == MegaDriveRegion::Europe ? "PAL" : "NTSC", starts.size(), starts.size() >= 2 ? static_cast<unsigned long long>(starts[1] - starts[0]) : 0ull,
                      static_cast<unsigned long long>(frame));
        Check(starts.size() == 4 && starts[1] - starts[0] >= frame - 20 && starts[1] - starts[0] <= frame + 20 &&
                  starts[2] - starts[1] >= frame - 20 && starts[2] - starts[1] <= frame + 20,
              label);
        Check(machine.TimingForTest().cyclesPerFrame == frame, "the Machine uses the timing of its region");
    }
}

void MegaDriveYm2612StatusShowsBusyAfterADataWrite() {
    std::printf("MegaDriveYm2612StatusShowsBusyAfterADataWrite%c", 10);
    // Write register 0x2A, then read the status at once (bit 7 = busy), then again long after.
    Program program;
    program.At(0x0000, {0x3E, 0x2A, 0x32, 0x00, 0x40, 0x3E, 0x80, 0x32, 0x01, 0x40, 0x3A, 0x00, 0x40, 0x32, 0x00, 0x10,
                        0x06, 0xFF, 0x10, 0xFE, 0x3A, 0x00, 0x40, 0x32, 0x01, 0x10, 0x18, 0xFE});
    auto machine = MakeMachine(program);
    machine->RunToWithSoundForTest(20000);
    Check((machine->Z80RamForTest(0x1000) & 0x80) != 0, "the status read right after a data write shows the busy flag");
    Check((machine->Z80RamForTest(0x1001) & 0x80) == 0, "the busy flag is clear a few thousand cycles later");
}

void MegaDriveBankRegisterAndChipPorts() {
    std::printf("MegaDriveBankRegisterAndChipPorts\n");
    Program program;
    std::size_t a = 0;
    auto emit = [&](std::initializer_list<std::uint8_t> code) {
        for (std::uint8_t b : code) program.bytes[a++] = b;
    };
    // Bank = 1 (68000 address 0x8000): the bits go in lowest first, nine writes.
    emit({0x21, 0x00, 0x60});  // LD HL,6000h
    emit({0x3E, 0x01, 0x77});  // LD A,1; LD (HL),A
    emit({0xAF});              // XOR A
    for (int i = 0; i < 8; ++i) emit({0x77});
    emit({0x3A, 0x00, 0x80, 0x32, 0x00, 0x10});  // LD A,(8000h); LD (1000h),A
    emit({0x3A, 0x01, 0x80, 0x32, 0x01, 0x10});  // LD A,(8001h); LD (1001h),A
    // The YM2612: register 0x2A, value 0x80.
    emit({0x3E, 0x2A, 0x32, 0x00, 0x40, 0x3E, 0x80, 0x32, 0x01, 0x40});
    // The PSG through the VDP window of the Z80 (0x7F11), then through the cartridge window.
    emit({0x3E, 0x9F, 0x32, 0x11, 0x7F});
    // Bank = 0x180: the zeros of bits 0-6, then 1, 1.
    emit({0xAF});
    for (int i = 0; i < 7; ++i) emit({0x77});
    emit({0x3E, 0x01, 0x77, 0x77});
    emit({0x3E, 0xBF, 0x32, 0x11, 0x80});  // LD A,0BFh; LD (8011h),A
    emit({0x18, 0xFE});
    auto machine = MakeMachine(program, 0x10000, {{0x8000, 0x5A}, {0x8001, 0xA5}});
    std::vector<std::tuple<char, std::uint32_t, std::uint8_t>> writes;
    machine->SetChipWriteObserverForTest([&](std::uint64_t, char kind, std::uint32_t port, std::uint8_t value) {
        writes.emplace_back(kind, port, value);
    });
    machine->RunToForTest(2000);
    Check(machine->Z80RamForTest(0x1000) == 0x5A && machine->Z80RamForTest(0x1001) == 0xA5,
          "after nine writes to $6000 the window at $8000 shows the cartridge from 0x8000");
    Check(machine->BankForTest() == 0xC00000, "bank 0x180 is the 68000 address 0xC00000");
    using Write = std::tuple<char, std::uint32_t, std::uint8_t>;
    const std::vector<Write> expected = {Write('Y', 0u, std::uint8_t{0x2A}), Write('Y', 1u, std::uint8_t{0x80}),
                                         Write('P', 0u, std::uint8_t{0x9F}), Write('P', 0u, std::uint8_t{0xBF})};
    Check(writes == expected, "YM2612 ports, the PSG at $7F11 and the PSG through the window ($C00011) all reach their chips");
}

void MegaDriveSwitchingStartsAfresh() {
    std::printf("MegaDriveSwitchingStartsAfresh\n");
    Program program;
    program.At(0x0000, {0x3A, 0x00, 0x10, 0x3C, 0x32, 0x00, 0x10, 0x18, 0xF5});  // counts up forever in RAM[0x1000]
    std::vector<std::uint8_t> cartridge(0x10000, 0);
    std::copy(program.bytes.begin(), program.bytes.end(), cartridge.begin());
    MegaDriveDriverConfig config = SyntheticConfig(0x100);
    config.sets.push_back({0, 0x100});
    config.setIds.push_back("other");
    MegaDriveMachine machine(cartridge, config);
    machine.Initialize();
    Check(machine.AcceptsCommand({MusicCommandKind::Raw, 0x81, 0}) && machine.AcceptsCommand({MusicCommandKind::Effect, 0xA0, 0}) &&
              machine.AcceptsCommand({MusicCommandKind::Raw, 0, 1}) && !machine.AcceptsCommand({MusicCommandKind::Raw, 0, 2}) &&
              !machine.AcceptsCommand({MusicCommandKind::Wsg, 1, 0}),
          "raw and effect commands are accepted, the set must exist, WSG is not accepted");
    machine.SendCommand({MusicCommandKind::Raw, 0x81, 0});
    Check(machine.Z80RamForTest(0x1804) == 0x81, "the first command writes its request byte");
    machine.RunToForTest(5000);
    Check(machine.Z80RamForTest(0x1000) != 0, "the program runs");
    machine.SendCommand({MusicCommandKind::Raw, 0x82, 0});
    Check(machine.Z80RamForTest(0x1804) == 0x82 && machine.Z80RamForTest(0x1000) == 0, "a second command starts again and writes its own request");
    machine.SendCommand({MusicCommandKind::Raw, 0, 1});
    Check(machine.LoadedSetForTest() == 1 && machine.Z80RamForTest(0x1804) == 0, "a track with a set loads that program and makes no request");
    machine.SendCommand({MusicCommandKind::Stop, 0, 0});
    Check(machine.LoadedSetForTest() == 0, "Stop goes back to the sound driver");
    Check(machine.LogicalChannels().size() == 11, "eleven logical channels: seven of the YM2612 and four of the PSG");
}

namespace {

struct RealGame {
    GameDefinition definition;
    ResourceResolutionResult resolution;
    std::string driverXml;
    bool ok = false;
};

RealGame LoadRealGame(const char* xmlFile = "spaceharrier2.xml", std::size_t regionIndex = 0) {
    RealGame game;
    const char* romRoot = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRoot == nullptr || romRoot[0] == '\0') return game;
    const auto path = FindDefinitionXml(xmlFile);
    Check(path.has_value(), (std::string(xmlFile) + " is present (run from the repo root)").c_str());
    if (!path) return game;
    const auto text = ReadFileText(*path);
    if (!text) return game;
    const XmlParseResult parsed = ParseGameDefinition(*text);
    Check(parsed.Status == XmlParseStatus::Ok, (std::string(xmlFile) + " parses cleanly").c_str());
    if (parsed.Status != XmlParseStatus::Ok) {
        std::printf("    %s\n", parsed.Detail.c_str());
        return game;
    }
    game.definition = parsed.Definition;
    const std::vector<RequiredResource> required = game.definition.BuildRequiredResources();
    const ResourceResolver resolver{std::filesystem::path(romRoot)};
    game.resolution = resolver.Resolve(required);
    Check(game.resolution.Ok, "the cartridge resolves under CHIPESTRA_ROM_ROOT");
    if (!game.resolution.Ok) {
        for (const auto& f : game.resolution.Failures) std::printf("    resolve fail '%s' (%s)\n", f.LogicalName.c_str(), f.Detail.c_str());
        return game;
    }
    std::string selectError;
    game.driverXml = SelectMegaDriveDriverRegion(ExtractDriverElementXml(*text), regionIndex, &selectError);
    Check(!game.driverXml.empty(), ("the <driver> of region number " + std::to_string(regionIndex) + " is made " + selectError).c_str());
    if (game.driverXml.empty()) return game;
    game.resolution.Resources.Add(ResolvedResource("driver_xml", std::vector<std::uint8_t>(game.driverXml.begin(), game.driverXml.end())));
    game.ok = true;
    return game;
}

std::unique_ptr<MegaDriveMachine> MakeRealMachine(const RealGame& game, MegaDriveMachine::ChipWriteObserver observer = nullptr,
                                                  std::optional<std::uint64_t> releaseCycle = std::nullopt) {
    MegaDriveMachineInputs inputs = AssembleMegaDriveMachineInputs(game.resolution.Resources);
    if (releaseCycle.has_value()) {
        // The game's own 68000 decompresses the song before it releases the Z80, so the release falls at a different
        // time for every song: use the one the recording has.
        const std::uint64_t frame = MegaDriveTiming::For(inputs.driver.region).cyclesPerFrame;
        const std::uint64_t frames = inputs.driver.requestCycle / frame - inputs.driver.releaseCycle / frame;
        inputs.driver.releaseCycle = *releaseCycle;
        inputs.driver.requestCycle = std::max(*releaseCycle, (*releaseCycle / frame + frames) * frame);
    }
    auto machine = std::make_unique<MegaDriveMachine>(std::move(inputs.cartridge), std::move(inputs.driver));
    if (observer) machine->SetChipWriteObserverForTest(std::move(observer));
    machine->Initialize();
    return machine;
}

}  // namespace

void MegaDriveViaDefinitionXmlMatchesTheOriginalsChipWrites() {
    std::printf("MegaDriveViaDefinitionXmlMatchesTheOriginalsChipWrites\n");
    const char* romRoot = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRoot == nullptr || romRoot[0] == '\0') {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is not set - real-cartridge bring-up not exercised\n");
        return;
    }
    const RealGame game = LoadRealGame();
    if (!game.ok) return;
    Check(game.definition.Machine == "SEGA_MEGADRIVE" && game.definition.Variant == "MEGADRIVE_BASE", "machine SEGA_MEGADRIVE / variant MEGADRIVE_BASE");
    const std::filesystem::path dir = MdReferenceDir("real_game");
    if (dir.empty()) {
        std::printf("  SKIP: CHIPESTRA_REFERENCE_DIR is not set - the chip writes are not compared with the original's\n");
        return;
    }
    std::size_t compared = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        const std::string file = entry.path().filename().string();
        if (file.size() < 12 || file.substr(file.size() - 11) != "_writes.txt") continue;
        const std::string name = file.substr(0, file.size() - 11);  // "req_81"
        if (name.rfind("req_", 0) != 0) continue;
        const unsigned request = static_cast<unsigned>(std::strtoul(name.c_str() + 4, nullptr, 16));
        // The original: every YM2612 and PSG write of the Z80 from power-on, then 300 frames from the request.
        std::vector<std::tuple<std::uint64_t, char, std::uint32_t, std::uint8_t>> original;
        std::uint64_t lastCycle = 0;
        {
            std::ifstream in(entry.path());
            std::string line;
            while (std::getline(in, line)) {
                if (line.empty() || line[0] == '#') continue;
                std::istringstream fields(line);
                unsigned long long cycle = 0;
                std::string kind;
                unsigned a = 0, b = 0;
                fields >> cycle >> kind >> a;
                if (kind == "YM") {
                    fields >> b;
                    original.emplace_back(cycle, 'Y', a, static_cast<std::uint8_t>(b));
                } else if (kind == "PSG") {
                    original.emplace_back(cycle, 'P', 0u, static_cast<std::uint8_t>(a));
                } else {
                    continue;
                }
                lastCycle = cycle;
            }
        }
        std::vector<std::tuple<std::uint64_t, char, std::uint32_t, std::uint8_t>> mine;
        auto machine = MakeRealMachine(game, [&](std::uint64_t cycle, char kind, std::uint32_t port, std::uint8_t value) {
            mine.emplace_back(cycle, kind, port, value);
        });
        machine->SendCommand({request >= 0xA0 && request < 0xC0 ? MusicCommandKind::Effect : MusicCommandKind::Raw,
                              static_cast<std::uint8_t>(request), 0});
        machine->RunToForTest(lastCycle - machine->ReleaseCycleForTest() + 100);
        // Compare up to the last write of the original (the machine may have gone on a little).
        std::size_t differing = 0;
        std::size_t firstDifference = 0;
        const std::size_t count = std::min(original.size(), mine.size());
        for (std::size_t i = 0; i < count; ++i) {
            if (original[i] != mine[i]) {
                if (differing == 0) firstDifference = i;
                ++differing;
            }
        }
        char label[300];
        std::snprintf(label, sizeof(label), "request 0x%02X: %zu writes of the original, %zu of this Machine, %zu differ",
                      request, original.size(), mine.size(), differing + (original.size() > mine.size() ? original.size() - mine.size() : 0));
        if (differing > 0 || original.size() > mine.size()) {
            const std::size_t i = firstDifference;
            for (std::size_t k = i; k < i + 14 && k < original.size() && k < mine.size(); ++k)
                std::printf("      [%zu] original %llu %c %u %02x  mine %llu %c %u %02x  delta %lld\n", k, static_cast<unsigned long long>(std::get<0>(original[k])), std::get<1>(original[k]), std::get<2>(original[k]), std::get<3>(original[k]), static_cast<unsigned long long>(std::get<0>(mine[k])), std::get<1>(mine[k]), std::get<2>(mine[k]), std::get<3>(mine[k]), static_cast<long long>(std::get<0>(mine[k])) - static_cast<long long>(std::get<0>(original[k])));
            if (i < original.size() && i < mine.size())
                std::printf("    first difference at %zu: original (%llu %c %u %02x) mine (%llu %c %u %02x)\n", i,
                            static_cast<unsigned long long>(std::get<0>(original[i])), std::get<1>(original[i]), std::get<2>(original[i]), std::get<3>(original[i]),
                            static_cast<unsigned long long>(std::get<0>(mine[i])), std::get<1>(mine[i]), std::get<2>(mine[i]), std::get<3>(mine[i]));
        }
        Check(differing == 0 && original.size() <= mine.size(), label);
        ++compared;
    }
    if (compared == 0) std::printf("  SKIP: no recordings in CHIPESTRA_REFERENCE_DIR\\md\\spaceharrier2\\real_game\n");
}

namespace {

using ChipWrite = std::tuple<std::uint64_t, char, std::uint32_t, std::uint8_t>;  // cycle, 'Y' or 'P', port, value

// How a game's recordings are compared (see CompareWithRecordings).
struct RecordingComparison {
    const char* xmlFile = "";
    const char* gameDir = "";
    unsigned firstEffect = 0;  // a request from this number on is sent as an effect
    // Registers of the YM2612 whose writes are left out of the comparison of what was written (the DAC data and the timer
    // control that a driver writes with every sample: how many there are depends on the timing of the driver's loops).
    std::vector<std::uint8_t> ignoredRegisters;
    // false: the writes must match cycle for cycle at the start of the recording, and the whole recording must hold the
    // same register writes the same number of times. true: the writes (those not ignored) must come in exactly the order
    // of the original's, 99% of them within maxTimeDifference cycles of the original's or of the original's plus or minus
    // a whole number of frames (a request that this Machine's driver handles one frame sooner or later than the original's
    // carries the song with it).
    bool sameOrder = false;
    long long maxTimeDifference = 0;
    std::size_t regionIndex = 0;                // which of the game's regions (region="usa|europe") the recordings are of
    const char* referenceFolder = "real_game";  // below CHIPESTRA_REFERENCE_DIR\md\<gameDir>
};

// The writes that are not to a register in `ignored`, starting at an address write (not between an address and its data).
std::vector<ChipWrite> WithoutIgnoredRegisters(const std::vector<ChipWrite>& writes, const std::vector<std::uint8_t>& ignored) {
    std::vector<ChipWrite> result;
    std::uint32_t latch[2] = {0, 0};
    for (const ChipWrite& w : writes) {
        if (std::get<1>(w) == 'Y') {
            const std::uint32_t part = (std::get<2>(w) >> 1) & 1u;
            if ((std::get<2>(w) & 1u) == 0) latch[part] = std::get<3>(w);
            if (part == 0 && std::find(ignored.begin(), ignored.end(), static_cast<std::uint8_t>(latch[0])) != ignored.end()) continue;
        }
        result.push_back(w);
    }
    std::size_t skip = 0;
    while (skip < result.size() && std::get<1>(result[skip]) == 'Y' && (std::get<2>(result[skip]) & 1u) != 0) ++skip;
    result.erase(result.begin(), result.begin() + static_cast<std::ptrdiff_t>(skip));
    return result;
}

// Plays every recording of a game (CHIPESTRA_REFERENCE_DIR\md\<gameDir>\real_game\req_<number>_writes.txt) through the
// Machine built from the game's definition and compares the YM2612 and PSG writes.
//
// The original's driver waits for the YM2612's timers by polling its status register in a loop. Which pass of the loop
// sees a timer that expires between two passes depends on how the original's scheduler cuts the Z80's run into slices,
// which this Machine does not model; the driver then goes on one pass (or a few) earlier or later, so its writes shift in
// time by that, and now and then swap places with the writes of the frame interrupt.
void CompareWithRecordings(const RecordingComparison& setup) {
    const char* romRoot = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRoot == nullptr || romRoot[0] == '\0') {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is not set - real-cartridge bring-up not exercised\n");
        return;
    }
    const RealGame game = LoadRealGame(setup.xmlFile, setup.regionIndex);
    if (!game.ok) return;
    Check(game.definition.Machine == "SEGA_MEGADRIVE" && game.definition.Variant == "MEGADRIVE_BASE", "machine SEGA_MEGADRIVE / variant MEGADRIVE_BASE");
    const std::filesystem::path dir = MdReferenceDir(setup.referenceFolder, setup.gameDir);
    if (dir.empty()) {
        std::printf("  SKIP: CHIPESTRA_REFERENCE_DIR is not set - the chip writes are not compared with the original's\n");
        return;
    }
    std::error_code ec;
    std::size_t compared = 0, identical = 0;
    std::vector<std::string> files;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) files.push_back(entry.path().filename().string());
    std::sort(files.begin(), files.end());
    constexpr std::size_t kExactPrefix = 180;
    constexpr long kAllowedCountDifference = 10;
    for (const std::string& file : files) {
        if (file.size() < 12 || file.substr(file.size() - 11) != "_writes.txt" || file.rfind("req_", 0) != 0) continue;
        const unsigned request = static_cast<unsigned>(std::strtoul(file.c_str() + 4, nullptr, 16));
        // The original: every YM2612 and PSG write of the Z80. The game releases the Z80 from reset with the first "RESET 1"
        // after the last "RESET 0"; it runs 19 cycles later.
        std::vector<ChipWrite> original;
        std::uint64_t release = 0;
        std::uint64_t lastCycle = 0;
        std::uint64_t requestCycle = 0;  // when the recording put its request in (a recording of the whole game has no such line)
        bool released = true;
        {
            std::ifstream in(dir / file);
            std::string line;
            while (std::getline(in, line)) {
                if (line.empty() || line[0] == '#') continue;
                std::istringstream fields(line);
                unsigned long long cycle = 0;
                std::string kind;
                unsigned a = 0, b = 0;
                fields >> cycle >> kind >> a;
                if (kind == "RESET" && a == 0) {
                    released = false;
                } else if (kind == "RESET" && a == 1) {
                    if (!released) {
                        released = true;
                        release = cycle + 19;
                        original.clear();
                    }
                } else if (kind == "INJECT") {
                    requestCycle = cycle;
                    continue;
                } else if (kind == "YM") {
                    fields >> b;
                    if (cycle >= release) original.emplace_back(cycle, 'Y', a, static_cast<std::uint8_t>(b));
                } else if (kind == "PSG") {
                    if (cycle >= release) original.emplace_back(cycle, 'P', 0u, static_cast<std::uint8_t>(a));
                } else {
                    continue;
                }
                lastCycle = cycle;
            }
        }
        std::vector<ChipWrite> mine;
        auto machine = MakeRealMachine(game, [&](std::uint64_t cycle, char kind, std::uint32_t port, std::uint8_t value) {
            mine.emplace_back(cycle, kind, port, value);
        }, release);
        machine->SendCommand({request >= setup.firstEffect ? MusicCommandKind::Effect : MusicCommandKind::Raw, static_cast<std::uint8_t>(request), 0});
        machine->RunToWithSoundForTest(lastCycle - release + 100);
        // When the game's 68000 writes the set-up requests the Z80 is already running and the 68000 holds its bus for a short
        // time at each write, which this Machine does not model; the driver is back in step with the frame interrupt
        // well before the request. Such a recording is compared from the request on.
        if (requestCycle != 0) {
            auto before = [&](const ChipWrite& w) { return std::get<0>(w) < requestCycle; };
            original.erase(std::remove_if(original.begin(), original.end(), before), original.end());
            mine.erase(std::remove_if(mine.begin(), mine.end(), before), mine.end());
        }
        char label[400];
        if (setup.sameOrder) {
            const std::vector<ChipWrite> a = WithoutIgnoredRegisters(original, setup.ignoredRegisters);
            const std::vector<ChipWrite> b = WithoutIgnoredRegisters(mine, setup.ignoredRegisters);
            const std::size_t count = std::min(a.size(), b.size());
            std::size_t sameUpTo = 0, late = 0, shifted = 0;
            long long worst = 0;
            while (sameUpTo < count && std::get<1>(a[sameUpTo]) == std::get<1>(b[sameUpTo]) && std::get<2>(a[sameUpTo]) == std::get<2>(b[sameUpTo]) &&
                   std::get<3>(a[sameUpTo]) == std::get<3>(b[sameUpTo])) {
                long long delta = static_cast<long long>(std::get<0>(a[sameUpTo])) - static_cast<long long>(std::get<0>(b[sameUpTo]));
                const long long frame = static_cast<long long>(machine->TimingForTest().cyclesPerFrame);
                const long long frames = (delta + (delta < 0 ? -frame / 2 : frame / 2)) / frame;
                delta -= frames * frame;
                if (frames != 0) ++shifted;
                worst = std::max(worst, delta < 0 ? -delta : delta);
                if (delta > setup.maxTimeDifference || delta < -setup.maxTimeDifference) ++late;
                ++sameUpTo;
            }
            std::snprintf(label, sizeof(label), "request 0x%02X: %zu of the original's %zu register writes (those not ignored) come in the same order in this Machine's %zu; %zu of them a whole number of frames off, %zu more than %lld cycles apart, the largest difference in time %lld cycles",
                          request, sameUpTo, a.size(), b.size(), shifted, late, setup.maxTimeDifference, worst);
            // The end of the run may fall a few writes apart.
            const bool ok = sameUpTo + 32 >= a.size() && sameUpTo + 32 >= b.size() && late * 100 <= sameUpTo;
            Check(ok, label);
            if (ok) ++identical;
            ++compared;
            continue;
        }
        // The recording takes the time of the release from the emulated clock, which is good to a cycle or two:
        // line the first write up and compare every other one against that.
        if (!original.empty() && !mine.empty()) {
            const long long shift = static_cast<long long>(std::get<0>(original[0])) - static_cast<long long>(std::get<0>(mine[0]));
            Check(shift >= -3 && shift <= 3, "the first write falls within 3 cycles of the original's");
            for (auto& write : mine) std::get<0>(write) = static_cast<std::uint64_t>(static_cast<long long>(std::get<0>(write)) + shift);
        }
        const std::size_t count = std::min(original.size(), mine.size());
        std::size_t firstDifference = count;
        for (std::size_t i = 0; i < count; ++i) {
            if (original[i] != mine[i]) {
                firstDifference = i;
                break;
            }
        }
        std::map<std::tuple<char, std::uint32_t, std::uint8_t>, long> balance;
        const std::uint64_t cutoff = lastCycle > release + 60000 ? lastCycle - 60000 : 0;
        for (const ChipWrite& w : WithoutIgnoredRegisters(original, setup.ignoredRegisters)) if (std::get<0>(w) < cutoff) ++balance[{std::get<1>(w), std::get<2>(w), std::get<3>(w)}];
        for (const ChipWrite& w : WithoutIgnoredRegisters(mine, setup.ignoredRegisters)) if (std::get<0>(w) < cutoff) --balance[{std::get<1>(w), std::get<2>(w), std::get<3>(w)}];
        long unbalanced = 0;
        for (const auto& entry : balance) unbalanced += entry.second < 0 ? -entry.second : entry.second;
        std::snprintf(label, sizeof(label), "request 0x%02X: %zu writes of the original, %zu of this Machine; the first %zu agree exactly, the whole run differs by %ld in the count of a register write",
                      request, original.size(), mine.size(), firstDifference, unbalanced);
        Check(firstDifference >= std::min(kExactPrefix, original.size()) && original.size() <= mine.size() + 10 && unbalanced <= kAllowedCountDifference, label);
        if (firstDifference == count && original.size() == mine.size()) ++identical;
        ++compared;
    }
    if (compared == 0) {
        std::printf("  SKIP: no recordings in CHIPESTRA_REFERENCE_DIR\\md\\%s\\real_game\n", setup.gameDir);
    } else {
        std::printf("  information only: %zu of %zu recordings are reproduced %s\n", identical, compared,
                    setup.sameOrder ? "in the same order of writes to the end" : "write for write to the end");
    }
}

}  // namespace

void MegaDriveSorcerianMatchesTheOriginalsChipWrites() {
    std::printf("MegaDriveSorcerianMatchesTheOriginalsChipWrites\n");
    RecordingComparison setup;
    setup.xmlFile = "sorcerian.xml";
    setup.gameDir = "sorcerian";
    setup.firstEffect = 0xB0;
    CompareWithRecordings(setup);
}

void MegaDriveTimeTraxMatchesTheOriginalsChipWrites() {
    std::printf("MegaDriveTimeTraxMatchesTheOriginalsChipWrites%c", 10);
    // The game lists two consoles (region="usa|europe"): the recordings of each are of the original on that console
    // (MAME's genesis and megadriv). On the NTSC console the sound program's work for one tick of a song can take almost a
    // whole frame (about 59.7 thousand cycles) at one place of request 0x04; whether the program then reads its
    // frame counter again before or after the three interrupts of the frame (the line stays up for 228 cycles and the
    // handler takes it again after each EI) differs by a few cycles between the original and this Machine, and it skips one
    // tick of the song, which shifts the rest of that song by a frame (once in the original, once here). The check
    // therefore allows a write to be a whole number of frames off. A PAL frame is 11.6 thousand cycles longer and the
    // reading never falls into the interrupts: no write is a frame off.
    // The driver puts a sample into the DAC (register 0x2A) with every pass of a loop that waits for the YM2612's Timer A
    // (register 0x27), some 8700 times a second, so those two registers are left out; the rest is the music and the sounds.
    RecordingComparison setup;
    setup.xmlFile = "timetrax.xml";
    setup.gameDir = "timetrax";
    setup.firstEffect = 0x06;
    setup.ignoredRegisters = {0x27, 0x2A};
    setup.sameOrder = true;
    setup.maxTimeDifference = 1500;
    std::printf("  NTSC (region usa)%c", 10);
    setup.regionIndex = 0;
    setup.referenceFolder = "real_game_ntsc";
    CompareWithRecordings(setup);
    std::printf("  PAL (region europe)%c", 10);
    setup.regionIndex = 1;
    setup.referenceFolder = "real_game";
    CompareWithRecordings(setup);
}

void MegaDriveRegionListSelectsOneRegion() {
    std::printf("MegaDriveRegionListSelectsOneRegion%c", 10);
    const std::string q(1, char(34));
    const std::string driver = "<driver music=" + q + "0:16" + q + " region=" + q + "usa|europe" + q + " warmup_frames=" + q + "784|782" + q + " release_cycle=" + q + "100" + q + "/>";
    Check(CountMegaDriveDriverRegions(driver) == 2 && CountMegaDriveDriverRegions("<driver music=" + q + "0:16" + q + "/>") == 1, "the regions of the list are counted (none or one counts one)");
    std::string error;
    MegaDriveDriverConfig config;
    Check(ParseMegaDriveDriverConfig(SelectMegaDriveDriverRegion(driver, 0, &error), config, &error) && config.region == MegaDriveRegion::Usa && config.requestCycle == (100 / 59736 + 784) * 59736,
          "region number 0 is the first: usa, 784 frames of 262 lines");
    Check(ParseMegaDriveDriverConfig(SelectMegaDriveDriverRegion(driver, 1, &error), config, &error) && config.region == MegaDriveRegion::Europe && config.requestCycle == (100 / 71364 + 782) * 71364,
          "region number 1 is the second: europe, 782 frames of 313 lines; the single release_cycle serves both");
    Check(SelectMegaDriveDriverRegion(driver, 2, &error).empty(), "a region number beyond the list is refused");
    const std::string uneven = "<driver music=" + q + "0:16" + q + " region=" + q + "usa|europe" + q + " warmup_frames=" + q + "1|2|3" + q + " release_cycle=" + q + "0" + q + "/>";
    Check(SelectMegaDriveDriverRegion(uneven, 0, &error).empty() && error.find("warmup_frames") != std::string::npos, "a list that does not fit the regions is refused, naming the attribute");
    Check(!ParseMegaDriveDriverConfig(driver, config, &error), "the Machine itself takes one region only");
}

void MegaDriveVoicesPlayTheSameSamplesAsTheOriginal() {
    std::printf("MegaDriveVoicesPlayTheSameSamplesAsTheOriginal\n");
    const char* romRoot = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRoot == nullptr || romRoot[0] == '\0') {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is not set - real-cartridge bring-up not exercised\n");
        return;
    }
    const std::filesystem::path dir = MdReferenceDir("real_game");
    if (dir.empty()) {
        std::printf("  SKIP: CHIPESTRA_REFERENCE_DIR is not set - the voices are not compared with the original's\n");
        return;
    }
    const RealGame game = LoadRealGame();
    if (!game.ok) return;
    for (unsigned set = 1; set <= 2; ++set) {
        // "<cycles after the release> <value>" for every write to the YM2612 data port the original's voice program made.
        std::ifstream in(dir / ("voice" + std::to_string(set) + "_dac.txt"));
        if (!in) {
            std::printf("  SKIP: voice%u_dac.txt is not in CHIPESTRA_REFERENCE_DIR\\md\\spaceharrier2\\real_game\n", set);
            continue;
        }
        std::vector<std::pair<std::uint64_t, unsigned>> original;
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream fields(line);
            unsigned long long cycle = 0;
            unsigned value = 0;
            if (fields >> cycle >> value) original.emplace_back(cycle, value);
        }
        std::vector<std::pair<std::uint64_t, unsigned>> mine;
        unsigned address = 0;
        std::uint64_t release = 0;
        auto machine = MakeRealMachine(game);
        release = machine->ReleaseCycleForTest();
        machine->SetChipWriteObserverForTest([&](std::uint64_t cycle, char kind, std::uint32_t port, std::uint8_t value) {
            if (kind != 'Y') return;
            if (port == 0) address = value;
            else if (port == 1 && address == 0x2A) mine.emplace_back(cycle - release, value);
        });
        machine->SendCommand({MusicCommandKind::Raw, 0, static_cast<std::uint8_t>(set)});
        machine->RunToForTest(original.empty() ? 0 : original.back().first + 1000);
        std::size_t differing = 0;
        for (std::size_t i = 0; i < std::min(original.size(), mine.size()); ++i)
            if (original[i].second != mine[i].second || original[i].first != mine[i].first) ++differing;
        for (std::size_t i = 0; differing > 0 && i < 4 && i < mine.size() && i < original.size(); ++i)
            std::printf("    [%zu] original %llu %u  mine %llu %u\n", i, static_cast<unsigned long long>(original[i].first), original[i].second, static_cast<unsigned long long>(mine[i].first), mine[i].second);
        char label[200];
        std::snprintf(label, sizeof(label), "voice %u: %zu DAC writes of the original, %zu of this Machine, %zu differ in value or time",
                      set, original.size(), mine.size(), differing);
        Check(differing == 0 && original.size() == mine.size(), label);
    }
}

namespace {

// Renders `blocks` blocks of the Machine and appends every channel's samples to `out`.
void RenderCollect(MegaDriveMachine& machine, LogicalChannelSink& sink, std::size_t blocks, std::vector<float>& out) {
    const auto& channels = machine.LogicalChannels();
    for (std::size_t b = 0; b < blocks; ++b) {
        sink.ClearAll();
        machine.Render(EmuDuration{Mixer::kBlockSizeFrames}, &sink);
        for (const LogicalChannel& channel : channels) {
            const LogicalChannelBuffer& buf = sink.ChannelBuffer(channel.Id);
            for (std::size_t k = 0; k < buf.Size(); ++k) out.push_back(static_cast<float>(buf[k]));
        }
    }
}

}  // namespace

void MegaDriveSwitchTimeReport() {
    std::printf("MegaDriveSwitchTimeReport\n");
    const char* romRoot = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRoot == nullptr || romRoot[0] == '\0') {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is not set - real-cartridge bring-up not exercised\n");
        return;
    }
    const RealGame game = LoadRealGame();
    if (!game.ok) return;
    auto machine = MakeRealMachine(game);
    using Clock = std::chrono::steady_clock;
    double first = 0, later = 0;
    for (int i = 0; i < 6; ++i) {
        const auto t0 = Clock::now();
        machine->SendCommand({MusicCommandKind::Raw, static_cast<std::uint8_t>(0x81 + i), 0});
        const double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        if (i == 0) first = ms;
        else later += ms / 5;
    }
    std::printf("  switch time: first command %.3f ms, later commands %.3f ms on average\n", first, later);
}

namespace {

struct SwitchCase {
    MusicCommandKind kind;
    std::uint8_t value;
};

}  // namespace

void MegaDriveRestoredStateSoundsLikeTheWarmUp() {
    std::printf("MegaDriveRestoredStateSoundsLikeTheWarmUp\n");
    const char* romRoot = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRoot == nullptr || romRoot[0] == '\0') {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is not set - real-cartridge bring-up not exercised\n");
        return;
    }
    const RealGame game = LoadRealGame();
    if (!game.ok) return;
    std::vector<SwitchCase> tracks;
    for (std::uint8_t v = 0x81; v <= 0x99; ++v) tracks.push_back({MusicCommandKind::Raw, v});
    for (std::uint8_t v = 0xA0; v <= 0xB9; ++v) tracks.push_back({MusicCommandKind::Effect, v});
    tracks.push_back({MusicCommandKind::Effect, 0xBB});
    tracks.push_back({MusicCommandKind::Raw, 0x80});
    constexpr std::size_t kBlocks = 150;  // about 0.8 s at 192 kHz in 1024-frame blocks

    // Machine A restores the saved state at every switch (the default), machine B runs the warm-up every time. Both
    // go through the same sequence of switches, so the second and later tracks also test repeated switching.
    auto restoring = MakeRealMachine(game);
    auto running = MakeRealMachine(game);
    running->SetWarmStateRestoreForTest(false);
    LogicalChannelSink sinkA, sinkB;
    std::size_t differing = 0, compared = 0, audible = 0;
    for (int round = 0; round < 2; ++round) {
        for (const SwitchCase& track : tracks) {
            const MusicCommand command{track.kind, track.value, 0};
            restoring->SendCommand(command);
            running->SendCommand(command);
            std::vector<float> a, b;
            RenderCollect(*restoring, sinkA, kBlocks, a);
            RenderCollect(*running, sinkB, kBlocks, b);
            ++compared;
            if (std::any_of(a.begin(), a.end(), [](float v) { return std::fabs(v) > 0.001f; })) ++audible;
            if (a != b) {
                ++differing;
                std::printf("    differs: round %d request 0x%02X\n", round, static_cast<unsigned>(track.value));
            }
        }
    }
    char label[160];
    std::snprintf(label, sizeof(label), "%zu switches: the restored state and the warm-up run sound identical (%zu differ)", compared, differing);
    Check(differing == 0 && compared > 0, label);
    Check(audible * 4 > compared * 3, "most of the compared tracks are audible (the comparison is not of silence)");
}

void MegaDriveVoiceEndsInSilence() {
    std::printf("MegaDriveVoiceEndsInSilence\n");
    const char* romRoot = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRoot == nullptr || romRoot[0] == '\0') {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is not set - real-cartridge bring-up not exercised\n");
        return;
    }
    const RealGame game = LoadRealGame();
    if (!game.ok) return;
    // The original's voice programs write 4139 samples to the DAC: voice 1 for 1.43 s (1237 cycles apart), voice 2 for
    // 0.468 s (405 cycles apart). The length of the sound is measured here from the rendered channels, in steps of 15 blocks (exactly 20 ms).
    const double expectedSeconds[2] = {1.430, 0.468};
    constexpr std::size_t kBlocksPerStep = 15;
    const double stepSeconds = static_cast<double>(kBlocksPerStep * Mixer::kBlockSizeFrames) / kNativeRuntimeTickRateHz;
    for (std::uint8_t set = 1; set <= 2; ++set) {
        auto machine = MakeRealMachine(game);
        machine->SendCommand({MusicCommandKind::Raw, 0, set});
        LogicalChannelSink sink;
        double firstAudible = -1, lastAudible = -1;
        for (std::size_t step = 0; step < 600; ++step) {  // 12 seconds
            std::vector<float> samples;
            RenderCollect(*machine, sink, kBlocksPerStep, samples);
            float peak = 0;
            for (float v : samples) peak = std::max(peak, std::fabs(v));
            if (peak > 0.01f) {
                if (firstAudible < 0) firstAudible = static_cast<double>(step) * stepSeconds;
                lastAudible = static_cast<double>(step + 1) * stepSeconds;
            }
        }
        const double length = lastAudible - firstAudible;
        std::printf("  voice %u: sounds from %.2f s to %.2f s (%.2f s long); Z80 PC at the end 0x%04X, request byte 0x%02X\n",
                    static_cast<unsigned>(set), firstAudible, lastAudible, length,
                    static_cast<unsigned>(machine->CpuSnapshotForTest().pc), static_cast<unsigned>(machine->Z80RamForTest(0x1804)));
        Check(firstAudible >= 0 && std::fabs(length - expectedSeconds[set - 1]) < 0.06,
              "the voice is as long as the original's (the DAC writes, 1.43 s and 0.468 s)");
        Check(lastAudible < 11.9, "the voice ends in silence (no song follows it)");
    }
}

void MegaDriveSongSoundsFromTheRequestOn() {
    std::printf("MegaDriveSongSoundsFromTheRequestOn\n");
    const char* romRoot = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRoot == nullptr || romRoot[0] == '\0') {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is not set - real-cartridge bring-up not exercised\n");
        return;
    }
    const RealGame game = LoadRealGame();
    if (!game.ok) return;
    auto machine = MakeRealMachine(game);
    LogicalChannelSink sink;
    constexpr std::size_t kBlocks = 100;
    // The chips' samples of the first blocks cover exactly the time rendered (not the idle time of the warm-up).
    const std::size_t expectedYm = kBlocks * Mixer::kBlockSizeFrames * MegaDriveMachine::kYmStreamRateHz / kNativeRuntimeTickRateHz;
    const std::size_t expectedPsg = kBlocks * Mixer::kBlockSizeFrames * MegaDriveMachine::kPsgStreamRateHz / kNativeRuntimeTickRateHz;
    for (int round = 0; round < 2; ++round) {
        machine->SendCommand({MusicCommandKind::Raw, 0x81, 0});
        std::size_t ymSamples = 0, psgSamples = 0;
        float peak = 0;
        for (std::size_t b = 0; b < kBlocks; ++b) {
            sink.ClearAll();
            machine->Render(EmuDuration{Mixer::kBlockSizeFrames}, &sink);
            ymSamples += sink.ChannelBuffer(machine->LogicalChannels()[0].Id).Size();
            psgSamples += sink.ChannelBuffer(machine->LogicalChannels()[MegaDriveMachine::kPsgFirstChannel].Id).Size();
            for (const LogicalChannel& channel : machine->LogicalChannels()) {
                const LogicalChannelBuffer& buf = sink.ChannelBuffer(channel.Id);
                for (std::size_t k = 0; k < buf.Size(); ++k) peak = std::max(peak, std::fabs(static_cast<float>(buf[k])));
            }
        }
        const char* when = round == 0 ? "the first song" : "a song after a switch";
        char label[200];
        std::snprintf(label, sizeof(label), "%s: %zu YM2612 and %zu PSG samples in 100 blocks (expected about %zu and %zu)", when, ymSamples, psgSamples, expectedYm, expectedPsg);
        Check(ymSamples + 4 >= expectedYm && ymSamples <= expectedYm + 4 && psgSamples + 4 >= expectedPsg && psgSamples <= expectedPsg + 4, label);
        Check(peak > 0.005f, "and it sounds");
    }
}

namespace {

// Encodes 68000-order bytes as a Super Magic Drive file: a 512-byte header (AA BB at offsets 8 and 9) and 16 KB blocks
// with the odd bytes in the first half and the even bytes in the second.
std::vector<std::uint8_t> EncodeAsSuperMagicDrive(const std::vector<std::uint8_t>& plain) {
    constexpr std::size_t kBlock = 16384;
    std::vector<std::uint8_t> file(512 + plain.size(), 0);
    file[0] = static_cast<std::uint8_t>(plain.size() / kBlock);
    file[1] = 3;
    file[8] = 0xAA;
    file[9] = 0xBB;
    for (std::size_t block = 0; block < plain.size() / kBlock; ++block) {
        for (std::size_t i = 0; i < kBlock / 2; ++i) {
            file[512 + block * kBlock + i] = plain[block * kBlock + 2 * i + 1];
            file[512 + block * kBlock + kBlock / 2 + i] = plain[block * kBlock + 2 * i];
        }
    }
    return file;
}

MachineResources CartridgeResources(const std::vector<std::uint8_t>& file, const std::string& driverXml) {
    MachineResources resources;
    resources.Add(ResolvedResource("rom", file));
    resources.Add(ResolvedResource("driver_xml", std::vector<std::uint8_t>(driverXml.begin(), driverXml.end())));
    return resources;
}

std::string HexCrc(std::uint32_t crc) {
    char text[16];
    std::snprintf(text, sizeof(text), "%08x", crc);
    return text;
}

}  // namespace

void MegaDriveCompressedDriverSongsAndConstants() {
    std::printf("MegaDriveCompressedDriverSongsAndConstants\n");
    // A stream of eight literals: count 8 (the bytes after the count, less one), a flag byte of all ones, the literals.
    std::vector<std::uint8_t> cartridge(0x100, 0);
    const std::vector<std::uint8_t> literals = {0x00, 0x08, 0xFF, 1, 2, 3, 4, 5, 6, 7, 8};
    std::copy(literals.begin(), literals.end(), cartridge.begin() + 0x10);
    // A literal and a match against the window: 0x41, then three bytes copied from window position 0xFEE.
    const std::vector<std::uint8_t> match = {0x00, 0x03, 0x80, 0x41, 0xFE, 0xE0};
    std::copy(match.begin(), match.end(), cartridge.begin() + 0x30);
    std::vector<std::uint8_t> out;
    std::uint32_t used = 0;
    Check(DecodeMegaDriveLzss(cartridge, 0x10, out, &used) && out == std::vector<std::uint8_t>({1, 2, 3, 4, 5, 6, 7, 8}) && used == 11, "eight literals decode, and the stream is 11 bytes long");
    Check(DecodeMegaDriveLzss(cartridge, 0x30, out, &used) && out == std::vector<std::uint8_t>({0x41, 0x41, 0x41, 0x41}) && used == 6, "a literal and a match decode");
    Check(!DecodeMegaDriveLzss(cartridge, 0xFE, out, &used), "a stream that runs past the end of the cartridge is refused");

    // A table of two songs: offsets from the table, each to a stream.
    std::vector<std::uint8_t> withTable(0x100, 0);
    withTable[0x80] = 0x00; withTable[0x81] = 0x04;                 // first offset 4 = two songs
    withTable[0x82] = 0x00; withTable[0x83] = 0x04 + 11;            // second song after the first
    std::copy(literals.begin(), literals.end(), withTable.begin() + 0x84);
    std::copy(match.begin(), match.end(), withTable.begin() + 0x84 + 11);
    for (std::size_t i = 0; i < 4; ++i) withTable[0x20 + i] = static_cast<std::uint8_t>(0xA0 + i);
    const std::string xml = "<driver music=\"0x84:lzss\" music_data=\"0x20:4@0x1C00\" song_table=\"0x80\" song_first=\"0x81\" song_address=\"0x1000\" request=\"0x1C0A\" release_cycle=\"0\" warmup_frames=\"0\"/>";
    MegaDriveMachineInputs inputs = AssembleMegaDriveMachineInputs(CartridgeResources(withTable, xml));
    Check(inputs.driver.music.lzss && inputs.driver.music.size == 11, "the compressed driver is found and its length taken from the stream");
    Check(inputs.driver.songCount == 2 && inputs.driver.songFirst == 0x81 && inputs.driver.songAddress == 0x1000, "the song table has two songs");
    Check(inputs.driver.musicData.size() == 1 && inputs.driver.musicData[0].address == 0x1C00 && inputs.driver.musicData[0].size == 4, "the constants are listed");
    auto machine = std::make_unique<MegaDriveMachine>(std::move(inputs.cartridge), std::move(inputs.driver));
    machine->Initialize();
    machine->SendCommand({MusicCommandKind::Raw, 0x82, 0});
    Check(machine->Z80RamForTest(0) == 1 && machine->Z80RamForTest(7) == 8, "the driver is decompressed to the start of the Z80 RAM");
    Check(machine->Z80RamForTest(0x1C00) == 0xA0 && machine->Z80RamForTest(0x1C03) == 0xA3, "the constants are copied after it");
    Check(machine->Z80RamForTest(0x1000) == 0x41 && machine->Z80RamForTest(0x1003) == 0x41 && machine->Z80RamForTest(0x1004) == 0, "the second song is decompressed to 0x1000");
    Check(machine->Z80RamForTest(0x1C0A) == 0x82, "and the request is written");
    machine->SendCommand({MusicCommandKind::Effect, 0xB0, 0});
    Check(machine->Z80RamForTest(0x1000) == 1 && machine->Z80RamForTest(0x1C0A) == 0xB0, "a request outside the songs loads the first song");

    std::string error;
    MegaDriveDriverConfig config;
    Check(!ParseMegaDriveDriverConfig("<driver music=\"0:lzss\" music_data=\"0:4\" release_cycle=\"0\" warmup_frames=\"0\"/>", config, &error), "music_data needs an address");
    Check(!ParseMegaDriveDriverConfig("<driver music=\"0:lzss\" music_data=\"0:16@0x1FF8\" release_cycle=\"0\" warmup_frames=\"0\"/>", config, &error), "music_data must end inside the Z80 RAM");
    Check(!ParseMegaDriveDriverConfig("<driver music=\"0:lzss\" song_first=\"0\" release_cycle=\"0\" warmup_frames=\"0\"/>", config, &error), "song_first is at least 1");
    Check(ParseMegaDriveDriverConfig("<driver music=\"0:lzss\" release_cycle=\"0\" warmup_frames=\"0\"/>", config, &error) && config.music.lzss && !config.songTable.has_value(), "a compressed driver without songs parses");
    bool refused = false;
    try {
        (void)AssembleMegaDriveMachineInputs(CartridgeResources(cartridge, "<driver music=\"0xFE:lzss\" release_cycle=\"0\" warmup_frames=\"0\"/>"));
    } catch (const std::runtime_error&) {
        refused = true;
    }
    Check(refused, "a compressed driver that does not decode is refused");
}

void MegaDriveCartridgeFileLayouts() {
    std::printf("MegaDriveCartridgeFileLayouts\n");
    // A synthetic 64 KB cartridge whose bytes are all different from their neighbours, so a wrong layout shows.
    std::vector<std::uint8_t> plain(65536);
    for (std::size_t i = 0; i < plain.size(); ++i) plain[i] = static_cast<std::uint8_t>((i * 7 + (i >> 8) * 13 + 1) & 0xFF);
    const std::vector<std::uint8_t> smd = EncodeAsSuperMagicDrive(plain);
    const std::uint32_t crc = Crc32(plain.data(), plain.size());
    const std::string driver = "<driver music=\"0:16\" release_cycle=\"0\" warmup_frames=\"0\" cart_crc32=\"" + HexCrc(crc) + "\"/>";

    Check(DecodeMegaDriveCartridge(smd.data(), smd.size()) == plain, "a Super Magic Drive file (header, interleaved) is made plain");
    Check(DecodeMegaDriveCartridge(plain.data(), plain.size()) == plain, "a plain image (.bin, .md) is used as it is");
    MegaDriveMachineInputs fromSmd = AssembleMegaDriveMachineInputs(CartridgeResources(smd, driver));
    MegaDriveMachineInputs fromPlain = AssembleMegaDriveMachineInputs(CartridgeResources(plain, driver));
    Check(fromSmd.cartridge == plain && fromPlain.cartridge == plain, "both file layouts give the same cartridge and pass the CRC-32 check");
    Check(fromSmd.driver.cartridgeCrc32 == crc, "the CRC-32 of <driver cart_crc32> is read");

    std::string message;
    try {
        AssembleMegaDriveMachineInputs(CartridgeResources(smd, "<driver music=\"0:16\" release_cycle=\"0\" warmup_frames=\"0\" cart_crc32=\"12345678\"/>"));
    } catch (const std::exception& e) {
        message = e.what();
    }
    Check(message.find("cart_crc32") != std::string::npos && message.find(HexCrc(crc)) != std::string::npos,
          "a cartridge with another CRC-32 is refused, with both numbers in the message");
    std::vector<std::uint8_t> damaged = plain;
    damaged[1000] ^= 1;
    message.clear();
    try {
        AssembleMegaDriveMachineInputs(CartridgeResources(damaged, driver));
    } catch (const std::exception& e) {
        message = e.what();
    }
    Check(!message.empty(), "a damaged plain image is refused");
    MegaDriveDriverConfig config;
    std::string error;
    Check(!ParseMegaDriveDriverConfig("<driver music=\"0:16\" release_cycle=\"0\" warmup_frames=\"0\" cart_crc32=\"xyz\"/>", config, &error),
          "a cart_crc32 that is not hexadecimal is refused");
    Check(ParseMegaDriveDriverConfig("<driver music=\"0:16\" release_cycle=\"0\" warmup_frames=\"0\"/>", config, &error) && !config.cartridgeCrc32.has_value(),
          "cart_crc32 is optional");
    const std::vector<std::string> names = SplitAcceptedNames("a.smd|a.bin||a.md");
    Check(names.size() == 3 && names[1] == "a.bin" && SplitAcceptedNames("one.bin") == std::vector<std::string>{"one.bin"},
          "<rom file> may list alternative names separated by |");

    // The real game, in both layouts.
    const char* romRoot = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRoot == nullptr || romRoot[0] == '\0') {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is not set - the real cartridge is not tried in both layouts\n");
        return;
    }
    const RealGame game = LoadRealGame();
    if (!game.ok) return;
    const ResolvedResource* file = game.resolution.Resources.Find("rom");
    Check(file != nullptr, "the cartridge was resolved");
    if (file == nullptr) return;
    const std::vector<std::uint8_t> realPlain = DecodeMegaDriveCartridge(file->Data(), file->Size());
    const MegaDriveMachineInputs real = AssembleMegaDriveMachineInputs(game.resolution.Resources);
    const MegaDriveMachineInputs again = AssembleMegaDriveMachineInputs(CartridgeResources(realPlain, game.driverXml));
    Check(realPlain.size() == 524288 && real.cartridge == again.cartridge && Crc32(realPlain.data(), realPlain.size()) == 0xedc0fb28u,
          "Space Harrier II: the file as it is and the same cartridge as a plain image are the same cartridge (CRC-32 edc0fb28)");
    // The same sound from both.
    auto fromFile = MakeRealMachine(game);
    MegaDriveMachine plainMachine(again.cartridge, again.driver);
    plainMachine.Initialize();
    LogicalChannelSink sinkA, sinkB;
    fromFile->SendCommand({MusicCommandKind::Raw, 0x84, 0});
    plainMachine.SendCommand({MusicCommandKind::Raw, 0x84, 0});
    std::vector<float> a, b;
    RenderCollect(*fromFile, sinkA, 300, a);
    RenderCollect(plainMachine, sinkB, 300, b);
    Check(a == b && !a.empty(), "both layouts sound the same");
}
