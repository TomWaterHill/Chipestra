// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// MegaDrive68kMachine native tests.

#include "megadrive68k_machine_tests.h"

#include "default_machine_catalog.h"
#include "definition_xml_locator.h"
#include "devices/nintendo/nes/nes_driver_config.h"
#include "devices/sega/megadrive/megadrive68k_machine.h"
#include "machine_registry.h"
#include "mixer.h"
#include "resource_hash.h"
#include "resource_resolver.h"
#include "test_support.h"
#include "xml_definition_parser.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
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

constexpr std::uint64_t kTicksPerFrame = 262 * 3420;   // NTSC

// A small cartridge written for the tests (68000 machine code assembled by hand; no game code).
//   0x0000  vector table: SSP 0xFFFE00 (the reset and level-6 vectors are replaced by the Machine)
//   0x1000  init: the Z80 gets "LD A,0x55 / LD (0x1000),A / JR $" and is started
//   0x2000  frame: counts the frames in RAM 0xFF0100; if the request byte (0xFF0002) is not 0, plays a YM2612 write
//           through the Z80's bus and clears the request
struct SyntheticCartridge {
    std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(0x4000, 0);
    std::uint32_t at = 0;
    void Word(std::uint16_t w) { bytes[at++] = static_cast<std::uint8_t>(w >> 8); bytes[at++] = static_cast<std::uint8_t>(w); }
    void Long(std::uint32_t v) { Word(static_cast<std::uint16_t>(v >> 16)); Word(static_cast<std::uint16_t>(v)); }
    void SetByte(std::uint32_t a, std::uint8_t v) { bytes[a] = v; }
    explicit SyntheticCartridge(std::vector<std::uint8_t> z80 = {0x3E, 0x55, 0x32, 0x00, 0x10, 0x18, 0xFE}) {
        bytes[0] = 0x00; bytes[1] = 0xFF; bytes[2] = 0xFE; bytes[3] = 0x00;
        // init
        at = 0x1000;
        Word(0x33FC); Word(0x0100); Long(0x00A11100);       // MOVE.W #$100,$A11100   request the Z80's bus
        Word(0x33FC); Word(0x0100); Long(0x00A11200);       // MOVE.W #$100,$A11200   release the reset
        for (std::uint32_t i = 0; i < z80.size(); ++i) { Word(0x13FC); Word(z80[i]); Long(0x00A00000 + i); }   // MOVE.B #imm,$A0000i
        Word(0x33FC); Word(0x0000); Long(0x00A11200);       // MOVE.W #0,$A11200      reset
        Word(0x4E71); Word(0x4E71);
        Word(0x33FC); Word(0x0100); Long(0x00A11200);       // MOVE.W #$100,$A11200   start
        Word(0x33FC); Word(0x0000); Long(0x00A11100);       // MOVE.W #0,$A11100      release the bus
        Word(0x4E75);                                       // RTS
        // frame
        at = 0x2000;
        Word(0x5239); Long(0x00FF0100);                     // ADDQ.B #1,$FF0100
        Word(0x4A39); Long(0x00FF0002);                     // TST.B $FF0002
        Word(0x6700); Word(0x0028);                         // BEQ.W done
        Word(0x33FC); Word(0x0100); Long(0x00A11100);       // MOVE.W #$100,$A11100
        Word(0x13FC); Word(0x002A); Long(0x00A04000);       // MOVE.B #$2A,$A04000    YM2612 register
        Word(0x13FC); Word(0x0080); Long(0x00A04001);       // MOVE.B #$80,$A04001    its data
        Word(0x33FC); Word(0x0000); Long(0x00A11100);       // MOVE.W #0,$A11100
        Word(0x4239); Long(0x00FF0002);                     // CLR.B $FF0002
        Word(0x4E75);                                       // done: RTS
    }
};

MegaDrive68kDriverConfig SyntheticConfig() {
    MegaDrive68kDriverConfig config;
    config.init = {0x1000};
    config.frame = 0x2000;
    config.requestAddress = 0xFF0002;
    config.warmupFrames = 3;
    return config;
}

std::string DriverXml(const std::string& attributes) { return "<driver " + attributes + "/>"; }

struct Event { std::uint64_t t; std::string kind; std::string a; };

std::vector<Event> LoadMameLog(const std::filesystem::path& path) {
    std::vector<Event> events;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream words(line);
        Event e;
        std::string first, rest;
        if (!(words >> e.t >> e.kind)) continue;
        std::getline(words, e.a);
        events.push_back(std::move(e));
    }
    return events;
}

}  // namespace

void MegaDrive68kDriverConfigIsParsedAndChecked() {
    std::printf("MegaDrive68kDriverConfigIsParsedAndChecked\n");
    MegaDrive68kDriverConfig config;
    std::string error;
    Check(ParseMegaDrive68kDriverConfig(DriverXml("m68k_init=\"0x134C 0x200\" m68k_frame=\"0x71B4C\" request=\"0xFFF00B\" warmup_frames=\"4\" region=\"usa\" cart_crc32=\"afe05eee\""), config, &error),
          "a complete <driver> parses");
    Check(config.init.size() == 2 && config.init[0] == 0x134C && config.init[1] == 0x200 && config.frame == 0x71B4C && config.requestAddress == 0xFFF00B &&
              config.warmupFrames == 4 && config.region == MegaDriveRegion::Usa && config.cartridgeCrc32 == 0xAFE05EEEu,
          "the numbers are read as written");
    Check(!ParseMegaDrive68kDriverConfig(DriverXml("m68k_init=\"0x134C\" request=\"0xFFF00B\" warmup_frames=\"4\""), config, &error) && error.find("m68k_frame") != std::string::npos,
          "m68k_frame is required, and the error names it");
    Check(!ParseMegaDrive68kDriverConfig(DriverXml("m68k_frame=\"0x71B4C\" request=\"0x1234\" warmup_frames=\"4\""), config, &error) && error.find("request") != std::string::npos,
          "the request byte must be in the 68000's RAM");
    Check(!ParseMegaDrive68kDriverConfig(DriverXml("m68k_init=\"0x134D\" m68k_frame=\"0x71B4C\" request=\"0xFFF00B\" warmup_frames=\"4\""), config, &error) && error.find("m68k_init") != std::string::npos,
          "a routine at an odd address is refused");
    Check(!ParseMegaDrive68kDriverConfig(DriverXml("m68k_frame=\"0x71B4C\" request=\"0xFFF00B\" warmup_frames=\"4\" region=\"mars\""), config, &error) && error.find("region") != std::string::npos,
          "an unknown region is refused");
    const MachineRegistry& registry = DefaultMachineRegistry();
    Check(registry.Create(MachineId("SEGA_MEGADRIVE"), VariantId("MEGADRIVE_68K"), MachineResources{}) != nullptr, "the variant MEGADRIVE_68K is registered (without resources: not playable)");
}

void MegaDrive68kSyntheticDriverRunsAndRequestsReachTheChips() {
    std::printf("MegaDrive68kSyntheticDriverRunsAndRequestsReachTheChips\n");
    SyntheticCartridge cartridge;
    MegaDrive68kMachine machine(cartridge.bytes, SyntheticConfig());
    machine.Initialize();
    Check(machine.Z80RamForTest(0x1000) == 0x55, "the game's initialization loaded the Z80's program and the Z80 ran it");
    Check(machine.RamForTest(0x0100) == 3, "the sound routine ran once for each of the warm-up frames (the frame counter in the game's RAM)");
    Check(machine.CpuForTest().Snapshot().halted, "the 68000 waits for the next interrupt after the warm-up");
    Check(!machine.Z80BusRequestedForTest(), "the Z80 has its bus back");

    std::vector<Event> chip;
    std::vector<std::pair<std::uint64_t, bool>> bus;
    machine.SetChipWriteObserverForTest([&](std::uint64_t ticks, char cpu, char kind, std::uint32_t port, std::uint8_t value) {
        chip.push_back({ticks, std::string(1, cpu) + kind + std::to_string(port), std::to_string(value)});
    });
    machine.SetBusObserverForTest([&](std::uint64_t ticks, bool requested) { bus.emplace_back(ticks, requested); });
    const std::uint64_t start = machine.TicksForTest();
    MusicCommand command;
    command.Kind = MusicCommandKind::Raw;
    command.Value = 0x77;
    machine.SendCommand(command);
    Check(machine.RamForTest(2) == 0x77, "a request is a byte written into the game's RAM");
    machine.RunToTicksForTest(start + 3 * kTicksPerFrame);
    Check(machine.RamForTest(2) == 0, "the sound routine took the request at its next frame");
    Check(chip.size() == 2 && chip[0].kind == "6Y0" && chip[0].a == "42" && chip[1].kind == "6Y1" && chip[1].a == "128",
          "its YM2612 writes (register 0x2A, data 0x80) came from the 68000 through the Z80's bus");
    Check(bus.size() == 2 && bus[0].second && !bus[1].second && bus[1].first > bus[0].first, "the bus was requested and given back around them");
    Check(machine.RamForTest(0x0100) >= 5, "the routine runs every frame");

    // A switch (here a Stop) puts back the state kept after the warm-up: the game's RAM and the Z80's RAM as they were then.
    machine.SendCommand(MusicCommand{MusicCommandKind::Stop, 0, 0});
    Check(machine.RamForTest(0x0100) == 3 && machine.RamForTest(2) == 0 && machine.Z80RamForTest(0x1000) == 0x55,
          "a switch restores the state of the warm-up (the frame counter is 3 again)");
    // The same command after another one starts from the state kept after the warm-up: the same writes at the same times.
    machine.SendCommand(MusicCommand{MusicCommandKind::Raw, 0x01, 0});
    machine.RunToTicksForTest(machine.TicksForTest() + 2 * kTicksPerFrame);
    chip.clear();
    MegaDrive68kMachine other(cartridge.bytes, SyntheticConfig());
    other.Initialize();
    other.SendCommand(command);
    std::vector<Event> a, b;
    other.SetChipWriteObserverForTest([&](std::uint64_t ticks, char cpu, char kind, std::uint32_t port, std::uint8_t value) { b.push_back({ticks, std::string(1, cpu) + kind + std::to_string(port), std::to_string(value)}); });
    machine.SetChipWriteObserverForTest([&](std::uint64_t ticks, char cpu, char kind, std::uint32_t port, std::uint8_t value) { a.push_back({ticks, std::string(1, cpu) + kind + std::to_string(port), std::to_string(value)}); });
    machine.SendCommand(command);       // after other commands: restores the state kept after the warm-up
    const std::uint64_t t1 = machine.TicksForTest(), t2 = other.TicksForTest();
    machine.RunToTicksForTest(t1 + 3 * kTicksPerFrame);
    other.RunToTicksForTest(t2 + 3 * kTicksPerFrame);
    Check(!a.empty() && a.size() == b.size() && a[0].t - t1 == b[0].t - t2 && a[0].kind == b[0].kind,
          "a switch gives the same sound (the same writes at the same times from the request on) as the first request after the start");
}

void MegaDrive68kBusRulesBetweenTheCpus() {
    std::printf("MegaDrive68kBusRulesBetweenTheCpus\n");
    // The Z80 counts in a loop: LD HL,0x1100 ; INC (HL) ; JR back
    SyntheticCartridge cartridge({0x21, 0x00, 0x11, 0x34, 0x18, 0xFD});
    // A frame routine that holds the Z80's bus for a long time (a loop of 100 NOPs between request and release): the Z80
    // must not run during it, and takes up again where it was when the 68000 gives the bus back.
    cartridge.at = 0x2000;
    cartridge.Word(0x33FC); cartridge.Word(0x0100); cartridge.Long(0x00A11100);
    for (int i = 0; i < 100; ++i) cartridge.Word(0x4E71);
    cartridge.Word(0x33FC); cartridge.Word(0x0000); cartridge.Long(0x00A11100);
    cartridge.Word(0x4E75);
    MegaDrive68kMachine machine(cartridge.bytes, SyntheticConfig());
    machine.Initialize();
    std::vector<std::pair<std::uint64_t, bool>> bus;
    std::vector<std::uint8_t> counterAtEvent;
    machine.SetBusObserverForTest([&](std::uint64_t ticks, bool requested) {
        bus.emplace_back(ticks, requested);
        counterAtEvent.push_back(machine.Z80RamForTest(0x1100));
    });
    Check(machine.Z80RamForTest(0x1100) != 0, "the Z80 is counting after the start");
    machine.RunToTicksForTest(machine.TicksForTest() + 2 * kTicksPerFrame);
    Check(bus.size() >= 4 && bus[0].second && !bus[1].second, "the routine requests and releases the bus every frame");
    const std::uint64_t held = bus[1].first - bus[0].first;
    Check(held >= 100ull * 4 * 7, "the bus is held for the length of the routine's 100 NOPs (4 cycles of 7 ticks each)");
    Check(held < 100ull * 4 * 7 + 400, "and for little more than that");
    // The Z80 does not run while the bus is held: its counter is the same when the bus is given back as when it was taken.
    Check(counterAtEvent[1] == counterAtEvent[0], "the Z80 does not run while the 68000 holds its bus");
    Check(machine.Z80RamForTest(0x1100) != counterAtEvent[1], "and goes on counting afterwards");
}

void MegaDrive68kZ80StartsWhenItsResetIsReleased() {
    std::printf("MegaDrive68kZ80StartsWhenItsResetIsReleased\n");
    // The Z80 counts in a loop (it starts at 0 after a reset). The frame routine holds the Z80 in reset for 100 NOPs with
    // the counter cleared, releases the reset and at once asks for the bus: the Z80 has had only the few cycles of the
    // store between the release and the request, so it cannot have counted more than once. A Machine that lets the
    // Z80 make up for the time of the reset would count several times.
    SyntheticCartridge cartridge({0x21, 0x00, 0x11, 0x34, 0x18, 0xFD});
    cartridge.at = 0x2000;
    cartridge.Word(0x33FC); cartridge.Word(0x0100); cartridge.Long(0x00A11100);   // request the bus
    cartridge.Word(0x33FC); cartridge.Word(0x0000); cartridge.Long(0x00A11200);   // assert the reset
    cartridge.Word(0x13FC); cartridge.Word(0x0000); cartridge.Long(0x00A01100);   // clear the counter
    cartridge.Word(0x33FC); cartridge.Word(0x0000); cartridge.Long(0x00A11100);   // give the bus back (the Z80 stays in reset)
    for (int i = 0; i < 100; ++i) cartridge.Word(0x4E71);
    cartridge.Word(0x33FC); cartridge.Word(0x0100); cartridge.Long(0x00A11200);   // release the reset
    cartridge.Word(0x33FC); cartridge.Word(0x0100); cartridge.Long(0x00A11100);   // request the bus at once
    cartridge.Word(0x33FC); cartridge.Word(0x0000); cartridge.Long(0x00A11100);
    cartridge.Word(0x4E75);
    MegaDrive68kMachine machine(cartridge.bytes, SyntheticConfig());
    machine.Initialize();
    std::vector<std::uint8_t> counterAtRequest;
    machine.SetBusObserverForTest([&](std::uint64_t, bool requested) {
        if (requested) counterAtRequest.push_back(machine.Z80RamForTest(0x1100));
    });
    machine.RunToTicksForTest(machine.TicksForTest() + 2 * kTicksPerFrame);
    // requests come in pairs: the one before the reset, then the one after its release
    Check(counterAtRequest.size() >= 4, "the routine ran in at least two frames");
    for (std::size_t i = 1; i < counterAtRequest.size(); i += 2) {
        Check(counterAtRequest[i] <= 1, "right after the reset is released the Z80 has hardly begun (it does not make up for the time it was held)");
    }
    machine.RunToTicksForTest(machine.TicksForTest() + kTicksPerFrame / 4);
    Check(machine.Z80RamForTest(0x1100) > 0, "and it counts afterwards");
}

void MegaDrive68kRealCartridgeMatchesTheOriginalsChipWrites() {
    std::printf("MegaDrive68kRealCartridgeMatchesTheOriginalsChipWrites\n");
    const char* romRoot = GetEnvVar("CHIPESTRA_ROM_ROOT");
    const char* referenceRoot = GetEnvVar("CHIPESTRA_REFERENCE_DIR");
    if (romRoot == nullptr || referenceRoot == nullptr) {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT and CHIPESTRA_REFERENCE_DIR are needed for the real-cartridge comparison\n");
        return;
    }
    // Which recording of the original: "normal" (MAME's usual scheduling of the two CPUs) or "fine" (a build that switches
    // between the CPUs every 68000 cycle). CHIPESTRA_REFERENCE_FLAVOR selects; the default is fine.
    const char* flavorEnv = GetEnvVar("CHIPESTRA_REFERENCE_FLAVOR");
    const std::string flavor = flavorEnv != nullptr ? flavorEnv : "fine";
    const std::filesystem::path logPath = std::filesystem::path(referenceRoot) / "md" / "sonic1" / "real_game" / flavor / "boot_12s.txt";
    if (!std::filesystem::exists(logPath)) {
        std::printf("  SKIP: %s is not there\n", logPath.string().c_str());
        return;
    }
    // The game's definition: the machine and the driver are made as the application does.
    const auto xmlPath = FindDefinitionXml("sonic1.xml");
    Check(xmlPath.has_value(), "sonic1.xml is present (run from the repo root)");
    if (!xmlPath) return;
    std::ifstream xmlFile(*xmlPath, std::ios::binary);
    std::ostringstream xmlText;
    xmlText << xmlFile.rdbuf();
    const XmlParseResult parsed = ParseGameDefinition(xmlText.str());
    Check(parsed.Status == XmlParseStatus::Ok, "sonic1.xml parses");
    if (parsed.Status != XmlParseStatus::Ok) return;
    const GameDefinition& def = parsed.Definition;
    const ResourceResolver resolver{std::filesystem::path(romRoot)};
    ResourceResolutionResult res = resolver.Resolve(def.BuildRequiredResources());
    Check(res.Ok, "the cartridge is found under CHIPESTRA_ROM_ROOT");
    if (!res.Ok) return;
    std::string selectError;
    const std::string driverXml = SelectMegaDriveDriverRegion(ExtractDriverElementXml(xmlText.str()), 0, &selectError);   // the NTSC console
    Check(!driverXml.empty(), "the first region of the list is selected");
    if (driverXml.empty()) return;
    res.Resources.Add(ResolvedResource("driver_xml", std::vector<std::uint8_t>(driverXml.begin(), driverXml.end())));
    MegaDrive68kMachineInputs inputs = AssembleMegaDrive68kMachineInputs(res.Resources);

    const std::vector<Event> reference = LoadMameLog(logPath);
    struct Request { std::uint8_t value; std::uint64_t phase; const char* name; double seconds; double dacToleranceUs; double writeToleranceUs; };
    // `phase`: where in the frame the request is written (ticks): the original's game writes it a little before its
    // vertical-blank interrupt; the phases are those that make our interrupt follow the request as in the recording.
    for (const Request& request : {Request{0xE1, 785117, "SEGA chime", 2.5, 60.0, 60.0}, Request{0x8A, 580230, "title music", 3.5, 1700.0, 17000.0}}) {
        // the reference: the events after the request of this value in the recording
        std::uint64_t t0 = 0;
        for (const Event& e : reference) {
            if (e.kind != "REQ") continue;
            std::istringstream in(e.a);
            std::string address, half, value;
            in >> address >> half >> value;
            if (address == "FFF00B" && half == "lo" && std::stoul(value, nullptr, 16) == request.value) { t0 = e.t; break; }
        }
        Check(t0 != 0, (std::string(request.name) + ": the request is in the recording").c_str());
        if (t0 == 0) continue;
        MegaDrive68kMachine machine(inputs.cartridge, inputs.driver);
        machine.Initialize();
        std::vector<Event> ours;
        std::uint64_t base = 0;
        bool logging = false;
        machine.SetChipWriteObserverForTest([&](std::uint64_t ticks, char cpu, char kind, std::uint32_t port, std::uint8_t value) {
            if (!logging) return;
            ours.push_back({ticks - base, std::string("CHIP") + std::string(1, cpu) + kind, std::to_string(port) + " " + std::to_string(value)});
        });
        std::uint64_t t = machine.TicksForTest();
        t = (t / kTicksPerFrame) * kTicksPerFrame + request.phase;
        if (t < machine.TicksForTest()) t += kTicksPerFrame;
        machine.RunToTicksForTest(t);
        base = t;
        logging = true;
        MusicCommand command;
        command.Kind = MusicCommandKind::Raw;
        command.Value = request.value;
        machine.SendCommand(command);
        const double mclk = 53693175.0;
        machine.RunToTicksForTest(t + static_cast<std::uint64_t>(request.seconds * mclk));

        // the original's chip writes in the window, by writer and chip, with the time from the request
        auto select = [&](const char* kind, std::vector<std::pair<std::uint64_t, std::string>>& out) {
            for (const Event& e : reference) {
                if (e.kind != kind || e.t < t0 || e.t > t0 + static_cast<std::uint64_t>(request.seconds * mclk)) continue;
                std::string a = e.a;
                while (!a.empty() && a[0] == ' ') a.erase(a.begin());
                out.emplace_back(e.t - t0, a);
            }
        };
        struct Stream { const char* original; const char* ourKind; };
        for (const Stream& stream : {Stream{"YM68", "CHIP6Y"}, Stream{"PSG68", "CHIP6P"}, Stream{"YMZ", "CHIPZY"}}) {
            std::vector<std::pair<std::uint64_t, std::string>> theirs, mine;
            select(stream.original, theirs);
            for (const Event& e : ours) {
                if (e.kind != stream.ourKind) continue;
                std::string a = e.a;
                if (std::string(stream.original) == "PSG68") a = a.substr(a.find(' ') + 1);     // the PSG has no port
                mine.emplace_back(e.t, a);
            }
            const std::string label = std::string(request.name) + " " + stream.original;
            Check(theirs.size() == mine.size(), (label + ": the same number of writes (" + std::to_string(theirs.size()) + " in the recording, " + std::to_string(mine.size()) + " here)").c_str());
            const std::size_t n = std::min(theirs.size(), mine.size());
            std::size_t different = 0;
            for (std::size_t i = 0; i < n; ++i) if (theirs[i].second != mine[i].second) ++different;
            Check(different == 0, (label + ": the same values in the same order").c_str());
            // the times: the difference of each write from its median, in microseconds
            std::vector<std::int64_t> d;
            for (std::size_t i = 0; i < n; ++i) d.push_back(static_cast<std::int64_t>(mine[i].first) - static_cast<std::int64_t>(theirs[i].first));
            if (d.empty()) continue;
            std::vector<std::int64_t> sorted = d;
            std::sort(sorted.begin(), sorted.end());
            const std::int64_t median = sorted[sorted.size() / 2];
            const double tolerance = std::string(stream.original) == "YMZ" ? request.dacToleranceUs : request.writeToleranceUs;
            std::size_t outside = 0;
            for (std::int64_t v : d) if (static_cast<double>(std::llabs(v - median)) / mclk * 1e6 > tolerance) ++outside;
            Check(outside * 1000 <= n, (label + ": 99.9% of the writes are within " + std::to_string(static_cast<int>(tolerance)) + " us of the median offset").c_str());
        }
    }
}
