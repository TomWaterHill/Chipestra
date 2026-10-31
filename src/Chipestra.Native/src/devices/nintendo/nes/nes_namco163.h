// license:BSD-3-Clause
// copyright-holders:Fabio Priuli
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/bus/nes/namcot.cpp  [copyright-holders:Fabio Priuli]
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill
#pragma once

#include <cstddef>
#include <cstdint>

// The program-ROM banking of Namco's 163 (mapper 19) and the decoding of its sound registers, and nothing else:
// three 8 KB banks at $8000, $A000 and $C000 (registers $E000, $E800, $F000; 6 bits each) and the last 8 KB bank
// fixed at $E000. Bit 6 of the $E000 value switches the sound off. A write to $F800 sets the sound address (its
// low four bits are also the WRAM write-protect bits, which are only received: the Machine's WRAM is always
// usable). The sound data port at $4800-$4FFF is outside this class (the caller routes it). The IRQ registers
// ($5000/$5800) and the CHR and nametable registers ($8000-$DFFF) do not change the sound and are not modelled.
class Namco163Prg {
public:
    // What a write to $8000-$FFFF did that the caller has to pass on.
    enum class Sound { None, Address, Enable, Disable };

    // `bankCount` is the number of 8 KB banks (at least 4).
    explicit Namco163Prg(std::size_t bankCount);

    // Power-on state: $8000 and $A000 show banks 0 and 1, $C000 the one before the last.
    void Reset();
    // Puts the three registers directly into a state (a Machine that starts the CPU as the game's code would
    // have left it).
    void SetState(std::uint8_t bank0, std::uint8_t bank1, std::uint8_t bank2);

    // A write to $8000-$FFFF. For Sound::Address, `soundAddress` is set to the value to give the sound chip.
    Sound Write(std::uint16_t address, std::uint8_t value, std::uint8_t& soundAddress);

    // Offset into the PRG image of the byte the CPU sees at `address` ($8000-$FFFF).
    std::size_t Offset(std::uint16_t address) const;

    std::uint8_t Bank(unsigned window) const { return banks_[window]; }
    // The last value written to $F800 (sound address in the low 7 bits, auto-increment in bit 7).
    std::uint8_t LastSoundAddressWrite() const { return last_sound_address_; }

private:
    std::size_t bank_count_;
    std::uint8_t banks_[3] = {};
    std::uint8_t last_sound_address_ = 0;
};
