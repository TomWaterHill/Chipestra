// license:BSD-3-Clause
// copyright-holders:Juergen Buchmueller
// copyright-holders:Juergen Buchmueller, Andrei I. Holub
// Ported and adapted for Chipestra from MAME (https://github.com/mamedev/mame, tag mame0289):
//   src/devices/cpu/z80/z80.cpp  [copyright-holders:Juergen Buchmueller]
//   src/devices/cpu/z80/z80.h  [copyright-holders:Juergen Buchmueller]
//   src/devices/cpu/z80/z80.lst  [copyright-holders:Juergen Buchmueller, Andrei I. Holub]
// See THIRD-PARTY-NOTICES.md.
// Changes for Chipestra: Copyright (C) 2026 Tom Water Hill

#pragma once

#include "iz80_core.h"
#include "z80_registers.h"

#include "device_manager.h"

#include <cstdint>
#include <functional>
#include <utility>

// IZ80Core implementation whose register/flag semantics are ported from
// MAME's z80_device (see THIRD-PARTY-NOTICES.md). No MAME type appears in
// this header or in mame_derived_z80_adapter.cpp; MAME's device-framework
// coupling is replaced by IZ80Core's callback-based design.
//
// Instruction-level cycle counts are the standard, publicly documented Z80
// timings, not MAME's generated timing table.
//
// Also implements IDevice so a Machine's DeviceManager can own it like any
// other device.
class MameDerivedZ80Adapter : public IZ80Core, public IDevice {
public:
    MameDerivedZ80Adapter();

    void SetMemoryCallbacks(MemoryReadFn read, MemoryWriteFn write) override;
    void SetIoCallbacks(IoReadFn read, IoWriteFn write) override;
    void SetInterruptAcknowledgeCallback(InterruptAckFn callback) override;
    void Reset() override;
    std::uint64_t Execute(std::uint64_t cycleBudget) override;
    void SetInputLine(Z80InputLine line, Z80InputLineState state) override;

    // Asserts the maskable interrupt as a HOLD_LINE: the line stays asserted until the CPU
    // accepts the interrupt, then clears itself. Not part of IZ80Core. A later SetInputLine(Irq, ...)
    // call replaces this behavior with the plain level semantics.
    void AssertIrqHoldLine();

    // Register snapshot for tests and diagnostics; not part of IZ80Core.
    // Ordinary Machine/Mixer code does not need raw register access.
    struct RegisterSnapshot {
        std::uint16_t af = 0, bc = 0, de = 0, hl = 0, ix = 0, iy = 0, sp = 0, pc = 0;
        std::uint16_t af2 = 0, bc2 = 0, de2 = 0, hl2 = 0;
        std::uint8_t i = 0, r = 0;
        std::uint8_t im = 0;
        bool iff1 = false, iff2 = false;
        bool halted = false;
        // Maskable interrupts serviced in IM 0, and how many of those
        // injected a byte matching none of the CALL nnnn/JP nnnn/RST xx/EI
        // patterns (silently ignored, as in MAME).
        std::uint64_t im0ServiceCount = 0;
        std::uint64_t im0UnhandledServiceCount = 0;
        std::uint64_t nmiServiceCount = 0;
        std::uint64_t irqServiceCount = 0;  // total maskable INTs serviced (all IMs)
    };
    RegisterSnapshot Snapshot() const;

    // T-states consumed so far by the in-progress Execute() call. Lets a
    // Machine sync co-processors (YM2151/SegaPCM) to the CPU's exact progress
    // from inside a bus callback, instead of only at block boundaries (sound
    // drivers that busy-poll a chip status register need it fresh at the
    // moment of the IN). Not part of IZ80Core. Reset to 0 at the top of every
    // Execute().
    std::uint64_t CyclesConsumedThisCall() const { return cyclesConsumedThisCall_; }

    // Diagnostic-only: invoked for every opcode fetch with the PC the byte was
    // read from and the byte itself (prefix bytes CB/ED/DD/FD included). Null
    // by default, zero cost when unset. Not part of IZ80Core.
    using OpcodeFetchObserver = std::function<void(std::uint16_t pc, std::uint8_t opcode)>;
    void SetOpcodeFetchObserverForTest(OpcodeFetchObserver observer) {
        opcodeFetchObserver_ = std::move(observer);
    }

    // Diagnostic-only: invoked for every CPU memory *write* with the target
    // address and byte. Null by default, zero cost when unset. Not part of
    // IZ80Core.
    using MemoryWriteObserver = std::function<void(std::uint16_t address, std::uint8_t value)>;
    void SetMemoryWriteObserverForTest(MemoryWriteObserver observer) {
        memoryWriteObserver_ = std::move(observer);
    }

private:
    Z80RegisterPair af_, bc_, de_, hl_, ix_, iy_, sp_, pc_, wz_;
    Z80RegisterPair af2_, bc2_, de2_, hl2_;
    Z80Flags f_;
    std::uint8_t i_ = 0;
    std::uint8_t r_ = 0;   // bits 0-6 of the refresh counter
    std::uint8_t r2_ = 0;  // bit 7 of the refresh counter (preserved separately - real hardware quirk)
    bool iff1_ = false;
    bool iff2_ = false;
    std::uint8_t im_ = 0;
    bool halted_ = false;

    std::uint64_t im0ServiceCount_ = 0;
    std::uint64_t im0UnhandledServiceCount_ = 0;
    std::uint64_t nmiServiceCount_ = 0;
    std::uint64_t irqServiceCount_ = 0;

    bool nmiPending_ = false;
    bool irqAsserted_ = false;
    bool irqHoldLine_ = false;  // irqAsserted_ clears when the interrupt is accepted
    Z80InputLineState nmiLineState_ = Z80InputLineState::Clear;
    // EI's documented one-instruction delay: interrupts are not recognized
    // again until after the instruction immediately following EI has run.
    bool eiDelayActive_ = false;

    MemoryReadFn memoryRead_;
    MemoryWriteFn memoryWrite_;
    IoReadFn ioRead_;
    IoWriteFn ioWrite_;
    InterruptAckFn interruptAck_;
    OpcodeFetchObserver opcodeFetchObserver_;
    MemoryWriteObserver memoryWriteObserver_;

    std::uint64_t cyclesConsumedThisCall_ = 0;

    void AddCycles(int cycles);
    std::uint8_t ReadMemory(std::uint16_t address) const;
    void WriteMemory(std::uint16_t address, std::uint8_t value) const;
    std::uint8_t FetchOpcode();
    std::uint8_t FetchByte();
    std::uint16_t FetchWord();

    std::uint8_t GetF() const { return f_.ToByte(); }
    void SetF(std::uint8_t value) { f_.FromByte(value); }

    // Returns true if an NMI/IRQ was actually serviced this call - the
    // caller must not also fetch/dispatch a normal instruction in that
    // case (a serviced interrupt replaces the instruction that would have
    // been fetched next, it doesn't run alongside it).
    bool CheckPendingInterrupts();
    void ServiceNmi();
    void ServiceIrq();

    // `hlReg` names the 16-bit register currently standing in for HL (HL
    // itself, or IX/IY under a DD/FD prefix); `useIndexedHl` says whether
    // an (HL)-slot memory access should instead fetch a displacement byte
    // and access (index+d). Parameterizing the same dispatch this way -
    // rather than duplicating it per prefix - is the standard technique
    // for the substitution the real DD/FD prefixes perform on hardware;
    // it isn't specific to MAME.
    void ExecuteUnprefixed(std::uint8_t opcode, Z80RegisterPair& hlReg, bool useIndexedHl);
    void ExecuteCb(Z80RegisterPair& hlReg, bool useIndexedHl);
    void ExecuteEd(std::uint8_t opcode);

    // ALU/rotate/shift/bit primitives, ported from z80_device's
    // identically-named private methods.
    void OpInc(std::uint8_t& r);
    void OpDec(std::uint8_t& r);
    void OpRlca();
    void OpRrca();
    void OpRla();
    void OpRra();
    void OpAddA(std::uint8_t value);
    void OpAdcA(std::uint8_t value);
    void OpSubA(std::uint8_t value);
    void OpSbcA(std::uint8_t value);
    void OpNeg();
    void OpDaa();
    void OpAndA(std::uint8_t value);
    void OpOrA(std::uint8_t value);
    void OpXorA(std::uint8_t value);
    void OpCp(std::uint8_t value);
    void OpExx();
    std::uint8_t OpRlc(std::uint8_t value);
    std::uint8_t OpRrc(std::uint8_t value);
    std::uint8_t OpRl(std::uint8_t value);
    std::uint8_t OpRr(std::uint8_t value);
    std::uint8_t OpSla(std::uint8_t value);
    std::uint8_t OpSra(std::uint8_t value);
    std::uint8_t OpSll(std::uint8_t value);
    std::uint8_t OpSrl(std::uint8_t value);
    void OpBit(int bit, std::uint8_t value);
    void OpAddHl(Z80RegisterPair& hlReg, std::uint16_t value);
    void OpAdcHl(std::uint16_t value);
    void OpSbcHl(std::uint16_t value);

    void PushWord(std::uint16_t value);
    std::uint16_t PopWord();
    void JumpRelative(std::int8_t displacement);
    bool TestCondition(int conditionCode) const;  // 0..7 => NZ,Z,NC,C,PO,PE,P,M
};
