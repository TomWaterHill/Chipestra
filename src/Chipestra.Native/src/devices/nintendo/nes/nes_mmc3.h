// license:BSD-3-Clause
// copyright-holders:Fabio Priuli
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/bus/nes/mmc3.cpp  [copyright-holders:Fabio Priuli]
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill
#pragma once

#include <cstddef>
#include <cstdint>

// The program-ROM banking of Nintendo's MMC3 (mapper 4, the TxROM boards) and nothing else: the bank select
// register ($8000, even addresses), the bank data register ($8001, odd addresses), the two 8 KB switchable
// banks R6 and R7 and the PRG mode bit. CHR banks, mirroring, WRAM protect and the scanline IRQ do not change
// the sound and are not modelled; the 8 KB WRAM at $6000-$7FFF is owned by the Machine and always usable.
//
//   mode 0: $8000 = R6, $A000 = R7, $C000 = second-to-last bank, $E000 = last bank
//   mode 1: $8000 = second-to-last bank, $A000 = R7, $C000 = R6, $E000 = last bank
class Mmc3Prg {
public:
    // `bankCount` is the number of 8 KB banks (a power of two).
    explicit Mmc3Prg(std::size_t bankCount);

    // Power-on state as the original's: mode 0, R6 = 0, R7 = 1 (MAME reset values 0xFFE/0xFFF masked).
    void Reset();
    // Puts the registers directly into a state (a Machine that starts the CPU as the game's code would have left it).
    void SetState(std::uint8_t r6, std::uint8_t r7, bool prgMode1);

    // A write to $8000-$FFFF; only the bank select and bank data registers are decoded.
    void Write(std::uint16_t address, std::uint8_t value);

    // Offset into the PRG image of the byte the CPU sees at `address` ($8000-$FFFF).
    std::size_t Offset(std::uint16_t address) const;

private:
    std::size_t bankCount_;
    std::uint8_t latch_ = 0;
    std::uint8_t r6_ = 0, r7_ = 1;
};
