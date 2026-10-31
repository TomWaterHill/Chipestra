// license:BSD-3-Clause
// copyright-holders:Fabio Priuli
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/bus/nes/sunsoft.cpp  [copyright-holders:Fabio Priuli]
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill
#pragma once

#include <cstddef>
#include <cstdint>

// The program-ROM banking of Sunsoft's FME-7 (mapper 69) and the routing of the Sunsoft 5B's sound registers, and
// nothing else. $8000-$9FFF selects a command (low four bits), $A000-$BFFF writes its data. Commands 9, A and B
// select the 8 KB banks at $8000, $A000 and $C000; the last 8 KB bank is fixed at $E000. $C000-$DFFF writes the
// sound chip's register select and $E000-$FFFF its data. The other commands (the CHR banks, the RAM/ROM switch at
// $6000, the mirroring and the IRQ counter) do not change the sound and are only received.
class Fme7Prg {
public:
    // What a write to $8000-$FFFF did that the caller has to pass on.
    enum class Sound { None, Address, Data };

    // `bankCount` is the number of 8 KB banks (at least 4).
    explicit Fme7Prg(std::size_t bankCount);

    // Power-on state: command 0, the first bank at $8000, the second at $A000 and the one before the last at $C000.
    void Reset();
    // Puts the three bank registers directly into a state (a Machine that starts the CPU as the game's code would
    // have left it); the command register stays as it is.
    void SetState(std::uint8_t bank9, std::uint8_t bankA, std::uint8_t bankB);

    // A write to $8000-$FFFF. For a sound write the value to give the chip is `value` itself.
    Sound Write(std::uint16_t address, std::uint8_t value);

    // Offset into the PRG image of the byte the CPU sees at `address` ($8000-$FFFF).
    std::size_t Offset(std::uint16_t address) const;

    std::uint8_t Bank(unsigned window) const { return banks_[window]; }
    std::uint8_t Command() const { return command_; }

private:
    std::size_t bank_count_;
    std::uint8_t banks_[3] = {};
    std::uint8_t command_ = 0;
};
