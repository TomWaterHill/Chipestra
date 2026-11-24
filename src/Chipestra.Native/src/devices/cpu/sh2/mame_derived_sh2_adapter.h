// license:BSD-3-Clause
// copyright-holders:David Haywood, Juergen Buchmueller, R. Belmont
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/cpu/sh/sh.cpp  [copyright-holders:David Haywood]
//   src/devices/cpu/sh/sh2.cpp  [copyright-holders:Juergen Buchmueller, R. Belmont]
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#pragma once

#include "ish2_core.h"

#include <cstdint>
#include <utility>

// ISh2Core implementation ported from MAME's SH-2 interpreter (mame0289, BSD-3-Clause): the instruction set of the SH7604
// class SH-2, the delayed branches (the instruction after the branch runs before it takes effect, and no interrupt is taken
// between the two), the exception sequence, and the per-instruction cycle counts of the interpreter. The on-chip peripherals
// of the SH7604 are not part of this class.
//
// Differences from MAME: the interrupt check happens at every instruction boundary (MAME checks when a line changes or after
// an instruction that can unmask), and the cycle counts of OR #imm,R0 (1) and OR.B #imm,@(R0,GBR) (3) are those of the
// Hitachi manual: MAME has the two exchanged.
class MameDerivedSh2Adapter : public ISh2Core {
public:
    MameDerivedSh2Adapter() = default;

    void SetCallbacks(Callbacks callbacks) override { cb_ = std::move(callbacks); }
    void Reset() override;
    std::uint64_t Execute(std::uint64_t cycleBudget) override;
    void SetInterruptLine(int level, bool asserted) override;
    void SetNmi(bool asserted) override;
    void SetInternalInterrupt(int level, std::uint32_t vector) override;

    // Cycles consumed so far by the Execute() call in progress (0 outside one), counted up to the instruction being executed.
    std::uint64_t CyclesConsumedThisCall() const { return executing_ ? consumedBefore_ + cycles_ : 0; }

    static constexpr std::uint32_t kT = 0x1, kS = 0x2, kI = 0xF0, kQ = 0x100, kM = 0x200, kSrMask = kM | kQ | kI | kS | kT;

    struct Registers {
        std::uint32_t r[16] = {};
        std::uint32_t pc = 0, pr = 0, sr = 0, gbr = 0, vbr = 0, mach = 0, macl = 0;
        bool inDelaySlot = false;
        std::uint32_t exceptionCount = 0;
    };
    // Declares that the bytes at addresses a with (a & tagMask) == tag are data[a & offsetMask] (size bytes, big endian), so reads
    // there skip the callbacks. At most two windows; the owner keeps the memory alive and in place.
    void SetReadWindow(int index, const std::uint8_t* data, std::uint32_t tagMask, std::uint32_t tag, std::uint32_t offsetMask, std::uint32_t size) {
        windows_[index] = Window{data, tagMask, tag, offsetMask, size};
    }
    Registers GetRegisters() const;
    // For tests and the machine's start-up: sets the registers (the delay slot is cleared).
    void SetRegisters(const Registers& registers);

private:
    void ExecuteOne(std::uint16_t opcode);
    void ExecuteGroup0(std::uint16_t op);
    void ExecuteGroup2(std::uint16_t op);
    void ExecuteGroup3(std::uint16_t op);
    void ExecuteGroup4(std::uint16_t op);
    void ExecuteGroup6(std::uint16_t op);
    void ExecuteGroup8(std::uint16_t op);
    void ExecuteGroupC(std::uint16_t op);

    void Illegal();
    void Div1(std::uint32_t m, std::uint32_t n);
    void MacL(std::uint32_t m, std::uint32_t n);
    void MacW(std::uint32_t m, std::uint32_t n);
    void Branch(std::uint32_t target) { delayTarget_ = target; hasDelay_ = true; }
    void SetT(bool value) { sr_ = (sr_ & ~kT) | (value ? kT : 0); }
    int PendingLevel() const;
    void TakeException(int level);
    void PushAndVector(int vector);

    // A read inside a window the owner has declared is answered from its memory (the same bytes the callback would return)
    const std::uint8_t* WindowBytes(std::uint32_t a, std::uint32_t bytes) const {
        for (const Window& w : windows_) {
            if ((a & w.tagMask) == w.tag) {
                const std::uint32_t offset = a & w.offsetMask;
                return offset + bytes <= w.size ? w.data + offset : nullptr;
            }
        }
        return nullptr;
    }
    std::uint8_t Read8(std::uint32_t a) { if (const std::uint8_t* p = WindowBytes(a, 1)) return p[0]; return cb_.read8 ? cb_.read8(a) : 0; }
    std::uint16_t Read16(std::uint32_t a) {
        if (const std::uint8_t* p = WindowBytes(a, 2)) return static_cast<std::uint16_t>(p[0] << 8 | p[1]);
        return cb_.read16 ? cb_.read16(a) : 0;
    }
    std::uint32_t Read32(std::uint32_t a) {
        if (const std::uint8_t* p = WindowBytes(a, 4)) return static_cast<std::uint32_t>(p[0]) << 24 | static_cast<std::uint32_t>(p[1]) << 16 | static_cast<std::uint32_t>(p[2]) << 8 | p[3];
        return cb_.read32 ? cb_.read32(a) : 0;
    }
    void Write8(std::uint32_t a, std::uint8_t v) { if (cb_.write8) cb_.write8(a, v); }
    void Write16(std::uint32_t a, std::uint16_t v) { if (cb_.write16) cb_.write16(a, v); }
    void Write32(std::uint32_t a, std::uint32_t v) { if (cb_.write32) cb_.write32(a, v); }

    struct Window {
        const std::uint8_t* data = nullptr;
        std::uint32_t tagMask = 0, tag = 0xFFFFFFFFu, offsetMask = 0, size = 0;
    };
    Window windows_[2];
    Callbacks cb_;
    std::uint32_t r_[16] = {};
    std::uint32_t pc_ = 0, pr_ = 0, sr_ = kI, gbr_ = 0, vbr_ = 0, mach_ = 0, macl_ = 0;
    bool hasDelay_ = false;
    std::uint32_t delayTarget_ = 0;
    std::uint32_t pendingExternal_ = 0;   // bit n: level n asserted
    bool pendingNmi_ = false, nmiLine_ = false;
    int internalLevel_ = 0;
    std::uint32_t internalVector_ = 0;
    int sleepMode_ = 0;                   // 0 running, 1 sleeping, 2 woken by an exception
    std::uint32_t exceptionCount_ = 0;

    bool executing_ = false;
    std::uint64_t consumedBefore_ = 0;
    std::uint64_t cycles_ = 1;            // the cycles of the instruction in progress
};
