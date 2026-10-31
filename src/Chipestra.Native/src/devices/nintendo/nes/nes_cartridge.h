// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include "nes_driver_config.h"
#include "resource_resolver.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// A Famicom/NES cartridge as far as the sound is concerned: the program data (PRG) the CPU sees at
// $8000-$FFFF, and the graphics data (CHR), kept only to check it. CHR bank switching does not change the
// sound, so the mappers handled are mapper 0 (NROM: 16 KB mirrored through $8000-$FFFF, or 32 KB), mapper 3
// (CNROM: the same fixed 32 KB, only CHR is switched) and mapper 4 (MMC3: up to 512 KB in 8 KB banks, see
// Mmc3Prg; the cartridge has 8 KB of WRAM at $6000-$7FFF) and mappers 24 and 26 (Konami VRC6: 32 to 256 KB, a
// 16 KB bank at $8000 and an 8 KB bank at $C000, see Vrc6Prg; the VRC6 adds three sound channels) and mapper 85
// (Konami VRC7: 32 to 512 KB, three 8 KB banks, see Vrc7Prg; the VRC7 adds six FM channels whose built-in
// instruments come from a ROM the user supplies) and mapper 19 (Namco 163: 32 to 512 KB, three 8 KB banks, see
// Namco163Prg; the chip adds up to eight wavetable channels) and mapper 69 (Sunsoft FME-7 / 5B: 32 to 512 KB, three 8 KB banks, see
// Fme7Prg; the 5B adds three tone channels).
struct NesCartridge {
    std::uint16_t mapper = 0;
    std::vector<std::uint8_t> prg;  // 16384 or 32768 bytes (MMC3: 16 KB x a power of two)
    std::vector<std::uint8_t> chr;  // may be empty (CHR RAM)
};

// Reads an iNES (or NES 2.0) file image: checks the header ("NES" 1A), the mapper, the PRG size (1 or 2
// banks of 16 KB) and that the file holds all the data it announces (a 512-byte trainer is skipped). Throws
// std::runtime_error naming the problem.
NesCartridge ParseInesCartridge(const std::uint8_t* data, std::size_t size);

// Everything a NesMachine is built from.
struct NesMachineInputs {
    NesCartridge cartridge;
    NesDriverConfig driver;
    std::vector<std::uint8_t> vrc7Instruments;  // the 144-byte instrument table, VRC7 cartridges only
};

// Logical resource names: the whole .nes file, and the text of the game's <driver> element (supplied by
// Managed Core from the game definition XML).
inline constexpr const char* kNesCartridgeResource = "cart";
inline constexpr const char* kNesDriverResource = "driver_xml";
// The VRC7's built-in instrument table (ds1001_instruments.bin of the "ds1001" archive), required for a VRC7 cartridge.
inline constexpr const char* kNesInstrumentsResource = "instruments";

// Maps resolved resources onto a cartridge and driver settings. An entirely empty `resources` yields a
// registered-but-not-playable set (a zero-filled 32 KB PRG and no routines). Once any resource is supplied
// both are required, and the cartridge's PRG and CHR CRC-32 must match the driver's prg_crc32/chr_crc32 when
// those are given, and a VRC7 cartridge also needs the instrument table and the settings that go with it
// (vrc7_register_line, three banks): a problem throws std::runtime_error.
NesMachineInputs AssembleNesMachineInputs(const MachineResources& resources);
