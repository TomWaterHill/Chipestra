// license:BSD-3-Clause
// copyright-holders:Fabio Priuli
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/bus/nes/konami.cpp  [copyright-holders:Fabio Priuli]
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill
#pragma once

#include <cstddef>
#include <cstdint>

// The program-ROM banking of Konami's VRC6 (VRC VI) and the decoding of its sound registers, and nothing else:
// the 16 KB bank at $8000 (register $8000-$8FFF, 4 bits), the 8 KB bank at $C000 (register $C000-$CFFF,
// 5 bits) and the last 8 KB bank fixed at $E000. CHR banks, mirroring, the PRG RAM enable ($B003) and the
// scanline IRQ do not change the sound and are not modelled.
//
// The register number inside a block of four addresses is taken from two address lines: mapper 24 (VRC6a) uses
// A1 and A0 as they are, mapper 26 (VRC6b) swaps them, so $x001 and $x002 exchange their meaning.
class Vrc6Prg {
public:
    // `bankCount` is the number of 8 KB banks (at least 2); `swapAddressLines` is true for mapper 26.
    Vrc6Prg(std::size_t bankCount, bool swapAddressLines);

    // Power-on state: both banks 0.
    void Reset();
    // Puts the two registers directly into a state (a Machine that starts the CPU as the game's code would
    // have left it).
    void SetState(std::uint8_t bank16, std::uint8_t bank8);

    // A write to $8000-$FFFF. Returns true when it is a write to a sound register, with the original sound
    // chip's register offset (0x000-0x003 pulse 1 and the frequency control, 0x100-0x102 pulse 2, 0x200-0x202
    // sawtooth) in *soundOffset; the caller passes it on to the sound chip.
    bool Write(std::uint16_t address, std::uint8_t value, unsigned* soundOffset);

    // Offset into the PRG image of the byte the CPU sees at `address` ($8000-$FFFF).
    std::size_t Offset(std::uint16_t address) const;

    std::uint8_t Bank16() const { return bank16_; }
    std::uint8_t Bank8() const { return bank8_; }

private:
    std::size_t bankCount_;
    bool swap_;
    std::uint8_t bank16_ = 0;
    std::uint8_t bank8_ = 0;
};
