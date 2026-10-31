// license:BSD-3-Clause
// copyright-holders:Fabio Priuli
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/bus/nes/konami.cpp  [copyright-holders:Fabio Priuli]
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill
#pragma once

#include <cstddef>
#include <cstdint>

// The program-ROM banking of Konami's VRC7 (mapper 85) and the decoding of its sound registers, and nothing
// else: three 8 KB banks at $8000, $A000 and $C000 (registers $8000, $8010/$8008, $9000; 6 bits each) and the
// last 8 KB bank fixed at $E000. The sound address and data registers ($9010/$9030 or $9008/$9028) are
// reported to the caller. The mirroring and WRAM-enable register ($E000) is only received (see MirroringAndWram()):
// the Machine's WRAM is always usable. CHR banks and the scanline IRQ do not change the sound and are not
// modelled.
//
// One address line (A4 or A3, the cartridge's pin 19) tells the registers of a block apart. Which one is a
// property of the board and is given to the constructor (the game definition names it; it is not guessed from
// the iNES header). Only that line is decoded; a write whose other line is set is counted
// (MismatchedWrites()) so a wrong choice shows up in a test or a diagnostic.
class Vrc7Prg {
public:
    // What a write to $8000-$FFFF did that the caller has to pass on.
    enum class Sound { None, Address, Data };

    // `bankCount` is the number of 8 KB banks (at least 4); `registerLine` is 3 or 4.
    Vrc7Prg(std::size_t bankCount, unsigned registerLine);

    // Power-on state: the four windows show the last four banks.
    void Reset();
    // Puts the three registers directly into a state (a Machine that starts the CPU as the game's code would
    // have left it).
    void SetState(std::uint8_t bank0, std::uint8_t bank1, std::uint8_t bank2);

    // A write to $8000-$FFFF.
    Sound Write(std::uint16_t address, std::uint8_t value);

    // Offset into the PRG image of the byte the CPU sees at `address` ($8000-$FFFF).
    std::size_t Offset(std::uint16_t address) const;

    std::uint8_t Bank(unsigned window) const { return banks_[window]; }
    // The last value written to $E000 (bit 7 = WRAM enable, the low bits the mirroring).
    std::uint8_t MirroringAndWram() const { return mirroring_and_wram_; }
    // Writes whose address has the line that is not the cartridge's register line set.
    std::uint64_t MismatchedWrites() const { return mismatched_writes_; }

private:
    std::size_t bank_count_;
    unsigned line_;
    std::uint8_t banks_[3] = {};
    std::uint8_t mirroring_and_wram_ = 0;
    std::uint64_t mismatched_writes_ = 0;
};
