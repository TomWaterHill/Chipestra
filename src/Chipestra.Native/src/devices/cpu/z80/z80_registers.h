// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
// Portions derived from MAME (src/devices/cpu/z80/z80.cpp); see THIRD-PARTY-NOTICES.md.
#pragma once

#include <cstdint>

// A 16-bit register pair with independent 8-bit high/low access.
//
// Chipestra-owned equivalent of MAME's PAIR16 union, implemented as a plain
// struct with accessors: type-punning through a union is undefined behavior
// in strict ISO C++. The value/bit layout is identical. The `++`/`--`/`+=`/
// `-=` and implicit uint16_t conversion operators keep the ported
// instruction logic close to the original source.
struct Z80RegisterPair {
    std::uint8_t low = 0;
    std::uint8_t high = 0;

    constexpr std::uint16_t Word() const {
        return static_cast<std::uint16_t>((static_cast<std::uint16_t>(high) << 8) | low);
    }
    constexpr void SetWord(std::uint16_t value) {
        low = static_cast<std::uint8_t>(value & 0xFF);
        high = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    }

    constexpr Z80RegisterPair() = default;
    constexpr explicit Z80RegisterPair(std::uint16_t value) { SetWord(value); }

    constexpr operator std::uint16_t() const { return Word(); }
    constexpr Z80RegisterPair& operator=(std::uint16_t value) {
        SetWord(value);
        return *this;
    }

    constexpr Z80RegisterPair& operator++() {
        SetWord(static_cast<std::uint16_t>(Word() + 1));
        return *this;
    }
    constexpr Z80RegisterPair operator++(int) {
        Z80RegisterPair old = *this;
        ++*this;
        return old;
    }
    constexpr Z80RegisterPair& operator--() {
        SetWord(static_cast<std::uint16_t>(Word() - 1));
        return *this;
    }
    constexpr Z80RegisterPair operator--(int) {
        Z80RegisterPair old = *this;
        --*this;
        return old;
    }
    constexpr Z80RegisterPair& operator+=(std::uint16_t delta) {
        SetWord(static_cast<std::uint16_t>(Word() + delta));
        return *this;
    }
    constexpr Z80RegisterPair& operator-=(std::uint16_t delta) {
        SetWord(static_cast<std::uint16_t>(Word() - delta));
        return *this;
    }
};

// Flag bit positions in the Z80's F register.
constexpr std::uint8_t kZ80FlagC = 0x01;   // carry
constexpr std::uint8_t kZ80FlagN = 0x02;   // add/subtract
constexpr std::uint8_t kZ80FlagPV = 0x04;  // parity/overflow
constexpr std::uint8_t kZ80FlagH = 0x10;   // half carry
constexpr std::uint8_t kZ80FlagYX = 0x28;  // undocumented Y (bit 5) / X (bit 3) - mirror bits 5/3 of the ALU result
constexpr std::uint8_t kZ80FlagZ = 0x40;   // zero
constexpr std::uint8_t kZ80FlagS = 0x80;   // sign

// Lazily-evaluated flag state, ported from z80_device::m_f. Instructions
// stash raw result bytes here; the F register is only assembled (via ToByte)
// when something reads it (e.g. PUSH AF). Includes the XOR-fold parity trick
// in Pv(), which doubles as the overflow flag.
struct Z80Flags {
    std::uint8_t sValue = 0;    // bit 7 holds Sign; other bits don't matter
    std::uint8_t zValue = 0;    // zero iff this is 0
    std::uint8_t yxValue = 0;   // bits 5/3 hold Y/X; other bits don't matter
    std::uint8_t hValue = 0;    // bit 4 holds Half-carry; other bits don't matter
    std::uint8_t pvValue = 0;   // see Pv(): parity-fold trick doubles as overflow
    bool n = false;
    bool c = false;

    std::uint8_t q = 0;      // CCF/SCF Y/X flag quirk mask (previous instruction's flag-touch state)
    std::uint8_t qTemp = 0;

    std::uint8_t S() const { return sValue & kZ80FlagS; }
    std::uint8_t Z() const { return zValue ? 0 : kZ80FlagZ; }
    std::uint8_t Yx() const { return yxValue & kZ80FlagYX; }
    std::uint8_t H() const { return hValue & kZ80FlagH; }

    // Doubles as the parity flag (pvValue = the logical-op result byte) and
    // the overflow flag (pvValue = a 0/1 boolean); the XOR-fold below gives
    // the right polarity for both uses.
    std::uint8_t Pv() const {
        std::uint8_t val = pvValue;
        val = static_cast<std::uint8_t>(val ^ (val >> 4));
        val = static_cast<std::uint8_t>(val ^ (val << 2));
        val = static_cast<std::uint8_t>(val ^ (val >> 1));
        return static_cast<std::uint8_t>(~val & kZ80FlagPV);
    }

    std::uint8_t ToByte() const {
        std::uint8_t f = 0;
        f |= S();
        f |= Z();
        f |= Yx();
        f |= H();
        f |= Pv();
        f |= n ? kZ80FlagN : 0;
        f |= c ? kZ80FlagC : 0;
        return f;
    }

    void FromByte(std::uint8_t f) {
        sValue = f;
        zValue = static_cast<std::uint8_t>(!(f & kZ80FlagZ));
        yxValue = f;
        hValue = f;
        pvValue = static_cast<std::uint8_t>(!(f & kZ80FlagPV));
        n = (f & kZ80FlagN) != 0;
        c = (f & kZ80FlagC) != 0;
    }
};
