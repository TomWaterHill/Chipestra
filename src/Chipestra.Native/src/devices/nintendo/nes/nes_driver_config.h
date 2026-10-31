// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The settings of a game whose sound driver is part of the game program and is called as ordinary
// routines of that program (a Famicom/NES cartridge): read from the game definition XML's <driver> element.
// Nothing here is a ROM byte; every number is a CPU address or the address of a variable.
//
//   <driver ram_clear="0x0000-0x07FF" init="0xB666 0xB809" frame="0xB690 0xB82A"
//           bgm="0x95" sfx="0x8C" prg_crc32="e6c4ec06" chr_crc32="67651e57"/>
//
//   ram_clear  RAM range set to 0 at start (without it nothing is cleared and the RAM holds 0xFF)
//   init       routines called once, in order, after the RAM clear
//   frame      routines called at the start of every frame, in order
//   bgm / sfx  the RAM variable a song number / an effect number is written to
//   prg_crc32 / chr_crc32  expected CRC-32 of the cartridge's PRG and CHR data (checked when present)
//   ram_set    "address=value" pairs (space separated) written to RAM after the RAM clear and before the
//              init routines, e.g. ram_set="0x0028=0x7C 0x0680=0x01"
//   warmup_frames  frames of the frame routines run at start-up before the first request (default 1)
//   request_call  address of a routine the game calls with the request number in the A register (a driver that
//              takes its requests as a call, not through RAM variables). A track's command is that number, 0 included; a
//              <track type="control"> (the Music Stop) makes no request: the Machine only starts again from the freshly
//              initialized state.
//   prg_banks_call / prg_banks_idle  (MMC3 cartridges) "R6 R7": the PRG banks the game maps while its sound
//              routines run and the banks it has mapped the rest of the time (the program banks the sound data
//              reads through while the routines are called, and the banks the DMC samples are read from in
//              between). The generated code switches to the first pair, calls a routine with X = 0 and switches
//              to the second pair; both are required together. Example: prg_banks_call="0x1D 0x1C"
//              prg_banks_idle="0x0A 0x1A". For a VRC6 cartridge (mapper 24 or 26) the two numbers are the values
//              of the registers at $8000 (the 16 KB bank) and $C000 (the 8 KB bank): prg_banks_call="0x0C 0x1E".
//              For a VRC7 cartridge (mapper 85) there are three numbers, the banks at $8000, $A000 and $C000:
//              prg_banks_call="0 1 2" prg_banks_idle="0 1 2". For an FME-7 / Sunsoft 5B cartridge (mapper 69) the two
//              numbers are the banks of commands 9 and A (the banks at $8000 and $A000); command B (the bank at $C000,
//              where the DMC reads) is never written, the driver chooses it: prg_banks_call="0x07 0x00".
//   vrc7_register_line  (VRC7 cartridges, required) "A3" or "A4": the address line that tells the two registers
//              of a block apart on this board. It is not read from the iNES header.
//   frame_counter  the RAM address of a byte the generated frame code adds 1 to once a frame (after the frame
//              routines and the request), for a driver that reads a frame count the game's own code keeps:
//              frame_counter="0x0022"
//   requests   (also for a cartridge) the RAM addresses a track may write its value to; a track names one with
//              request="0x04F5" (see the FDS note below). A raw command without a request goes to bgm.
//
// A Famicom Disk System game uses the same element for its disk files, see the FDS members below:
//
//   <driver ... base="MET.HEX" requests="0x0680 0x0681 0x0682 0x0684 0x0685">
//     <file name="MET.HEX" crc32="474edcec"/> ...
//     <set id="area_a" files="MMROCK STG1PGM"/> ...
//   </driver>
struct NesDriverConfig {
    // The files (of the disk image) a Famicom Disk System game uses, found by name and CRC-32.
    struct DiskFile {
        std::string name;
        std::optional<std::uint32_t> crc32;
    };
    // A combination of files loaded on top of the base files.
    struct FileSet {
        std::string id;
        std::vector<std::string> files;  // names listed in `diskFiles`
        // Written to RAM after the driver's own ram_set, for the tracks that use this set
        // (<set id="x" files="..." ram_set="0x0686=0x01"/>; the files may be empty).
        std::vector<std::pair<std::uint16_t, std::uint8_t>> ramSet;
    };

    std::optional<std::pair<std::uint16_t, std::uint16_t>> ramClear;  // first, last (inclusive)
    std::vector<std::uint16_t> init;
    std::vector<std::uint16_t> frame;
    std::optional<std::uint16_t> bgmVariable;
    std::optional<std::uint16_t> sfxVariable;
    std::optional<std::uint16_t> requestCall;
    std::optional<std::uint32_t> prgCrc32;
    std::optional<std::uint32_t> chrCrc32;
    std::vector<std::pair<std::uint16_t, std::uint8_t>> ramSet;
    std::uint32_t warmupFrames = 1;

    // MMC3: R6 and R7 while the sound routines run (`call`) and between them (`idle`). VRC6: the register
    // values for the 16 KB bank at $8000 (r6) and the 8 KB bank at $C000 (r7).
    // VRC7 and Namco 163: r6, r7 and r8 are the registers of the banks at $8000, $A000 and $C000 (`count` is 3).
    struct PrgBanks {
        std::uint8_t r6 = 0;
        std::uint8_t r7 = 0;
        std::uint8_t r8 = 0;
        std::uint8_t count = 2;
    };
    std::optional<PrgBanks> prgBanksCall;
    std::optional<PrgBanks> prgBanksIdle;
    std::optional<std::uint8_t> vrc7RegisterLine;  // 3 or 4
    std::optional<std::uint16_t> frameCounter;
    // Namco 163: the number of sound channels the meters show (1-8, the last ones) and the MAME submapper (0-5)
    // whose sound gain is used.
    std::optional<std::uint8_t> n163Channels;
    std::optional<std::uint8_t> n163Volume;

    // Famicom Disk System only.
    std::vector<DiskFile> diskFiles;
    std::vector<std::string> baseFiles;      // always loaded, first
    std::vector<FileSet> sets;               // at most 15
    std::vector<std::uint16_t> requests;     // addresses a track may write its value to, at most 15

    // How a track picks a set and a request address: the Auxiliary byte of its command is
    // kControlFlag (control tracks) | (request number << 4) | set number. Set number 0 is the base files alone, 1.. are the <set> elements
    // in order; request number 0 means "no request" (the track only loads its files), 1.. are the entries
    // of `requests` in order.
    static constexpr std::uint32_t kMaxSets = 15;
    static constexpr std::uint32_t kMaxRequests = 7;
    // Bit 7 of the byte is set for a <track type="control"> (the Music Stop): the Machine only resets and makes no
    // request for it.
    static constexpr std::uint8_t kControlFlag = 0x80;
    static std::uint8_t PackSelector(std::uint32_t setNumber, std::uint32_t requestNumber) {
        return static_cast<std::uint8_t>((setNumber & 0xF) | ((requestNumber & 0x7) << 4));
    }
    static std::uint32_t SetNumberOf(std::uint8_t auxiliary) { return auxiliary & 0xF; }
    static std::uint32_t RequestNumberOf(std::uint8_t auxiliary) { return (auxiliary >> 4) & 0x7; }
    static bool IsControl(std::uint8_t auxiliary) { return (auxiliary & kControlFlag) != 0; }
};

// Parses the text of a <driver .../> element. On a problem returns false and describes it in *error.
bool ParseNesDriverConfig(std::string_view driverXml, NesDriverConfig& out, std::string* error);

// Returns the text of the first <chipestra><game><driver> element of a whole game definition document, or an
// empty string if there is none (or the document cannot be parsed).
std::string ExtractDriverElementXml(std::string_view gameDefinitionXml);
