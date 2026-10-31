// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill

#include "nes_cartridge.h"

#include "resource_hash.h"

#include <cstdio>
#include <stdexcept>
#include <string>

NesCartridge ParseInesCartridge(const std::uint8_t* data, std::size_t size) {
    if (size < 16 || data[0] != 'N' || data[1] != 'E' || data[2] != 'S' || data[3] != 0x1A)
        throw std::runtime_error("NES cartridge: not an iNES file (missing the \"NES\" 1A header)");

    const std::uint8_t flags6 = data[6];
    const std::uint8_t flags7 = data[7];
    const bool nes2 = (flags7 & 0x0C) == 0x08;
    std::uint16_t mapper = static_cast<std::uint16_t>((flags6 >> 4) | (flags7 & 0xF0));
    std::size_t prgBanks = data[4];
    std::size_t chrBanks = data[5];
    if (nes2) {
        mapper = static_cast<std::uint16_t>(mapper | ((data[8] & 0x0F) << 8));
        if ((data[9] & 0x0F) != 0 || (data[9] >> 4) != 0)
            throw std::runtime_error("NES cartridge: NES 2.0 sizes of 4 MB or more are not supported");
    }
    if (mapper != 0 && mapper != 3 && mapper != 4 && mapper != 24 && mapper != 26 && mapper != 85 && mapper != 19 && mapper != 69) {
        char message[96];
        std::snprintf(message, sizeof(message), "NES cartridge: mapper %u is not supported (only 0, 3, 4, 19, 24, 26, 69 and 85)", mapper);
        throw std::runtime_error(message);
    }
    if (mapper == 4) {
        // MMC3: 8 KB banks, a power of two of them (16 KB units: 1, 2, 4, 8, 16 or 32).
        if (prgBanks == 0 || prgBanks > 32 || (prgBanks & (prgBanks - 1)) != 0)
            throw std::runtime_error("NES cartridge: an MMC3 PRG size must be 16, 32, 64, 128, 256 or 512 KB");
    } else if (mapper == 24 || mapper == 26) {
        // VRC6: a 16 KB bank register of 4 bits, so 32 to 256 KB in a power of two of 16 KB banks.
        if (prgBanks < 2 || prgBanks > 16 || (prgBanks & (prgBanks - 1)) != 0)
            throw std::runtime_error("NES cartridge: a VRC6 PRG size must be 32, 64, 128 or 256 KB");
    } else if (mapper == 85) {
        // VRC7: 8 KB banks selected by a 6 bit register, a power of two of them, at least the four a window each.
        if (prgBanks < 2 || prgBanks > 32 || (prgBanks & (prgBanks - 1)) != 0)
            throw std::runtime_error("NES cartridge: a VRC7 PRG size must be 32, 64, 128, 256 or 512 KB");
    } else if (mapper == 69) {
        // FME-7 / Sunsoft 5B: 8 KB banks selected by an 8 bit register, a power of two of them.
        if (prgBanks < 2 || prgBanks > 32 || (prgBanks & (prgBanks - 1)) != 0)
            throw std::runtime_error("NES cartridge: an FME-7 PRG size must be 32, 64, 128, 256 or 512 KB");
    } else if (mapper == 19) {
        // Namco 163: 8 KB banks selected by a 6 bit register, a power of two of them.
        if (prgBanks < 2 || prgBanks > 32 || (prgBanks & (prgBanks - 1)) != 0)
            throw std::runtime_error("NES cartridge: a Namco 163 PRG size must be 32, 64, 128, 256 or 512 KB");
    } else if (prgBanks != 1 && prgBanks != 2) {
        throw std::runtime_error("NES cartridge: PRG size must be 16 KB or 32 KB");
    }

    const std::size_t trainer = (flags6 & 0x04) ? 512 : 0;
    const std::size_t prgSize = prgBanks * 16384;
    const std::size_t chrSize = chrBanks * 8192;
    if (size < 16 + trainer + prgSize + chrSize)
        throw std::runtime_error("NES cartridge: the file is shorter than its header announces");

    NesCartridge cart;
    cart.mapper = mapper;
    cart.prg.assign(data + 16 + trainer, data + 16 + trainer + prgSize);
    cart.chr.assign(data + 16 + trainer + prgSize, data + 16 + trainer + prgSize + chrSize);
    return cart;
}

NesMachineInputs AssembleNesMachineInputs(const MachineResources& resources) {
    NesMachineInputs inputs;
    if (resources.Count() == 0) {
        inputs.cartridge.prg.assign(32768, 0);
        return inputs;
    }

    const ResolvedResource* cart = resources.Find(kNesCartridgeResource);
    if (cart == nullptr) throw std::runtime_error(std::string("NES: required resource \"") + kNesCartridgeResource + "\" is missing");
    const ResolvedResource* driver = resources.Find(kNesDriverResource);
    if (driver == nullptr)
        throw std::runtime_error(std::string("NES: required resource \"") + kNesDriverResource + "\" is missing (the game XML needs a <driver> element)");

    std::string error;
    if (!ParseNesDriverConfig(std::string_view(reinterpret_cast<const char*>(driver->Data()), driver->Size()), inputs.driver, &error))
        throw std::runtime_error("NES: " + error);
    inputs.cartridge = ParseInesCartridge(cart->Data(), cart->Size());

    const bool vrc7 = inputs.cartridge.mapper == 85;
    const bool n163 = inputs.cartridge.mapper == 19;
    const auto& banksCall = inputs.driver.prgBanksCall;
    if (vrc7) {
        if (!inputs.driver.vrc7RegisterLine)
            throw std::runtime_error("NES cartridge: a VRC7 game needs <driver vrc7_register_line=\"A3\" or \"A4\">");
        if (banksCall && banksCall->count != 3)
            throw std::runtime_error("NES cartridge: a VRC7 game's prg_banks_call and prg_banks_idle need three banks");
        const ResolvedResource* table = resources.Find(kNesInstrumentsResource);
        if (table == nullptr)
            throw std::runtime_error(std::string("NES cartridge: a VRC7 game needs the resource \"") + kNesInstrumentsResource +
                                     "\" (ds1001_instruments.bin from ds1001.zip)");
        inputs.vrc7Instruments.assign(table->Data(), table->Data() + table->Size());
    } else {
        if (inputs.driver.vrc7RegisterLine) throw std::runtime_error("NES cartridge: vrc7_register_line is for a VRC7 cartridge (mapper 85)");
        if (banksCall && banksCall->count != (n163 ? 3 : 2))
            throw std::runtime_error(n163 ? "NES cartridge: a Namco 163 game's prg_banks_call and prg_banks_idle need three banks"
                                          : "NES cartridge: three banks in prg_banks_call are for a VRC7 or Namco 163 cartridge (mapper 85, 19)");
    }
    if (!n163 && (inputs.driver.n163Channels || inputs.driver.n163Volume))
        throw std::runtime_error("NES cartridge: n163_channels and n163_volume are for a Namco 163 cartridge (mapper 19)");
    if (n163 && (!inputs.driver.n163Channels || !inputs.driver.n163Volume))
        throw std::runtime_error("NES cartridge: a Namco 163 game needs <driver n163_channels=\"1-8\" n163_volume=\"0-5\">");

    char message[128];
    if (inputs.driver.prgCrc32 && Crc32(inputs.cartridge.prg.data(), inputs.cartridge.prg.size()) != *inputs.driver.prgCrc32) {
        std::snprintf(message, sizeof(message), "NES cartridge: PRG CRC-32 is %08x, the game XML expects %08x",
                      Crc32(inputs.cartridge.prg.data(), inputs.cartridge.prg.size()), *inputs.driver.prgCrc32);
        throw std::runtime_error(message);
    }
    if (inputs.driver.chrCrc32 && Crc32(inputs.cartridge.chr.data(), inputs.cartridge.chr.size()) != *inputs.driver.chrCrc32) {
        std::snprintf(message, sizeof(message), "NES cartridge: CHR CRC-32 is %08x, the game XML expects %08x",
                      Crc32(inputs.cartridge.chr.data(), inputs.cartridge.chr.size()), *inputs.driver.chrCrc32);
        throw std::runtime_error(message);
    }
    return inputs;
}
