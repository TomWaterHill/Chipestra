// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Out Run reference tests.
//
// This file never hard-codes a ROM Root path or ROM bytes, and only ever prints derived
// register/RAM state (addresses, values as game state, not code) - never
// ROM-derived sample data.

#include "outrun_reference_tests.h"

#include "default_machine_catalog.h"
#include "devices/sega/outrun/outrun_machine.h"
#include "devices/sega/outrun/outrun_rom_layout.h"
#include "machine_registry.h"
#include "mixer.h"
#include "resource_resolver.h"
#include "test_support.h"
#include "chipestra_abi.h"
#include "xml_definition_parser.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

namespace {

const char* GetEnvVar(const char* name) {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)  // getenv is fine for a read-only test lookup
#endif
    return std::getenv(name);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
}

// Locate xml/outrun.xml: relative to cwd (how tools/
// verify.ps1 runs the suite), else walk up a few parents.
std::optional<std::filesystem::path> FindOutRunDefinitionFile() {
    const std::filesystem::path relative = std::filesystem::path("xml") / "outrun.xml";
    std::filesystem::path dir = std::filesystem::current_path();
    for (int depth = 0; depth < 8; ++depth) {
        std::error_code ec;
        const std::filesystem::path candidate = dir / relative;
        if (std::filesystem::exists(candidate, ec)) {
            return candidate;
        }
        if (!dir.has_parent_path() || dir.parent_path() == dir) {
            break;
        }
        dir = dir.parent_path();
    }
    return std::nullopt;
}

std::optional<std::string> ReadFileText(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

}  // namespace

void OutRunDefinitionXmlIsValid() {
    std::printf("OutRunDefinitionXmlIsValid\n");

    const auto path = FindOutRunDefinitionFile();
    Check(path.has_value(), "definition xml: xml/outrun.xml is present (run from the repo root)");
    if (!path.has_value()) {
        return;
    }

    const auto text = ReadFileText(*path);
    Check(text.has_value(), "definition xml: file is readable");
    if (!text.has_value()) {
        return;
    }

    const XmlParseResult parsed = ParseGameDefinition(*text);
    Check(parsed.Status == XmlParseStatus::Ok, "definition xml: parses cleanly");
    if (parsed.Status != XmlParseStatus::Ok) {
        std::printf("  detail: %s\n", parsed.Detail.c_str());
        return;
    }

    const GameDefinition& def = parsed.Definition;
    Check(def.Machine == "SEGA_OUTRUN", "definition xml: machine == SEGA_OUTRUN");
    Check(def.Variant == "OUTRUN_BASE", "definition xml: variant == OUTRUN_BASE");
    Check(def.Resources.size() == 7, "definition xml: seven sound ROM resources declared (soundcpu + 6 pcm sockets)");

    bool haveSoundcpu = false;
    int pcmCount = 0;
    bool allHaveCrc = true;
    for (const RomResourceDefinition& rom : def.Resources) {
        if (rom.Name == "soundcpu") {
            haveSoundcpu = true;
        }
        if (rom.Name.rfind("pcm", 0) == 0) {
            ++pcmCount;
        }
        allHaveCrc = allHaveCrc && rom.Crc32.has_value();
    }
    Check(haveSoundcpu && pcmCount == 6, "definition xml: one soundcpu + six pcm resources by logical name");
    Check(allHaveCrc, "definition xml: every <rom> carries a crc32 (MAME-catalog metadata, not ROM content)");
    Check(!def.Tracks.empty(), "definition xml: at least one <track> is declared");

    const MachineDefinition* machineDef = DefaultMachineRegistry().Find(MachineId(def.Machine));
    Check(machineDef != nullptr, "definition xml: the referenced machine (SEGA_OUTRUN) is in the catalog");
    if (machineDef != nullptr) {
        const bool variantOk = std::any_of(
            machineDef->Variants.begin(), machineDef->Variants.end(),
            [&](const MachineVariant& v) { return v.Id.Value() == def.Variant; });
        Check(variantOk, "definition xml: the referenced variant (OUTRUN_BASE) exists on that machine");
    }
}

void OutRunTrackSwitchReferenceBringUp() {
    std::printf("OutRunTrackSwitchReferenceBringUp\n");

    const char* romRootEnv = GetEnvVar("CHIPESTRA_ROM_ROOT");
    if (romRootEnv == nullptr || romRootEnv[0] == '\0') {
        std::printf(
            "  SKIP: CHIPESTRA_ROM_ROOT is not set - real-ROM bring-up not exercised (Build gate stays green "
            "without the ROMs)\n");
        return;
    }

    const auto defPath = FindOutRunDefinitionFile();
    if (!defPath.has_value()) {
        std::printf(
            "  SKIP: CHIPESTRA_ROM_ROOT is set but xml/outrun.xml was not found from the cwd - run the "
            "suite from the repo root (tools/verify.ps1 does)\n");
        return;
    }
    const auto text = ReadFileText(*defPath);
    if (!text.has_value()) {
        std::printf("  SKIP: could not read outrun.xml\n");
        return;
    }
    const XmlParseResult parsed = ParseGameDefinition(*text);
    if (parsed.Status != XmlParseStatus::Ok) {
        Check(false, "bring-up: outrun.xml must parse (also covered by OutRunDefinitionXmlIsValid)");
        return;
    }

    const ResourceResolver resolver{std::filesystem::path(romRootEnv)};
    const ResourceResolutionResult res = resolver.Resolve(parsed.Definition.BuildRequiredResources());
    if (!res.Ok) {
        std::printf("  SKIP: CHIPESTRA_ROM_ROOT is set but Out Run ROMs did not resolve under it\n");
        for (const auto& f : res.Failures) {
            std::printf("    resolve fail '%s' code %d\n", f.LogicalName.c_str(), static_cast<int>(f.Result));
        }
        return;
    }
    const OutRunSoundRoms roms = AssembleOutRunSoundRoms(res.Resources);

    OutRunMachine machine(roms.Z80Program, roms.SegaPcmSamples);
    machine.Initialize();

    // Track switching: the real driver's NMI handler advances an internal
    // 8-slot ring-buffer pointer every time it services a command, but the
    // driver's main loop only ever reads a single *fixed* slot (0xF800) to
    // decide whether a new command has arrived. On real hardware the main CPU
    // re-asserts the command-latch NMI continuously (~65-180Hz), so that slot
    // cycles back into view regardless of where a command landed.
    // OutRunCommandInterface therefore fires enough consecutive re-latches per
    // SendCommand() (one per Render() block; same-block re-latching corrupts
    // the stack) to make the ring buffer complete a full lap. This sends
    // several *different* commands in a row (like a user clicking through
    // tracks while one is playing) and confirms every one - not just the
    // first - reaches the driver's fixed mailbox slot.
    //
    // A plain post-block ReadMemory() poll is not reliable here: the
    // driver's main loop can read the value and reset the slot back to its
    // idle sentinel (0x80) within the same render block the write happened
    // in, so polling only at block boundaries can miss a transient value
    // that was, in fact, correctly delivered. A memory-write observer that
    // latches "did this exact value ever get written to F800" (not just
    // "what's the most recent write") catches it regardless of how quickly
    // the main loop consumes it afterward.
    std::uint8_t expectedValue = 0;
    bool sawExpectedValueAtMailbox = false;
    machine.Z80ForTest().SetMemoryWriteObserverForTest([&](std::uint16_t address, std::uint8_t value) {
        if (address == 0xF800 && value == expectedValue) {
            sawExpectedValueAtMailbox = true;
        }
    });

    const std::array<std::uint8_t, 5> commands{0x81, 0x82, 0x85, 0x8A, 0x84};
    const std::size_t block = Mixer::kBlockSizeFrames;
    constexpr int kBlocksToObserve = 16;  // generous margin over the 8 relatches the fix performs

    for (std::uint8_t cmd : commands) {
        expectedValue = cmd;
        sawExpectedValueAtMailbox = false;

        MusicCommand mc;
        mc.Kind = MusicCommandKind::Raw;
        mc.Value = cmd;
        machine.SendCommand(mc);

        // Always renders the full observation window, even after the target
        // value is seen: exiting early would leave this command's re-latch
        // burst (OutRunCommandInterface::pending_relatches_) mid-flight when
        // the next SendCommand() fires, which can swallow the next command's
        // own initial NMI edge (the line is already asserted, awaiting
        // service, when Send() tries to assert it again) - not a realistic
        // scenario for real usage (track switches are seconds apart, not
        // back-to-back within the same handful of render blocks).
        for (int b = 0; b < kBlocksToObserve; ++b) {
            machine.Render(EmuDuration{block}, nullptr);
        }
        char label[128];
        std::snprintf(label, sizeof(label),
                      "bring-up: command 0x%02X reached the driver's fixed command mailbox (0xF800) within %d blocks",
                      cmd, kBlocksToObserve);
        Check(sawExpectedValueAtMailbox, label);
    }
}
