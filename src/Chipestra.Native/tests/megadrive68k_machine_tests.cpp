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
    config.frames = {0x2000};
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
    Check(config.init.size() == 2 && config.init[0] == 0x134C && config.init[1] == 0x200 && config.frames.size() == 1 && config.frames[0] == 0x71B4C && config.requestAddress == 0xFFF00B &&
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

void MegaDrive68kZ80ResetCanBeReleasedAtTheStart() {
    std::printf("MegaDrive68kZ80ResetCanBeReleasedAtTheStart\n");
    MegaDrive68kDriverConfig config;
    std::string error;
    Check(ParseMegaDrive68kDriverConfig(DriverXml("m68k_frame=\"0x2000\" request=\"0xFFF00B\" z80_reset=\"released\" warmup_frames=\"3\""), config, &error) && config.z80ResetReleased,
          "z80_reset=\"released\" is read");
    Check(!ParseMegaDrive68kDriverConfig(DriverXml("m68k_frame=\"0x2000\" request=\"0xFFF00B\" z80_reset=\"held\" warmup_frames=\"3\""), config, &error) && error.find("z80_reset") != std::string::npos,
          "any other value is refused");
    SyntheticCartridge cartridge;
    MegaDrive68kDriverConfig held = SyntheticConfig();
    held.init.clear();         // (nothing in the cartridge releases the Z80 reset)
    MegaDrive68kMachine heldMachine(cartridge.bytes, held);
    heldMachine.Initialize();
    MegaDrive68kDriverConfig released = SyntheticConfig();
    released.init.clear();
    released.z80ResetReleased = true;
    MegaDrive68kMachine releasedMachine(cartridge.bytes, released);
    releasedMachine.Initialize();
    Check(heldMachine.Z80ForTest().Snapshot().pc == 0, "by default the Z80 stays at its reset address until the game releases the reset");
    Check(releasedMachine.Z80ForTest().Snapshot().pc != 0, "released at the start, the Z80 runs without the game");
}

void MegaDrive68kRequestCanNameAByteOfTheGamesRam() {
    std::printf("MegaDrive68kRequestCanNameAByteOfTheGamesRam\n");
    MegaDrive68kDriverConfig config;
    std::string error;
    Check(ParseMegaDrive68kDriverConfig(DriverXml("m68k_init=\"0x1000\" m68k_frame=\"0x2000\" requests=\"0x2100 0xFFF012\" warmup_frames=\"3\""), config, &error) &&
              config.requestRoutines.size() == 2 && config.requestRoutines[1] == 0xFFF012,
          "an entry of requests may be a byte of the game's RAM");
    Check(!ParseMegaDrive68kDriverConfig(DriverXml("m68k_frame=\"0x2000\" requests=\"0xA00000\" warmup_frames=\"3\""), config, &error) && error.find("requests") != std::string::npos,
          "an address that is neither in the cartridge nor in the RAM is refused");

    SyntheticCartridge cartridge;
    cartridge.at = 0x2100;
    cartridge.Word(0x13C0); cartridge.Long(0x00FF0200);      // MOVE.B D0,$FF0200
    cartridge.Word(0x4E75);
    MegaDrive68kDriverConfig game = SyntheticConfig();
    game.requestRoutines = {0x2100, 0xFF0210};
    MegaDrive68kMachine machine(cartridge.bytes, game);
    machine.Initialize();
    MusicCommand command;
    command.Kind = MusicCommandKind::Raw;
    command.Value = 0x2B;
    command.Auxiliary = 2 << 4;       // the byte of the RAM
    machine.SendCommand(command);
    Check(machine.RamForTest(0x0210) == 0x2B, "the number is written into the byte, as the game's main program does");
    machine.RunToTicksForTest(machine.TicksForTest() + kTicksPerFrame / 8);
    Check(machine.RamForTest(0x0200) == 0, "no routine was called for it");
}

void MegaDrive68kCallRequestsReachTheGamesRoutines() {
    std::printf("MegaDrive68kCallRequestsReachTheGamesRoutines\n");
    MegaDrive68kDriverConfig config;
    std::string error;
    Check(ParseMegaDrive68kDriverConfig(DriverXml("m68k_init=\"0x1000\" m68k_frame=\"0x2000 0x2020\" requests=\"0x2100 0x2110\" warmup_frames=\"3\""), config, &error),
          "requests and several frame routines parse");
    Check(config.requestRoutines.size() == 2 && config.requestRoutines[0] == 0x2100 && config.requestRoutines[1] == 0x2110 && config.frames.size() == 2 && config.frames[1] == 0x2020,
          "the addresses are read in the order written");
    Check(!ParseMegaDrive68kDriverConfig(DriverXml("m68k_frame=\"0x2000\" requests=\"0x2101\" warmup_frames=\"3\""), config, &error) && error.find("requests") != std::string::npos,
          "a request routine at an odd address is refused");
    Check(!ParseMegaDrive68kDriverConfig(DriverXml("m68k_frame=\"0x2000\" requests=\"\" warmup_frames=\"3\""), config, &error) && error.find("requests") != std::string::npos,
          "an empty list of request routines is refused");
    Check(!ParseMegaDrive68kDriverConfig(DriverXml("m68k_frame=\"0x2000 0x2001\" request=\"0xFFF00B\" warmup_frames=\"3\""), config, &error) && error.find("m68k_frame") != std::string::npos,
          "a frame routine at an odd address is refused");

    // Two routines of the game: the first stores D0 (a byte) at 0xFF0200, the second stores D1 (a word) at 0xFF0202;
    // a second frame routine counts the frames at 0xFF0204.
    SyntheticCartridge cartridge;
    cartridge.at = 0x2100;
    cartridge.Word(0x13C0); cartridge.Long(0x00FF0200);      // MOVE.B D0,$FF0200
    cartridge.Word(0x4E75);                                  // RTS
    cartridge.at = 0x2110;
    cartridge.Word(0x33C1); cartridge.Long(0x00FF0202);      // MOVE.W D1,$FF0202
    cartridge.Word(0x4E75);
    cartridge.at = 0x2020;
    cartridge.Word(0x5239); cartridge.Long(0x00FF0204);      // ADDQ.B #1,$FF0204
    cartridge.Word(0x4E75);
    MegaDrive68kDriverConfig game = SyntheticConfig();
    game.frames = {0x2000, 0x2020};
    game.requestRoutines = {0x2100, 0x2110};
    MegaDrive68kMachine machine(cartridge.bytes, game);
    machine.Initialize();
    Check(machine.RamForTest(0x0204) == 3 && machine.RamForTest(0x0100) == 3, "both frame routines run at every frame, in order");

    MusicCommand command;
    command.Kind = MusicCommandKind::Raw;
    command.Value = 0x5A;
    command.Auxiliary = 1 << 4;       // the first routine
    machine.SendCommand(command);
    machine.RunToTicksForTest(machine.TicksForTest() + kTicksPerFrame / 8);
    Check(machine.RamForTest(0x0200) == 0x5A, "the first routine was called with the number in D0, within a fraction of a frame (as the game's main program would)");
    command.Value = 0x6C;
    command.Auxiliary = 2 << 4;       // the second routine
    machine.SendCommand(command);      // after another request: starts again from the saved state
    machine.RunToTicksForTest(machine.TicksForTest() + kTicksPerFrame / 8);
    Check(machine.RamForTest(0x0200) == 0 && machine.RamForTest(0x0202) == 0 && machine.RamForTest(0x0203) == 0x6C, "the second routine was called with the number in D1 and the first request was undone");

    // Requests made one after the other (for the tests) are called in this order, each when the 68000 is idle again.
    machine.CallRequestForTest(1, 0x11);
    machine.CallRequestForTest(2, 0x22);
    machine.RunToTicksForTest(machine.TicksForTest() + kTicksPerFrame / 2);
    Check(machine.RamForTest(0x0200) == 0x11 && machine.RamForTest(0x0203) == 0x22, "queued requests are all called");
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

namespace {

struct Stimulus { std::uint64_t t; std::string kind; int value; };

// Loads the stimuli file of a recording: "<ticks> <SONG|SFX|PSG> <number>" per line, the calls the original's game made to
// the routines of the sound driver (see <driver requests>, in this order).
std::vector<Stimulus> LoadStimuli(const std::filesystem::path& path) {
    std::vector<Stimulus> list;
    std::ifstream in(path);
    Stimulus s;
    while (in >> s.t >> s.kind >> s.value) list.push_back(s);
    return list;
}


using TimedValues = std::vector<std::pair<std::uint64_t, std::string>>;

// The Z80 plays a sampled sound by writing the YM2612's DAC (register 0x2A) at the pace its program sets; the 68000 starts
// and restarts it by writing the number to the Z80's RAM. Where a restart comes within a frame differs a little from the
// original (the original's game does its own work before it calls the driver), so the DAC writes of a whole recording
// cannot be compared one for one. They are compared between two restarts: the sound must begin with the same values and
// run for about the same time.
std::vector<std::string> DacValues(const TimedValues& all, std::uint64_t from, std::uint64_t to) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i + 1 < all.size(); ++i) {
        if (all[i].first < from || all[i].first >= to) continue;
        if (all[i].second == "0 42" && all[i + 1].second.rfind("1 ", 0) == 0) out.push_back(all[i + 1].second);
    }
    // the sound begins at the Z80's first write of the DAC's rest value; what comes before it is the end of the sound it replaced
    std::size_t first = 0;
    for (std::size_t i = 0; i < out.size() && i < 3; ++i) if (out[i] == "1 128") first = i;
    out.erase(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(first));
    // a Z80 program that writes the rest value while it has nothing to play (some do) leaves runs of it around the sound
    while (!out.empty() && out.front() == "1 128") out.erase(out.begin());
    while (!out.empty() && out.back() == "1 128") out.pop_back();
    return out;
}

// For a game whose sounds start at times that depend on a race between the 68000's request and the Z80's polling: the sounds that start
// in both (within `windowTicks` of each other) are compared as in CompareDacBetweenRestarts; at most 5% of the starts may have no partner.
void CompareDacPairedByStart(const std::string& label, const TimedValues& startsTheirs, const TimedValues& startsMine, const TimedValues& dacTheirs,
                             const TimedValues& dacMine, std::uint64_t windowTicks, std::size_t compareFirst, std::size_t lengthTolerance) {
    std::size_t unpaired = 0, badStart = 0, badLength = 0, paired = 0, j = 0;
    for (std::size_t i = 0; i < startsTheirs.size(); ++i) {
        while (j < startsMine.size() && startsMine[j].first + windowTicks < startsTheirs[i].first) ++j;
        if (j >= startsMine.size() || startsMine[j].first > startsTheirs[i].first + windowTicks) { ++unpaired; continue; }
        // (a start that has another start of the other list nearby on both sides cannot be told apart: both are compared only when alone)
        ++paired;
        const std::uint64_t endTheirs = i + 1 < startsTheirs.size() ? startsTheirs[i + 1].first : ~0ull;
        const std::uint64_t endMine = j + 1 < startsMine.size() ? startsMine[j + 1].first : ~0ull;
        const std::vector<std::string> a = DacValues(dacTheirs, startsTheirs[i].first, endTheirs);
        const std::vector<std::string> b = DacValues(dacMine, startsMine[j].first, endMine);
        const std::size_t from = 10;
        const std::size_t count = std::min<std::size_t>(compareFirst - from, a.size() > from ? a.size() - from : 0);
        bool same = a.size() <= from + 20 || b.size() <= from + 20;
        for (std::size_t shift = 0; !same && shift + count <= b.size() && shift <= 40; ++shift) {
            bool equal = true;
            for (std::size_t k = 0; k < count; ++k) if (a[from + k] != b[shift + k]) { equal = false; break; }
            same = equal;
        }
        if (!same) ++badStart;
        const std::size_t diff = a.size() > b.size() ? a.size() - b.size() : b.size() - a.size();
        if (startsTheirs.size() > i + 1 && startsMine.size() > j + 1 && std::llabs(static_cast<std::int64_t>(startsTheirs[i + 1].first) - static_cast<std::int64_t>(startsMine[j + 1].first)) <= static_cast<std::int64_t>(windowTicks) &&
            diff > lengthTolerance + a.size() / 50)
            ++badLength;
        ++j;
    }
    Check(unpaired * 20 <= startsTheirs.size(), (label + ": " + std::to_string(unpaired) + " of " + std::to_string(startsTheirs.size()) + " sampled sounds have no partner (5% allowed)").c_str());
    Check(paired > 0 && badStart == 0, (label + ": every paired sampled sound has the same " + std::to_string(compareFirst - 10) + " values from its 11th on").c_str());
    Check(badLength == 0, (label + ": every paired sampled sound runs for the same number of writes (within " + std::to_string(lengthTolerance) + " and 2%)").c_str());
}

void CompareDacBetweenRestarts(const std::string& label, const TimedValues& restartsTheirs, const TimedValues& restartsMine,
                               const TimedValues& dacTheirs, const TimedValues& dacMine, std::size_t compareFirst, std::size_t lengthTolerance,
                               std::size_t badStartsAllowedPerThousand = 0) {
    auto values = [](const TimedValues& all, std::uint64_t from, std::uint64_t to) { return DacValues(all, from, to); };
    Check(restartsTheirs.size() == restartsMine.size(), (label + ": the same number of restarts of the sampled sound").c_str());
    const std::size_t n = std::min(restartsTheirs.size(), restartsMine.size());
    std::size_t badStart = 0, badLength = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint64_t endTheirs = i + 1 < restartsTheirs.size() ? restartsTheirs[i + 1].first : ~0ull;
        const std::uint64_t endMine = i + 1 < restartsMine.size() ? restartsMine[i + 1].first : ~0ull;
        const std::vector<std::string> a = values(dacTheirs, restartsTheirs[i].first, endTheirs);
        const std::vector<std::string> b = values(dacMine, restartsMine[i].first, endMine);
        // The first values are those of the sound it replaced (how many depends on when the Z80 looks at the request), so the
        // values from the 10th on must be found in ours a few values earlier or later.
        const std::size_t from = 10;
        const std::size_t count = std::min<std::size_t>(compareFirst - from, a.size() > from ? a.size() - from : 0);
        bool same = a.size() <= from + 20 || b.size() <= from + 20;   // a very short sound: only its length is compared
        for (std::size_t shift = 0; !same && shift + count <= b.size() && shift <= 40; ++shift) {
            bool equal = true;
            for (std::size_t j = 0; j < count; ++j) if (a[from + j] != b[shift + j]) { equal = false; break; }
            same = equal;
        }
        if (!same && GetEnvVar("CHIPESTRA_REPLAY_DEBUG") != nullptr) { std::printf("  dac segment %zu: %zu / %zu values; theirs:", i, a.size(), b.size()); for (std::size_t j = 0; j < 14 && j < a.size(); ++j) std::printf(" %s", a[j].c_str() + 2); std::printf(" ours:"); for (std::size_t j = 0; j < 14 && j < b.size(); ++j) std::printf(" %s", b[j].c_str() + 2); std::printf("\n"); }
        if (!same) ++badStart;
        // The Z80 of the original is held a little longer by the 68000 than here (about one write in 180), so a sound that is
        // cut by the next request ends a little earlier there.
        const std::size_t diff = a.size() > b.size() ? a.size() - b.size() : b.size() - a.size();
        if (i + 1 < n && diff > lengthTolerance + a.size() / 50) ++badLength;
    }
    Check(badStart * 1000 <= n * badStartsAllowedPerThousand,
          (label + ": the sampled sounds have the same " + std::to_string(compareFirst - 10) + " values from the 11th on" + (badStartsAllowedPerThousand != 0 ? " (all but " + std::to_string(badStartsAllowedPerThousand / 10) + "%: sounds that begin while another one is mixed depend on the Z80's phase)" : std::string())).c_str());
    Check(badLength == 0, (label + ": every sampled sound runs for the same number of writes (within " + std::to_string(lengthTolerance) + " and 2%)").c_str());
}

// A game whose recording is replayed: its definition, the recording and the list of the calls the original's game made (one
// "<ticks> <kind> <number>" per line; kind POKE is a write into the driver's RAM, number = address * 256 + value), and what a
// kind of call is here: the entry of <driver requests> that takes the number (kinds); the kind INIT is a restart of the
// driver by the routines `initRoutines` (called one after the other).
struct ReplayGame {
    const char* xml;
    const char* log;
    const char* stimuli;
    std::vector<std::pair<std::string, std::uint32_t>> kinds;
    std::vector<std::uint32_t> initRoutines;
    // The recording has the times at which the original's vertical-blank handler began (kind VBL): the 68000 gets its
    // vertical-blank interrupts at these times (a game that keeps interrupts off while it loads misses some).
    bool vblankSchedule = false;
    // The parts of the Z80's RAM (besides 0x1FF0-0x1FFF) where the 68000 leaves the Z80 program its commands: the writes there are compared.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> z80TriggerRanges;
    // Routines of the game, called by kinds of calls that are not requests of the definition: after the restarts of the driver,
    // the n-th of them is the routine number (definition's requests + restart routines + n).
    std::vector<std::uint32_t> extraRoutines;
    // The sampled sound is played by a Z80 program that is not restarted by a request that can be told apart: its writes to the
    // DAC are compared like the other chip writes (the same values in the same order).
    bool exactDac = false;
    // The sampled sounds are told apart by the Z80 program's own writes that switch the DAC on (register 0x2B), not by the
    // 68000's writes into the Z80's RAM: each such write begins a sound, compared as in the other games.
    bool dacRestartsFromStream = false;
    // The driver's sound part of the vertical-blank interrupt is skipped when the horizontal interrupt's work is still going on
    // (a flag the game's handlers set), so whether a frame is skipped depends on the lengths of the long frames, which differ a
    // little from the original's (the Z80's phase builds up from every time it is stopped): the writes are compared by
    // lining the two lists up again after each difference (a few writes that one has and the other has not).
    bool resyncCompare = false;
};

struct AlignedWrites {
    std::vector<std::pair<std::size_t, std::size_t>> pairs;   // (index in theirs, index in mine) of the writes found in both
    std::size_t differences = 0;                              // places where the lists were lined up again
};

// Walks both lists; at a difference it looks for the nearest place (up to `maxSkip` writes on either side) where the next
// `confirm` writes agree again.
AlignedWrites AlignWithResync(const std::vector<std::pair<std::uint64_t, std::string>>& a, const std::vector<std::pair<std::uint64_t, std::string>>& b) {
    constexpr std::size_t kMaxSkip = 200, kConfirm = 6;
    AlignedWrites result;
    std::size_t i = 0, j = 0;
    auto agree = [&](std::size_t x, std::size_t y) {
        if (x + kConfirm > a.size() || y + kConfirm > b.size()) return false;
        for (std::size_t k = 0; k < kConfirm; ++k) if (a[x + k].second != b[y + k].second) return false;
        return true;
    };
    while (i < a.size() && j < b.size()) {
        if (a[i].second == b[j].second) { result.pairs.emplace_back(i, j); ++i; ++j; continue; }
        ++result.differences;
        bool found = false;
        for (std::size_t d = 1; d <= kMaxSkip * 2 && !found; ++d) {
            for (std::size_t di = 0; di <= d && !found; ++di) {
                const std::size_t dj = d - di;
                if (di > kMaxSkip || dj > kMaxSkip) continue;
                if (agree(i + di, j + dj)) { i += di; j += dj; found = true; }
            }
        }
        if (!found) { ++i; ++j; }
    }
    return result;
}

// Plays the calls the original's game made, from `fromSeconds` to `toSeconds` of its recording, at the same times and
// compares the writes to the YM2612 and the PSG with the recording: the same number, the same values in the same order,
// and the times within the tolerance of the median offset.
void CompareReplayWithRecording(const ReplayGame& game, double fromSeconds, double toSeconds, double dacToleranceUs, double writeToleranceUs) {
    const char* xmlName = game.xml;
    const char* logRelativePath = game.log;
    const char* stimuliRelativePath = game.stimuli;
    const char* romRoot = GetEnvVar("CHIPESTRA_ROM_ROOT");
    const char* referenceRoot = GetEnvVar("CHIPESTRA_REFERENCE_DIR");
    if (romRoot == nullptr || referenceRoot == nullptr) {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT and CHIPESTRA_REFERENCE_DIR are needed for the real-cartridge comparison\n");
        return;
    }
    const char* flavorEnv = GetEnvVar("CHIPESTRA_REFERENCE_FLAVOR");
    const std::string flavor = flavorEnv != nullptr ? flavorEnv : "fine";
    auto resolve = [&](std::string relative) {
        const std::size_t at = relative.find("<flavor>");
        if (at != std::string::npos) relative.replace(at, 8, flavor);
        return std::filesystem::path(referenceRoot) / relative;
    };
    const std::filesystem::path logPath = resolve(logRelativePath);
    const std::filesystem::path stimuliPath = resolve(stimuliRelativePath);
    if (!std::filesystem::exists(logPath) || !std::filesystem::exists(stimuliPath)) {
        std::printf("  SKIP: %s or %s is not there\n", logPath.string().c_str(), stimuliPath.string().c_str());
        return;
    }
    const auto xmlPath = FindDefinitionXml(xmlName);
    Check(xmlPath.has_value(), (std::string(xmlName) + " is present (run from the repo root)").c_str());
    if (!xmlPath) return;
    std::ifstream xmlFile(*xmlPath, std::ios::binary);
    std::ostringstream xmlText;
    xmlText << xmlFile.rdbuf();
    const XmlParseResult parsed = ParseGameDefinition(xmlText.str());
    Check(parsed.Status == XmlParseStatus::Ok, (std::string(xmlName) + " parses").c_str());
    if (parsed.Status != XmlParseStatus::Ok) return;
    const ResourceResolver resolver{std::filesystem::path(romRoot)};
    ResourceResolutionResult res = resolver.Resolve(parsed.Definition.BuildRequiredResources());
    Check(res.Ok, "the cartridge is found under CHIPESTRA_ROM_ROOT");
    if (!res.Ok) return;
    std::string selectError;
    const std::string driverXml = SelectMegaDriveDriverRegion(ExtractDriverElementXml(xmlText.str()), 0, &selectError);
    Check(!driverXml.empty(), "the first region of the list is selected");
    if (driverXml.empty()) return;
    res.Resources.Add(ResolvedResource("driver_xml", std::vector<std::uint8_t>(driverXml.begin(), driverXml.end())));
    MegaDrive68kMachineInputs inputs = AssembleMegaDrive68kMachineInputs(res.Resources);

    const double mclk = 53693175.0;
    const std::vector<Event> reference = LoadMameLog(logPath);
    // the calls in the window
    std::vector<Stimulus> calls;
    std::vector<std::uint64_t> vblanks;
    for (const Stimulus& s : LoadStimuli(stimuliPath)) {
        if (s.kind == "VBL") { vblanks.push_back(s.t); continue; }
        if (s.t < static_cast<std::uint64_t>(fromSeconds * mclk) || s.t >= static_cast<std::uint64_t>(toSeconds * mclk)) continue;
        bool known = s.kind == "POKE" || s.kind == "INIT";
        for (const auto& kind : game.kinds) known = known || kind.first == s.kind;
        if (known) calls.push_back(s);
    }
    Check(!calls.empty(), "the recording has calls in the window");
    if (calls.empty()) return;
    const std::uint64_t t0 = calls.front().t;
    const std::uint64_t window = static_cast<std::uint64_t>(toSeconds * mclk) - t0;

    // The calls the game makes to restart the driver are routines of the game that are not requests: they are added here.
    const std::size_t routinesInXml = inputs.driver.requestRoutines.size();
    for (const std::uint32_t routine : game.initRoutines) inputs.driver.requestRoutines.push_back(routine);
    for (const std::uint32_t routine : game.extraRoutines) inputs.driver.requestRoutines.push_back(routine);
    Check(routinesInXml + game.initRoutines.size() + game.extraRoutines.size() <= 7, "the game has no more than seven routines to call");
    MegaDrive68kMachine machine(inputs.cartridge, inputs.driver);
    machine.Initialize();
    std::vector<Event> ours;
    std::uint64_t base = 0;
    bool logging = false;
    machine.SetChipWriteObserverForTest([&](std::uint64_t ticks, char cpu, char kind, std::uint32_t port, std::uint8_t value) {
        if (!logging) return;
        ours.push_back({ticks - base, std::string("CHIP") + std::string(1, cpu) + kind, std::to_string(port) + " " + std::to_string(value)});
    });
    machine.SetZ80RamWriteObserverForTest([&](std::uint64_t ticks, std::uint32_t offset, std::uint8_t value) {
        bool wanted = offset >= 0x1FF0;
        for (const auto& range : game.z80TriggerRanges) wanted = wanted || (offset >= range.first && offset < range.second);
        if (!logging || !wanted) return;
        char text[32];
        std::snprintf(text, sizeof(text), "%04X %02X", offset, value);
        ours.push_back({ticks - base, "CHIP6W", text});
    });
    std::uint64_t start = (machine.TicksForTest() / kTicksPerFrame) * kTicksPerFrame + t0 % kTicksPerFrame;
    if (start < machine.TicksForTest()) start += kTicksPerFrame;
    base = start;
    if (game.vblankSchedule) {
        // (the handler's first read of the game's RAM comes about 1500 ticks after the interrupt is taken)
        std::vector<std::uint64_t> schedule;
        for (const std::uint64_t v : vblanks) {
            if (v < t0 + 1500 || v > t0 + window) continue;
            schedule.push_back(start + (v - 1500 - t0));
        }
        machine.SetVblankScheduleForTest(schedule);
        if (GetEnvVar("CHIPESTRA_REPLAY_DEBUG") != nullptr) std::printf("  schedule: %zu interrupts, first %llu start %llu t0 %llu window %llu\n", schedule.size(), schedule.empty() ? 0ull : static_cast<unsigned long long>(schedule.front()), static_cast<unsigned long long>(start), static_cast<unsigned long long>(t0), static_cast<unsigned long long>(window));
    }
    machine.RunToTicksForTest(start);
    logging = true;
    for (const Stimulus& call : calls) {
        std::uint64_t at = call.t;
        if (call.kind == "POKE") {
            // The original's vertical-blank handler first does its video work (up to about 3 ms) and then calls the driver; a
            // write the game's main program makes in that time is seen by the driver in the same frame. Ours calls the driver
            // at the start of the interrupt, so such a write is made just before it.
            if (game.vblankSchedule) {
                // the handler's start nearest before the write
                std::uint64_t entry = 0;
                for (const std::uint64_t v : vblanks) if (v <= call.t) entry = v;
                if (entry != 0 && call.t - entry <= 0) at = entry - 1500 - 4000;
            } else {
                const std::uint64_t vblank = (224 * 228 + 10) * 15;
                const std::uint64_t phase = call.t % kTicksPerFrame;
                if (phase > vblank && phase <= vblank + 160000) at = call.t - (phase - vblank) - 4000;
            }
        }
        machine.RunToTicksForTest(start + (at - t0));
        if (call.kind == "POKE") {
            // a write of the original's game into the driver's RAM (its fade-out speed): the number is address * 256 + value
            machine.PokeRamForTest(0xFF0000u + static_cast<std::uint32_t>(call.value >> 8), static_cast<std::uint8_t>(call.value & 0xFF));
        } else if (call.kind == "INIT") {
            // the game's restart of the sound driver: its initialization routines one after the other
            for (std::size_t i = 0; i < game.initRoutines.size(); ++i) machine.CallRequestForTest(static_cast<std::uint32_t>(routinesInXml + i + 1), 0);
        } else {
            // a kind the game's definition does not name (for instance the loading of the Z80's program) is not a request
            for (const auto& kind : game.kinds) {
                if (kind.first == call.kind) machine.CallRequestForTest(kind.second, static_cast<std::uint8_t>(call.value));
            }
        }
    }
    machine.RunToTicksForTest(start + window);

    struct Stream { const char* original; const char* ourKind; };
    TimedValues restartsTheirs, restartsMine;
    for (const Stream& stream : {Stream{"YM68", "CHIP6Y"}, Stream{"PSG68", "CHIP6P"}, Stream{"ZW", "CHIP6W"}, Stream{"YMZ", "CHIPZY"}}) {
        std::vector<std::pair<std::uint64_t, std::string>> theirs, mine;
        for (const Event& e : reference) {
            if (e.kind != stream.original || e.t < t0 || e.t > t0 + window) continue;
            std::string a = e.a;
            while (!a.empty() && a[0] == ' ') a.erase(a.begin());
            if (const std::size_t pc = a.find(" PC "); pc != std::string::npos) a.erase(pc);      // (the recording names the instruction that wrote)
            theirs.emplace_back(e.t - t0, a);
        }
        for (const Event& e : ours) {
            if (e.kind != stream.ourKind) continue;
            std::string a = e.a;
            if (std::string(stream.original) == "PSG68") a = a.substr(a.find(' ') + 1);     // the PSG has no port
            mine.emplace_back(e.t, a);
        }
        const std::string label = std::string("replay ") + stream.original;
        if (std::string(stream.original) == "PSG68") {
            // The PSG part of the driver runs its first frame at the first vertical-blank interrupt after the start-up; in the
            // original's start-up that is later than in ours, so its first writes are left out (the first 37 ms of the window).
            auto trim = [](TimedValues& list) {
                list.erase(std::remove_if(list.begin(), list.end(), [](const auto& e) { return e.first < 2000000; }), list.end());
            };
            trim(theirs);
            trim(mine);
        }
        if (const char* dump = GetEnvVar("CHIPESTRA_DUMP_DIR"); dump != nullptr) {
            std::ofstream a(std::filesystem::path(dump) / (label + "_" + std::to_string(static_cast<int>(fromSeconds)) + "_theirs.txt"));
            std::ofstream b(std::filesystem::path(dump) / (label + "_" + std::to_string(static_cast<int>(fromSeconds)) + "_mine.txt"));
            for (const auto& e : theirs) a << e.first << ' ' << e.second << '\n';
            for (const auto& e : mine) b << e.first << ' ' << e.second << '\n';
        }
        if (std::string(stream.original) == "ZW") { restartsTheirs = theirs; restartsMine = mine; }
        if (std::string(stream.original) == "YMZ" && game.dacRestartsFromStream) {
            restartsTheirs.clear();
            restartsMine.clear();
            for (const auto& e : theirs) if (e.second == "0 43") restartsTheirs.push_back(e);
            for (const auto& e : mine) if (e.second == "0 43") restartsMine.push_back(e);
            CompareDacPairedByStart(label, restartsTheirs, restartsMine, theirs, mine, 1500000, 150, 12);
            continue;
        }
        if (std::string(stream.original) == "YMZ" && !game.exactDac) {
            CompareDacBetweenRestarts(label, restartsTheirs, restartsMine, theirs, mine, 150, 12, game.resyncCompare ? 20 : 0);
            continue;
        }
        std::vector<std::int64_t> d;
        if (game.resyncCompare) {
            const std::size_t larger = std::max(theirs.size(), mine.size());
            Check(theirs.size() + larger / 200 >= mine.size() && mine.size() + larger / 200 >= theirs.size(), (label + ": the number of writes is within 0.5% (" + std::to_string(theirs.size()) + " in the recording, " + std::to_string(mine.size()) + " here)").c_str());
            const AlignedWrites aligned = AlignWithResync(theirs, mine);
            Check(aligned.pairs.size() * 1000 >= larger * 995, (label + ": 99.5% of the writes have the same value in the same order (" + std::to_string(aligned.pairs.size()) + " of " + std::to_string(larger) + ", " + std::to_string(aligned.differences) + " places lined up again)").c_str());
            for (const auto& pair : aligned.pairs) d.push_back(static_cast<std::int64_t>(mine[pair.second].first) - static_cast<std::int64_t>(theirs[pair.first].first));
        } else {
            Check(theirs.size() == mine.size(), (label + ": the same number of writes (" + std::to_string(theirs.size()) + " in the recording, " + std::to_string(mine.size()) + " here)").c_str());
            const std::size_t n = std::min(theirs.size(), mine.size());
            std::size_t different = 0;
            for (std::size_t i = 0; i < n; ++i) if (theirs[i].second != mine[i].second) ++different;
            Check(different == 0, (label + ": the same values in the same order").c_str());
            for (std::size_t i = 0; i < n; ++i) d.push_back(static_cast<std::int64_t>(mine[i].first) - static_cast<std::int64_t>(theirs[i].first));
        }
        const std::size_t n = d.size();
        if (d.empty()) continue;
        std::vector<std::int64_t> sorted = d;
        std::sort(sorted.begin(), sorted.end());
        const std::int64_t median = sorted[sorted.size() / 2];
        const double tolerance = std::string(stream.original) == "YMZ" ? dacToleranceUs : writeToleranceUs;
        std::size_t outside = 0;
        for (std::int64_t v : d) if (static_cast<double>(std::llabs(v - median)) / mclk * 1e6 > tolerance) ++outside;
        Check(outside * (game.resyncCompare ? 100 : 1000) <= n, (label + (game.resyncCompare ? ": 99% of the writes are within " : ": 99.9% of the writes are within ") + std::to_string(static_cast<int>(tolerance)) + " us of the median offset").c_str());
    }
}

}  // namespace

void MegaDrive68kBattleManiaReplayMatchesTheOriginalsChipWrites() {
    std::printf("MegaDrive68kBattleManiaReplayMatchesTheOriginalsChipWrites\n");
    const ReplayGame game{"battlemania.xml", "md/battlemania/real_game/<flavor>/boot_70s.txt", "md/battlemania/real_game/<flavor>/stimuli_70s.txt",
                          {{"SONG", 1}, {"SFX", 2}, {"PSG", 3}}, {0x6C000, 0x6CBC2}};
    CompareReplayWithRecording(game, 1.0, 46.0, 17000.0, 17000.0);
    // after the game restarted the driver (its first call in this window) the sounds of the rest of the recording
    CompareReplayWithRecording(game, 46.5, 69.0, 17000.0, 17000.0);
}

void MegaDrive68kBattleMania2ReplayMatchesTheOriginalsChipWrites() {
    std::printf("MegaDrive68kBattleMania2ReplayMatchesTheOriginalsChipWrites\n");
    const ReplayGame game{"battlemania2.xml", "md/battlemania2/real_game/<flavor>/boot_90s.txt", "md/battlemania2/real_game/<flavor>/stimuli_90s.txt",
                          {{"SONG", 1}, {"SFX", 2}}, {}, true};
    // The window ends where the game starts over (it clears its RAM at 85.54 s and starts a song). The fade-out that begins at 84.4 s
    // is the game's own request: it writes the fade speed into the driver's variable at 84.36 s (a POKE of the recording), and the
    // driver fades by itself from then on.
    CompareReplayWithRecording(game, 1.3, 85.5, 17000.0, 17000.0);
}

void MegaDrive68kGauntlet4ReplayMatchesTheOriginalsChipWrites() {
    std::printf("MegaDrive68kGauntlet4ReplayMatchesTheOriginalsChipWrites\n");
    const ReplayGame game{"gauntlet4.xml", "md/gauntlet4/real_game/<flavor>/play_120s.txt", "md/gauntlet4/real_game/<flavor>/stimuli_120s.txt",
                          {{"SONG", 1}, {"ZCMD", 3}, {"SEQ", 4}}, {}, true, {{0x100, 0x120}}, {0x39E14, 0x39E08}, true};
    CompareReplayWithRecording(game, 15.0, 118.0, 17000.0, 17000.0);
}

void MegaDrive68kPhantasyStar4ReplayMatchesTheOriginalsChipWrites() {
    std::printf("MegaDrive68kPhantasyStar4ReplayMatchesTheOriginalsChipWrites\n");
    const ReplayGame game{"pstar4.xml", "md/pstar4/real_game/<flavor>/play3_150s.txt", "md/pstar4/real_game/<flavor>/stimuli3_150s.txt",
                          {}, {}, true, {{0x1F00, 0x1F10}}, {}, false, true};
    CompareReplayWithRecording(game, 0.4, 147.0, 17000.0, 17000.0);
}

void MegaDrive68kStarCruiserReplayMatchesTheOriginalsChipWrites() {
    std::printf("MegaDrive68kStarCruiserReplayMatchesTheOriginalsChipWrites\n");
    const ReplayGame game{"starcruiser.xml", "md/starcruiser/real_game/<flavor>/play_125s.txt", "md/starcruiser/real_game/<flavor>/stimuli_125s.txt",
                          {{"SONG", 1}, {"SFX", 2}, {"FADE", 4}}, {0x9610}, false, {{0x3B, 0x3D}}, {0x95F8}, false, false, true};
    CompareReplayWithRecording(game, 1.0, 120.0, 17000.0, 17000.0);
}

void MegaDrive68kGamesOwnHandlersHorizontalInterruptsAndPointerRequests() {
    std::printf("MegaDrive68kGamesOwnHandlersHorizontalInterruptsAndPointerRequests\n");
    MegaDrive68kDriverConfig config;
    std::string error;
    Check(ParseMegaDrive68kDriverConfig(DriverXml("m68k_init=\"0x2A00\" vblank_handler=\"game\" hblank_lines=\"53\" m68k_a6=\"0xFF0000\" m68k_ram_words=\"0xFF0110=0x1234 0xFF0112=5\" requests=\"0x2B00\" request_arg=\"pointer\" warmup_frames=\"3\""), config, &error),
          "the settings of a game that keeps its own handlers parse (m68k_frame is not needed then)");
    Check(config.vblankHandlerIsGame && config.hblankLines == 53 && config.a6 == 0xFF0000u && config.requestByPointer && config.ramWords.size() == 2 &&
              config.ramWords[0].first == 0xFF0110u && config.ramWords[0].second == 0x1234 && config.ramWords[1].second == 5,
          "the numbers are read as written");
    Check(!ParseMegaDrive68kDriverConfig(DriverXml("m68k_frame=\"0x2000\" request=\"0xFF0002\" hblank_lines=\"0\" warmup_frames=\"3\""), config, &error) && error.find("hblank_lines") != std::string::npos, "hblank_lines 0 is refused");
    Check(!ParseMegaDrive68kDriverConfig(DriverXml("m68k_frame=\"0x2000\" request=\"0xFF0002\" hblank_lines=\"257\" warmup_frames=\"3\""), config, &error) && error.find("hblank_lines") != std::string::npos, "hblank_lines above 256 is refused");
    Check(!ParseMegaDrive68kDriverConfig(DriverXml("m68k_frame=\"0x2000\" request=\"0xFF0002\" m68k_a6=\"0x1000\" warmup_frames=\"3\""), config, &error) && error.find("m68k_a6") != std::string::npos, "A6 must point into the RAM");
    Check(!ParseMegaDrive68kDriverConfig(DriverXml("m68k_frame=\"0x2000\" request=\"0xFF0002\" m68k_ram_words=\"0xFF0111=1\" warmup_frames=\"3\""), config, &error) && error.find("m68k_ram_words") != std::string::npos, "a word at an odd address is refused");
    Check(!ParseMegaDrive68kDriverConfig(DriverXml("m68k_frame=\"0x2000\" request=\"0xFF0002\" m68k_ram_words=\"0xFF0110=0x12345\" warmup_frames=\"3\""), config, &error) && error.find("m68k_ram_words") != std::string::npos, "a value above 16 bits is refused");
    Check(ParseMegaDrive68kDriverConfig(DriverXml("m68k_frame=\"0x2000\" request=\"0xFF0002\" z80_bus=\"instruction_end\" warmup_frames=\"3\""), config, &error) && config.z80BusAtInstructionEnd, "z80_bus=\"instruction_end\" parses");
    Check(!ParseMegaDrive68kDriverConfig(DriverXml("m68k_frame=\"0x2000\" request=\"0xFF0002\" z80_bus=\"late\" warmup_frames=\"3\""), config, &error) && error.find("z80_bus") != std::string::npos, "an unknown z80_bus is refused");
    Check(!ParseMegaDrive68kDriverConfig(DriverXml("m68k_frame=\"0x2000\" request=\"0xFF0002\" request_arg=\"word\" warmup_frames=\"3\""), config, &error) && error.find("request_arg") != std::string::npos, "an unknown request_arg is refused");
    Check(!ParseMegaDrive68kDriverConfig(DriverXml("vblank_handler=\"own\" request=\"0xFF0002\" warmup_frames=\"3\""), config, &error) && error.find("vblank_handler") != std::string::npos, "an unknown vblank_handler is refused");
    Check(!ParseMegaDrive68kDriverConfig(DriverXml("request=\"0xFF0002\" warmup_frames=\"3\""), config, &error) && error.find("m68k_frame") != std::string::npos, "without vblank_handler=\"game\" m68k_frame is still required");

    SyntheticCartridge cartridge;
    auto put32 = [&](std::uint32_t at, std::uint32_t value) {
        cartridge.bytes[at] = static_cast<std::uint8_t>(value >> 24); cartridge.bytes[at + 1] = static_cast<std::uint8_t>(value >> 16);
        cartridge.bytes[at + 2] = static_cast<std::uint8_t>(value >> 8); cartridge.bytes[at + 3] = static_cast<std::uint8_t>(value);
    };
    put32(0x70, 0x2800);    // the horizontal-blank interrupt (level 4)
    put32(0x78, 0x2900);    // the vertical-blank interrupt (level 6)
    cartridge.at = 0x2800;
    cartridge.Word(0x5279); cartridge.Long(0x00FF0100);      // ADDQ.W #1,$FF0100
    cartridge.Word(0x4E73);                                  // RTE
    cartridge.at = 0x2900;
    cartridge.Word(0x5279); cartridge.Long(0x00FF0102);      // ADDQ.W #1,$FF0102
    cartridge.Word(0x4E73);                                  // RTE
    cartridge.at = 0x2A00;
    cartridge.Word(0x23CE); cartridge.Long(0x00FF0108);      // MOVE.L A6,$FF0108
    cartridge.Word(0x13F9); cartridge.Long(0x00A10003); cartridge.Long(0x00FF0106);   // MOVE.B $A10003,$FF0106   (the first controller)
    cartridge.Word(0x4E75);                                  // RTS
    cartridge.at = 0x2B00;
    cartridge.Word(0x33D0); cartridge.Long(0x00FF0104);      // MOVE.W (A0),$FF0104
    cartridge.Word(0x4E75);                                  // RTS
    MegaDrive68kDriverConfig game;
    game.init = {0x2A00};
    game.vblankHandlerIsGame = true;
    game.hblankLines = 53;
    game.a6 = 0xFF0000u;
    game.ramWords = {{0xFF0110u, 0x1234}};
    game.requestRoutines = {0x2B00};
    game.requestByPointer = true;
    game.warmupFrames = 3;
    MegaDrive68kMachine machine(cartridge.bytes, game);
    machine.Initialize();
    Check(machine.RamForTest(0x108) == 0x00 && machine.RamForTest(0x109) == 0xFF && machine.RamForTest(0x10A) == 0x00 && machine.RamForTest(0x10B) == 0x00, "A6 holds the base the definition names when the routines run");
    Check(machine.RamForTest(0x110) == 0x12 && machine.RamForTest(0x111) == 0x34, "the words of m68k_ram_words are there before the routines run");
    Check(machine.RamForTest(0x106) == 0x7F, "the controller reads as one with no button pressed");
    auto word = [&](std::uint32_t at) { return static_cast<std::uint32_t>(machine.RamForTest(at) << 8 | machine.RamForTest(at + 1)); };
    const std::uint32_t hint0 = word(0x100), vint0 = word(0x102);
    machine.RunToTicksForTest(machine.TicksForTest() + 10 * kTicksPerFrame);
    const std::uint32_t hints = (word(0x100) - hint0) & 0xFFFF, vints = (word(0x102) - vint0) & 0xFFFF;
    Check(hints >= 39 && hints <= 41, "the game's own horizontal-blank handler runs 4 times a frame (lines 52, 105, 158, 211)");
    Check(vints >= 9 && vints <= 11, "the game's own vertical-blank handler runs once a frame");
    MusicCommand command;
    command.Kind = MusicCommandKind::Raw;
    command.Value = 0x2B;
    command.Auxiliary = 1 << 4;
    machine.SendCommand(command);
    machine.RunToTicksForTest(machine.TicksForTest() + kTicksPerFrame / 2);
    Check(word(0x104) == 0x002B, "the routine of a pointer request finds the number in the word A0 points at");
}
