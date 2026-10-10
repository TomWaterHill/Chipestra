// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Portions derived from MAME (src/devices/cpu/m68000/m68kcpu.h); see THIRD-PARTY-NOTICES.md.
#pragma once

#include <cstdint>

// The 68000 Status Register, split into the user-visible Condition Code Register (CCR,
// bits 0-4) and the supervisor system byte (T/S/interrupt mask, bits 8-15; the other bits
// read as 0 on a real 68000).
//
// Individual bool flags are used rather than Musashi's sentinel-value encoding
// (m_x_flag etc. in m68kcpu.h), as Z80RegisterPair does for MAME's PAIR16.
struct M68000StatusRegister {
    bool trace = false;        // T: single-step trace exception armed
    bool supervisor = true;    // S: supervisor (1) vs user (0) mode
    std::uint8_t interruptMask = 7;  // I2-I0: interrupt priority mask (0-7)
    bool x = false;  // extend
    bool n = false;  // negative
    bool z = false;  // zero
    bool v = false;  // overflow
    bool c = false;  // carry

    std::uint16_t ToWord() const {
        std::uint16_t sr = 0;
        if (trace) sr |= 0x8000;
        if (supervisor) sr |= 0x2000;
        sr |= static_cast<std::uint16_t>((interruptMask & 0x07) << 8);
        sr |= ToCcr();
        return sr;
    }

    std::uint16_t ToCcr() const {
        std::uint16_t ccr = 0;
        if (x) ccr |= 0x10;
        if (n) ccr |= 0x08;
        if (z) ccr |= 0x04;
        if (v) ccr |= 0x02;
        if (c) ccr |= 0x01;
        return ccr;
    }

    void FromCcr(std::uint16_t value) {
        x = (value & 0x10) != 0;
        n = (value & 0x08) != 0;
        z = (value & 0x04) != 0;
        v = (value & 0x02) != 0;
        c = (value & 0x01) != 0;
    }

    // Sets every field from a full 16-bit SR value. It does not swap A7 between USP and SSP
    // on an S-bit change; the caller (MameDerivedM68000Adapter) owns that storage.
    void FromWord(std::uint16_t value) {
        trace = (value & 0x8000) != 0;
        supervisor = (value & 0x2000) != 0;
        interruptMask = static_cast<std::uint8_t>((value >> 8) & 0x07);
        FromCcr(value & 0x1F);
    }
};

// M68000Registers - the full user+supervisor register file. `a[7]` is always the active
// stack pointer (USP while `sr.supervisor` is false, SSP while true); `usp`/`ssp` are the
// shadow copies swapped in/out of `a[7]` whenever `sr.supervisor` changes.
struct M68000Registers {
    std::uint32_t d[8] = {};
    std::uint32_t a[8] = {};
    std::uint32_t usp = 0;
    std::uint32_t ssp = 0;
    std::uint32_t pc = 0;
    M68000StatusRegister sr;
};
